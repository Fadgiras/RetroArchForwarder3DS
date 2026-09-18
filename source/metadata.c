#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "metadata.h"

// Overridable so the host harness can point at a local copy.
#ifndef INDEX_DIR
#define INDEX_DIR "romfs:/db"
#endif
#define SCAN_BYTES (2 * 1024 * 1024)   // SYSTEM.CNF always sits in the first megabytes
#define SFO_MAX 4096

static uint32_t rd32(const unsigned char* p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool hasExt(const char* path, const char* ext)
{
	size_t n = strlen(path), e = strlen(ext);
	return n > e && strcasecmp(path + n - e, ext) == 0;
}

// "SLES_000.49" and "SLES00868" both become "SLES-00049" style.
static bool normalise(const char* raw, char* out, size_t out_len)
{
	char digits[16];
	size_t d = 0, i;

	if (strlen(raw) < 8) return false;
	for (i = 0; i < 4; i++)
		if (!isalpha((unsigned char)raw[i])) return false;

	for (i = 4; raw[i] && d < sizeof(digits) - 1; i++)
		if (isdigit((unsigned char)raw[i])) digits[d++] = raw[i];
	digits[d] = 0;
	if (d < 5) return false;

	// Some serials carry a trailing revision digit; the first five identify
	// the disc.
	if (snprintf(out, out_len, "%c%c%c%c-%.5s",
		toupper((unsigned char)raw[0]), toupper((unsigned char)raw[1]),
		toupper((unsigned char)raw[2]), toupper((unsigned char)raw[3]),
		digits) < 0)
		return false;
	return true;
}

// ---------------------------------------------------------------- pbp

// A pbp is a table of offsets: PARAM.SFO, ICON0.PNG, ICON1.PMF, PIC0.PNG,
// PIC1.PNG, SND0.AT3, DATA.PSP, DATA.PSAR.
static bool pbpSection(const char* path, int index, long* offset, long* size)
{
	unsigned char head[0x28];
	FILE* f = fopen(path, "rb");
	uint32_t here, next;

	if (!f) return false;
	if (fread(head, 1, sizeof(head), f) != sizeof(head) || memcmp(head, "\0PBP", 4) != 0)
	{
		fclose(f);
		return false;
	}
	fclose(f);

	if (index < 0 || index > 7) return false;
	here = rd32(head + 8 + index * 4);
	next = (index < 7) ? rd32(head + 8 + (index + 1) * 4) : here;
	if (next < here) return false;

	*offset = (long)here;
	*size = (long)(next - here);
	return true;
}

// PARAM.SFO: a key table and a data table, joined by a list of entries.
static bool sfoLookup(const char* path, const char* wanted, char* out, size_t out_len)
{
	unsigned char* sfo;
	long off, size;
	uint32_t key_tab, data_tab, count, i;
	FILE* f;
	bool found = false;

	if (!pbpSection(path, 0, &off, &size)) return false;
	if (size < 0x14 || size > SFO_MAX) return false;

	sfo = malloc((size_t)size);
	if (!sfo) return false;

	f = fopen(path, "rb");
	if (!f || fseek(f, off, SEEK_SET) != 0
	    || fread(sfo, 1, (size_t)size, f) != (size_t)size
	    || memcmp(sfo, "\0PSF", 4) != 0)
	{
		if (f) fclose(f);
		free(sfo);
		return false;
	}
	fclose(f);

	key_tab = rd32(sfo + 0x08);
	data_tab = rd32(sfo + 0x0C);
	count = rd32(sfo + 0x10);

	for (i = 0; i < count && !found; i++)
	{
		const unsigned char* e = sfo + 0x14 + i * 16;
		uint32_t key_off = e[0] | (e[1] << 8);
		uint32_t data_len = rd32(e + 4);
		uint32_t data_off = rd32(e + 12);
		const char* key;

		if (key_tab + key_off >= (uint32_t)size) break;
		key = (const char*)sfo + key_tab + key_off;
		if (strcmp(key, wanted) != 0) continue;
		if (data_tab + data_off + data_len > (uint32_t)size) break;

		if (data_len >= out_len) data_len = (uint32_t)out_len - 1;
		memcpy(out, sfo + data_tab + data_off, data_len);
		out[data_len] = 0;
		found = true;
	}

	free(sfo);
	return found;
}

bool metaPbpTitle(const char* rom_path, char* out, size_t out_len)
{
	return hasExt(rom_path, ".pbp") && sfoLookup(rom_path, "TITLE", out, out_len);
}

bool metaPbpIcon(const char* rom_path, const char* dest)
{
	long off, size;
	unsigned char* buf;
	FILE* in;
	FILE* outf;
	bool ok = false;

	if (!hasExt(rom_path, ".pbp")) return false;
	if (!pbpSection(rom_path, 1, &off, &size) || size < 64) return false;

	buf = malloc((size_t)size);
	if (!buf) return false;

	in = fopen(rom_path, "rb");
	if (in && fseek(in, off, SEEK_SET) == 0
	    && fread(buf, 1, (size_t)size, in) == (size_t)size
	    && memcmp(buf + 1, "PNG", 3) == 0)
	{
		outf = fopen(dest, "wb");
		if (outf)
		{
			ok = fwrite(buf, 1, (size_t)size, outf) == (size_t)size;
			fclose(outf);
		}
	}
	if (in) fclose(in);
	free(buf);
	return ok;
}

// ---------------------------------------------------------------- bin/cue

// The first FILE line of a cue names the data track.
static bool cueFirstTrack(const char* cue_path, char* out, size_t out_len)
{
	char line[512];
	const char* slash;
	size_t dir_len;
	FILE* f = fopen(cue_path, "r");
	bool found = false;

	if (!f) return false;
	while (!found && fgets(line, sizeof(line), f))
	{
		char* p = line;
		char* end;
		while (*p == ' ' || *p == '\t') p++;
		if (strncasecmp(p, "FILE ", 5) != 0) continue;

		p += 5;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '"') { p++; end = strchr(p, '"'); }
		else { end = p; while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') end++; }
		if (!end) break;
		*end = 0;

		slash = strrchr(cue_path, '/');
		dir_len = slash ? (size_t)(slash - cue_path) : 0;
		found = snprintf(out, out_len, "%.*s/%s", (int)dir_len, cue_path, p) > 0;
	}
	fclose(f);
	return found;
}

// SYSTEM.CNF is plain text inside the data track, so a string scan finds the
// boot line without having to walk the ISO9660 filesystem.
static bool scanBootLine(const char* bin_path, char* out, size_t out_len)
{
	char* buf;
	size_t n, i;
	FILE* f = fopen(bin_path, "rb");
	bool found = false;

	if (!f) return false;
	buf = malloc(SCAN_BYTES + 1);
	if (!buf) { fclose(f); return false; }

	n = fread(buf, 1, SCAN_BYTES, f);
	fclose(f);
	buf[n] = 0;

	for (i = 0; i + 5 < n && !found; i++)
	{
		const char* p;
		char raw[SERIAL_LEN];
		size_t k = 0;

		if (memcmp(buf + i, "BOOT", 4) != 0) continue;
		p = buf + i + 4;
		while (*p == ' ' || *p == '=' || *p == '\t') p++;
		if (strncasecmp(p, "cdrom:", 6) == 0) p += 6;
		while (*p == '\\' || *p == '/') p++;

		while (*p && k < sizeof(raw) - 1 && *p != ';' && *p != '\r' && *p != '\n')
			raw[k++] = *p++;
		raw[k] = 0;
		found = normalise(raw, out, out_len);
	}

	free(buf);
	return found;
}

// ---------------------------------------------------------------- public

bool metaSerial(const char* rom_path, char* out, size_t out_len)
{
	char raw[SERIAL_LEN];
	char track[512];

	if (hasExt(rom_path, ".pbp"))
		return sfoLookup(rom_path, "DISC_ID", raw, sizeof(raw))
			&& normalise(raw, out, out_len);

	if (hasExt(rom_path, ".cue"))
		return cueFirstTrack(rom_path, track, sizeof(track))
			&& scanBootLine(track, out, out_len);

	if (hasExt(rom_path, ".bin") || hasExt(rom_path, ".iso") || hasExt(rom_path, ".img"))
		return scanBootLine(rom_path, out, out_len);

	// chd and the rest keep their data compressed: nothing to read here.
	return false;
}

bool metaIndexPath(const char* system, char* out, size_t out_len)
{
	if (!system || !*system) return false;
	return snprintf(out, out_len, "%s/%s.txt", INDEX_DIR, system) > 0;
}

bool metaNameForSerial(const char* system, const char* serial,
                       char* out, size_t out_len)
{
	char path[256], line[256];
	size_t n = strlen(serial);
	FILE* f;
	bool found = false;

	if (!metaIndexPath(system, path, sizeof(path))) return false;
	f = fopen(path, "r");

	if (!f) return false;
	while (!found && fgets(line, sizeof(line), f))
	{
		char* bar;
		if (strncasecmp(line, serial, n) != 0 || line[n] != '|') continue;
		bar = line + n + 1;
		bar[strcspn(bar, "\r\n")] = 0;
		if (!*bar) break;
		snprintf(out, out_len, "%s", bar);
		found = true;
	}
	fclose(f);
	return found;
}

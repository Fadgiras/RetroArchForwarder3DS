#include <stdio.h>
#include <string.h>

#include "idalloc.h"
#include "sha256.h"

uint64_t idTitleForUid(uint32_t uid)
{
	return 0x0004000000000000ULL | ((uint64_t)uid << 8);
}

uint32_t idUidForTitle(uint64_t tid)
{
	return (uint32_t)((tid >> 8) & 0xFFFFF);
}

bool idInRange(uint64_t tid)
{
	uint32_t uid = idUidForTitle(tid);
	return (tid >> 32) == 0x00040000ULL && (tid & 0xFF) == 0
		&& uid >= ID_UID_BASE && uid <= (ID_UID_BASE | ID_UID_MASK);
}

// Deriving the id from the ROM path means rebuilding the same game usually
// lands on the same id. "Usually": the registry is what makes it certain.
uint64_t idPreferredTitle(const char* rom_path)
{
	uint8_t h[32];
	sha256(rom_path, strlen(rom_path), h);
	return idTitleForUid(ID_UID_BASE | ((((uint32_t)h[0] << 8) | h[1]) & ID_UID_MASK));
}

uint64_t idPickFree(uint64_t start, id_taken_fn taken, void* ctx)
{
	uint32_t first = idUidForTitle(start) & ID_UID_MASK;
	uint32_t i;

	for (i = 0; i < ID_UID_COUNT; i++)
	{
		uint64_t tid = idTitleForUid(ID_UID_BASE | ((first + i) & ID_UID_MASK));
		if (!taken(tid, ctx)) return tid;
	}
	return 0;
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Parses one line. Returns false for anything malformed, which is skipped
// rather than trusted.
static bool parseLine(const char* line, const char* end, uint64_t* tid,
                      const char** path, size_t* path_len)
{
	uint64_t v = 0;
	int i;

	if (end - line < 18) return false;
	for (i = 0; i < 16; i++)
	{
		int d = hexval(line[i]);
		if (d < 0) return false;
		v = (v << 4) | (uint64_t)d;
	}
	if (line[16] != '|') return false;

	*tid = v;
	*path = line + 17;
	*path_len = (size_t)(end - *path);
	while (*path_len && ((*path)[*path_len - 1] == '\r' || (*path)[*path_len - 1] == ' '))
		(*path_len)--;
	return *path_len > 0;
}

typedef bool (*line_fn)(uint64_t tid, const char* path, size_t path_len, void* ctx);

static bool eachLine(const char* text, line_fn fn, void* ctx)
{
	const char* p = text;

	if (!text) return false;
	while (*p)
	{
		const char* end = strchr(p, '\n');
		uint64_t tid;
		const char* path;
		size_t len;

		if (!end) end = p + strlen(p);
		if (parseLine(p, end, &tid, &path, &len) && fn(tid, path, len, ctx))
			return true;
		p = *end ? end + 1 : end;
	}
	return false;
}

typedef struct { const char* rom; uint64_t tid; bool found; } find_ctx;

// The last line wins: a game moved to a new id is appended, not rewritten.
static bool matchPath(uint64_t tid, const char* path, size_t len, void* ctx)
{
	find_ctx* f = (find_ctx*)ctx;
	if (len == strlen(f->rom) && memcmp(path, f->rom, len) == 0 && idInRange(tid))
	{
		f->tid = tid;
		f->found = true;
	}
	return false;
}

bool idRegistryFind(const char* text, const char* rom_path, uint64_t* tid)
{
	find_ctx f;
	f.rom = rom_path;
	f.tid = 0;
	f.found = false;
	eachLine(text, matchPath, &f);
	if (!f.found) return false;
	*tid = f.tid;
	return true;
}

static bool matchTitle(uint64_t tid, const char* path, size_t len, void* ctx)
{
	(void)path; (void)len;
	return tid == *(uint64_t*)ctx;
}

bool idRegistryHasTitle(const char* text, uint64_t tid)
{
	return eachLine(text, matchTitle, &tid);
}

size_t idRegistryLine(char* out, size_t out_len, uint64_t tid, const char* rom_path)
{
	int n = snprintf(out, out_len, "%08lX%08lX|%s\n",
		(unsigned long)(tid >> 32), (unsigned long)(tid & 0xFFFFFFFF), rom_path);
	if (n < 0 || (size_t)n >= out_len) return 0;
	return (size_t)n;
}

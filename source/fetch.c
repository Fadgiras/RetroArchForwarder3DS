#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fetch.h"
#include "romfilter.h"

#define HOST "http://thumbnails.libretro.com"
#define THUMB_ROOT "sdmc:/retroarch/thumbnails"
#define CHUNK 0x4000
#define MAX_REDIRECTS 4

static const struct { const char* core; const char* system; } SYSTEMS[] = {
	{ "pcsx_rearmed",              "Sony - PlayStation" },
	{ "pcsx_rearmed_interpreter",  "Sony - PlayStation" },
	{ "snes9x",                    "Nintendo - Super Nintendo Entertainment System" },
	{ "snes9x2010",                "Nintendo - Super Nintendo Entertainment System" },
	{ "nestopia",                  "Nintendo - Nintendo Entertainment System" },
	{ "fceumm",                    "Nintendo - Nintendo Entertainment System" },
	{ "gambatte",                  "Nintendo - Game Boy Color" },
	{ "mgba",                      "Nintendo - Game Boy Advance" },
	{ "vba_next",                  "Nintendo - Game Boy Advance" },
	{ "genesis_plus_gx",           "Sega - Mega Drive - Genesis" },
	{ "picodrive",                 "Sega - Mega Drive - Genesis" },
	{ "mame2000",                  "MAME" },
};

const char* fetchSystemForCore(const char* core_path)
{
	char stem[128];
	const char* slash = strrchr(core_path, '/');
	const char* suffix;
	size_t n, i;

	slash = slash ? slash + 1 : core_path;
	suffix = strstr(slash, "_libretro.");
	n = suffix ? (size_t)(suffix - slash) : strlen(slash);
	if (n >= sizeof(stem)) return NULL;
	memcpy(stem, slash, n);
	stem[n] = 0;

	for (i = 0; i < sizeof(SYSTEMS) / sizeof(SYSTEMS[0]); i++)
		if (strcmp(stem, SYSTEMS[i].core) == 0)
			return SYSTEMS[i].system;
	return NULL;
}


// Game names contain spaces, apostrophes and parentheses.
static void urlEncode(const char* src, char* dst, size_t dst_len)
{
	static const char HEX[] = "0123456789ABCDEF";
	size_t out = 0;

	for (; *src && out + 4 < dst_len; src++)
	{
		unsigned char c = (unsigned char)*src;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
		    || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			dst[out++] = (char)c;
		else
		{
			dst[out++] = '%';
			dst[out++] = HEX[c >> 4];
			dst[out++] = HEX[c & 0xF];
		}
	}
	dst[out] = 0;
}

static bool fileExists(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

static void makeParents(const char* path)
{
	char buf[512];
	char* p;

	snprintf(buf, sizeof(buf), "%s", path);
	for (p = buf + strlen("sdmc:/"); *p; p++)
		if (*p == '/')
		{
			*p = 0;
			mkdir(buf, 0777);
			*p = '/';
		}
}

static bool download(const char* url, const char* dest)
{
	httpcContext ctx;
	char location[512];
	u32 status = 0, size = 0, read = 0;
	u8* buf;
	FILE* f;
	int hop;
	Result rc;

	for (hop = 0; hop <= MAX_REDIRECTS; hop++)
	{
		rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1);
		if (R_FAILED(rc)) return false;

		httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
		httpcAddRequestHeaderField(&ctx, "User-Agent", "fwdgen/1.0");
		httpcAddRequestHeaderField(&ctx, "Connection", "close");

		if (R_FAILED(httpcBeginRequest(&ctx))
		    || R_FAILED(httpcGetResponseStatusCode(&ctx, &status)))
		{
			httpcCloseContext(&ctx);
			return false;
		}

		if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
		{
			if (R_FAILED(httpcGetResponseHeader(&ctx, "Location", location, sizeof(location))))
			{
				httpcCloseContext(&ctx);
				return false;
			}
			httpcCloseContext(&ctx);
			url = location;
			continue;
		}
		break;
	}

	if (status != 200) { httpcCloseContext(&ctx); return false; }

	httpcGetDownloadSizeState(&ctx, NULL, &size);
	if (!size) size = 4 * 1024 * 1024;

	buf = malloc(size);
	if (!buf) { httpcCloseContext(&ctx); return false; }

	do
	{
		u32 got = 0;
		rc = httpcDownloadData(&ctx, buf + read, size - read, &got);
		read += got;
	} while (rc == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING && read < size);

	httpcCloseContext(&ctx);

	if (R_FAILED(rc) && rc != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) { free(buf); return false; }
	if (read < 16) { free(buf); return false; }

	makeParents(dest);
	f = fopen(dest, "wb");
	if (!f) { free(buf); return false; }
	fwrite(buf, 1, read, f);
	fclose(f);
	free(buf);
	return true;
}

bool fetchArtwork(const char* system, const char* game, const char* kind,
                  char* out_path, size_t out_len)
{
	char enc_system[256], enc_game[512], url[1024];

	snprintf(out_path, out_len, "%s/%s/%s/%s.png", THUMB_ROOT, system, kind, game);
	if (fileExists(out_path)) return true;
	if (!system) return false;

	urlEncode(system, enc_system, sizeof(enc_system));
	{
		char scrubbed[512];
		scrubThumbnailName(game, scrubbed, sizeof(scrubbed));
		urlEncode(scrubbed, enc_game, sizeof(enc_game));
	}
	snprintf(url, sizeof(url), "%s/%s/%s/%s.png", HOST, enc_system, kind, enc_game);

	return download(url, out_path) && fileExists(out_path);
}

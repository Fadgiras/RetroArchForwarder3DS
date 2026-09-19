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

// Which systems a core plays, spelled the way the thumbnail repository spells
// them. Every name here was checked against the server. A core appears once
// per system it covers, likeliest first, because the repositories are disjoint
// and a ROM does not say which machine it belongs to. Cores that carry their
// own game are left out: they have no repository to look in.
static const struct { const char* core; const char* system; } SYSTEMS[] = {
	{ "pcsx_rearmed",              "Sony - PlayStation" },
	{ "pcsx_rearmed_interpreter",  "Sony - PlayStation" },

	{ "snes9x",                    "Nintendo - Super Nintendo Entertainment System" },
	{ "snes9x2002",                "Nintendo - Super Nintendo Entertainment System" },
	{ "snes9x2005",                "Nintendo - Super Nintendo Entertainment System" },
	{ "snes9x2005_plus",           "Nintendo - Super Nintendo Entertainment System" },
	{ "snes9x2010",                "Nintendo - Super Nintendo Entertainment System" },
	{ "nestopia",                  "Nintendo - Nintendo Entertainment System" },
	{ "fceumm",                    "Nintendo - Nintendo Entertainment System" },
	{ "quicknes",                  "Nintendo - Nintendo Entertainment System" },
	{ "gambatte",                  "Nintendo - Game Boy Color" },
	{ "gambatte",                  "Nintendo - Game Boy" },
	{ "gearboy",                   "Nintendo - Game Boy Color" },
	{ "gearboy",                   "Nintendo - Game Boy" },
	{ "tgbdual",                   "Nintendo - Game Boy Color" },
	{ "tgbdual",                   "Nintendo - Game Boy" },
	{ "DoubleCherryGB",            "Nintendo - Game Boy Color" },
	{ "DoubleCherryGB",            "Nintendo - Game Boy" },
	{ "mgba",                      "Nintendo - Game Boy Advance" },
	{ "mgba",                      "Nintendo - Game Boy Color" },
	{ "mgba",                      "Nintendo - Game Boy" },
	{ "vba_next",                  "Nintendo - Game Boy Advance" },
	{ "gpsp",                      "Nintendo - Game Boy Advance" },
	{ "mednafen_vb",               "Nintendo - Virtual Boy" },
	{ "pokemini",                  "Nintendo - Pokemon Mini" },

	{ "genesis_plus_gx",           "Sega - Mega Drive - Genesis" },
	{ "genesis_plus_gx",           "Sega - Mega-CD - Sega CD" },
	{ "genesis_plus_gx",           "Sega - Master System - Mark III" },
	{ "genesis_plus_gx",           "Sega - Game Gear" },
	{ "genesis_plus_gx",           "Sega - SG-1000" },
	{ "genesis_plus_gx_wide",      "Sega - Mega Drive - Genesis" },
	{ "genesis_plus_gx_wide",      "Sega - Mega-CD - Sega CD" },
	{ "picodrive",                 "Sega - Mega Drive - Genesis" },
	{ "picodrive",                 "Sega - 32X" },
	{ "picodrive",                 "Sega - Mega-CD - Sega CD" },
	{ "clownmdemu",                "Sega - Mega Drive - Genesis" },
	{ "gearsystem",                "Sega - Master System - Mark III" },
	{ "gearsystem",                "Sega - Game Gear" },
	{ "gearsystem",                "Sega - SG-1000" },
	{ "smsplus",                   "Sega - Master System - Mark III" },
	{ "smsplus",                   "Sega - Game Gear" },

	{ "mednafen_pce_fast",         "NEC - PC Engine - TurboGrafx 16" },
	{ "mednafen_pce_fast",         "NEC - PC Engine CD - TurboGrafx-CD" },
	{ "geargrafx",                 "NEC - PC Engine - TurboGrafx 16" },
	{ "geargrafx",                 "NEC - PC Engine CD - TurboGrafx-CD" },
	{ "quasi88",                   "NEC - PC-88" },
	{ "np2kai",                    "NEC - PC-98" },
	{ "nekop2",                    "NEC - PC-98" },

	{ "mednafen_ngp",              "SNK - Neo Geo Pocket Color" },
	{ "mednafen_ngp",              "SNK - Neo Geo Pocket" },
	{ "race",                      "SNK - Neo Geo Pocket Color" },
	{ "race",                      "SNK - Neo Geo Pocket" },
	{ "mednafen_wswan",            "Bandai - WonderSwan Color" },
	{ "mednafen_wswan",            "Bandai - WonderSwan" },

	{ "stella2014",                "Atari - 2600" },
	{ "a5200",                     "Atari - 5200" },
	{ "prosystem",                 "Atari - 7800" },
	{ "atari800",                  "Atari - 8-bit Family" },
	{ "atari800",                  "Atari - 5200" },
	{ "handy",                     "Atari - Lynx" },

	{ "mame2000",                  "MAME" },
	{ "mame2003",                  "MAME" },
	{ "mame2003_plus",             "MAME" },
	{ "fbalpha2012",               "FBNeo - Arcade Games" },
	{ "fbalpha2012_cps1",          "FBNeo - Arcade Games" },
	{ "fbalpha2012_cps2",          "FBNeo - Arcade Games" },
	{ "fbalpha2012_cps3",          "FBNeo - Arcade Games" },
	{ "fbneo_cps12",               "FBNeo - Arcade Games" },
	{ "fbalpha2012_neogeo",        "SNK - Neo Geo" },
	{ "fbalpha2012_neogeo",        "FBNeo - Arcade Games" },
	{ "fbneo_neogeo",              "SNK - Neo Geo" },
	{ "fbneo_neogeo",              "FBNeo - Arcade Games" },
	{ "neocd",                     "SNK - Neo Geo CD" },

	{ "gearcoleco",                "Coleco - ColecoVision" },
	{ "freeintv",                  "Mattel - Intellivision" },
	{ "o2em",                      "Magnavox - Odyssey2" },
	{ "freechaf",                  "Fairchild - Channel F" },
	{ "vecx",                      "GCE - Vectrex" },
	{ "opera",                     "The 3DO Company - 3DO" },
	{ "potator",                   "Watara - Supervision" },
	{ "gw",                        "Handheld Electronic Game" },

	{ "bluemsx",                   "Microsoft - MSX" },
	{ "bluemsx",                   "Microsoft - MSX2" },
	{ "fmsx",                      "Microsoft - MSX" },
	{ "fmsx",                      "Microsoft - MSX2" },
	{ "cap32",                     "Amstrad - CPC" },
	{ "crocods",                   "Amstrad - CPC" },
	{ "fuse",                      "Sinclair - ZX Spectrum" },
	{ "81",                        "Sinclair - ZX 81" },
	{ "frodo",                     "Commodore - 64" },
	{ "vice_x64",                  "Commodore - 64" },
	{ "vice_x64sc",                "Commodore - 64" },
	{ "vice_xscpu64",              "Commodore - 64" },
	{ "vice_xvic",                 "Commodore - VIC-20" },
	{ "vice_xplus4",               "Commodore - Plus-4" },
	{ "vice_xpet",                 "Commodore - PET" },
	{ "theodore",                  "Thomson - MOTO" },
	{ "x1",                        "Sharp - X1" },
	{ "dosbox_svn",                "DOS" },
	{ "scummvm",                   "ScummVM" },
};

const char* fetchSystemForCore(const char* core_path, int index)
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

	// A core listed several times covers several systems, in table order.
	for (i = 0; i < sizeof(SYSTEMS) / sizeof(SYSTEMS[0]); i++)
		if (strcmp(stem, SYSTEMS[i].core) == 0 && index-- == 0)
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

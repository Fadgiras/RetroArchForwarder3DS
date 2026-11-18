// RetroArch Forwarder Generator - on-console forwarder builder.
//
// Starts from the template embedded in romfs, rewrites the ROM path, the core
// path, the SMDH titles, the icon, the banner and the title id, recomputes the
// whole hash chain, then installs the resulting CIA.
//
// Every patching routine has a PC-side counterpart under host/, whose output
// was compared byte for byte against a Python reference before shipping.

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "browser.h"
#include "ui.h"
#include "cia_patch.h"
#include "coresel.h"
#include "icongen.h"
#include "bannergen.h"
#include "fetch.h"
#include "sha256.h"
#include "metadata.h"
#include "namepick.h"
#include "idalloc.h"

#define OUT_DIR "sdmc:/forwarders"
#define TEMPLATE "romfs:/template.cia"
#define PBP_ICON_TMP "sdmc:/forwarders/.pbp_icon.png"
#define REGISTRY "sdmc:/forwarders/registry.txt"
#define REGISTRY_MAX (256 * 1024)

static u8* s_cia;
static size_t s_ciaLen;

// What is installed on the SD card, read once before an id is chosen.
static u64* s_installed;
static u32 s_installedCount;
static bool s_installedKnown;

static char* s_registry;
static char s_productCode[17];

static bool isInstalled(u64 tid)
{
	u32 i;
	for (i = 0; i < s_installedCount; i++)
		if (s_installed[i] == tid) return true;
	return false;
}

static bool loadInstalled(void)
{
	u32 count = 0, read = 0;

	if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count))) return false;
	s_installed = malloc((count ? count : 1) * sizeof(u64));
	if (!s_installed) return false;
	if (R_FAILED(AM_GetTitleList(&read, MEDIATYPE_SD, count, s_installed))) return false;
	s_installedCount = read;
	return true;
}

static void loadRegistry(void)
{
	FILE* f = fopen(REGISTRY, "rb");
	long len;

	s_registry = NULL;
	if (f)
	{
		fseek(f, 0, SEEK_END);
		len = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (len > 0 && len < REGISTRY_MAX && (s_registry = malloc((size_t)len + 1)))
		{
			size_t got = fread(s_registry, 1, (size_t)len, f);
			s_registry[got] = 0;
		}
		fclose(f);
	}
	if (!s_registry && (s_registry = malloc(1))) s_registry[0] = 0;
}

static bool registryAdd(u64 tid, const char* rom_path)
{
	char line[PATH_LEN + 32];
	size_t n;
	FILE* f;
	u64 known;

	if (idRegistryFind(s_registry, rom_path, &known) && known == tid) return true;
	n = idRegistryLine(line, sizeof(line), tid, rom_path);
	if (!n) return false;
	f = fopen(REGISTRY, "ab");
	if (!f) return false;
	fwrite(line, 1, n, f);
	fclose(f);
	return true;
}

// Whether an installed title was built by this app: it carries the template's
// product code. Anything else in the range, NDS forwarders included, has its
// own and is left alone.
static bool isOurForwarder(u64 tid)
{
	char code[16 + 1];

	memset(code, 0, sizeof(code));
	if (!s_productCode[0]) return false;
	if (R_FAILED(AM_GetTitleProductCode(MEDIATYPE_SD, tid, code))) return false;
	return strncmp(code, s_productCode, 16) == 0;
}

static bool idTaken(u64 tid, void* ctx)
{
	(void)ctx;
	return isInstalled(tid) || idRegistryHasTitle(s_registry, tid);
}

// The id a ROM gets, and whether whatever sits there now is ours to replace.
//
// 1. The registry lists the ROM, and that id is free or still holds our
//    forwarder: an update of our own title. Should another title have taken
//    the id since, the game simply moves on, as in 3.
// 2. The hashed id holds a forwarder from before the registry: adopted.
// 3. Otherwise the first id from the hashed one on that nobody uses, neither
//    an installed title nor another game in the registry.
//
// Nothing that was not built by this app is ever replaced.
static bool chooseTitleId(const char* rom_path, u64* tid, bool* owned, bool* moved)
{
	u64 preferred = idPreferredTitle(rom_path);

	*owned = false;
	*moved = false;

	if (idRegistryFind(s_registry, rom_path, tid)
	    && (!isInstalled(*tid) || isOurForwarder(*tid)))
	{
		*owned = true;
		return true;
	}

	if (isInstalled(preferred) && !idRegistryHasTitle(s_registry, preferred)
	    && isOurForwarder(preferred))
	{
		*tid = preferred;
		*owned = true;
		return true;
	}

	*tid = idPickFree(preferred, idTaken, NULL);
	if (!*tid) return false;
	*moved = (*tid != preferred);
	return true;
}

// Settles on the name the artwork is filed under, once per game, because the
// last resort asks the user and doing that twice would be tiresome.
//
// The disc's own serial comes first: it is the only thing that identifies a
// file named "FF7.pbp". Then the file name, confirmed by an actual fetch
// rather than assumed. Then the user, with a sensible starting guess.
// A core can cover several systems, so each step is tried against all of them,
// and the one that answered is handed back: the banner must not go looking in
// a different place than the icon did.
static bool resolveArtName(const char* rom_path, const char* core_path,
                           const char** out_system, char* out, size_t out_len)
{
	char serial[SERIAL_LEN], stem[NAME_LEN], title[NAME_LEN], probe[PATH_LEN];
	const char* system;
	int i;

	*out_system = NULL;
	if (!fetchSystemForCore(core_path, 0)) return false;

	if (metaSerial(rom_path, serial, sizeof(serial)))
		for (i = 0; (system = fetchSystemForCore(core_path, i)) != NULL; i++)
			if (metaNameForSerial(system, serial, out, out_len))
			{
				printf("  serial   : %s\n", serial);
				*out_system = system;
				return true;
			}

	artStem(rom_path, stem, sizeof(stem));
	for (i = 0; (system = fetchSystemForCore(core_path, i)) != NULL; i++)
		if (fetchArtwork(system, stem, "Named_Boxarts", probe, sizeof(probe)))
		{
			snprintf(out, out_len, "%s", stem);
			*out_system = system;
			return true;
		}

	// A pbp states its own title, a far better starting point for the search
	// than a file called "FF7". Only worth asking where an index exists:
	// without one the keyboard would filter an empty list.
	for (i = 0; (system = fetchSystemForCore(core_path, i)) != NULL; i++)
		if (metaHasIndex(system))
		{
			printf("  name     : not recognised, asking\n");
			*out_system = system;
			return namePick(system,
			                metaPbpTitle(rom_path, title, sizeof(title)) ? title : stem,
			                out, out_len);
		}

	printf("  name     : not recognised, no index for this system\n");
	return false;
}

// Artwork comes from the libretro repository when the network answers, and
// from the SD card otherwise.
static bool findArt(const char* rom_path, const char* system,
                    const char* art_name, const char* kind,
                    char* out, size_t out_len)
{
	if (system && art_name && *art_name
	    && fetchArtwork(system, art_name, kind, out, out_len))
		return true;

	return iconFindArtwork(rom_path, kind, out, out_len);
}

static void displayName(const char* rom_path, char* out, size_t len)
{
	const char* slash = strrchr(rom_path, '/');
	const char* dot;
	size_t n;

	slash = slash ? slash + 1 : rom_path;
	dot = strrchr(slash, '.');
	n = dot ? (size_t)(dot - slash) : strlen(slash);
	if (n >= len) n = len - 1;
	memcpy(out, slash, n);
	out[n] = 0;
}

static bool loadTemplate(void)
{
	FILE* f = fopen(TEMPLATE, "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	s_ciaLen = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	s_cia = malloc(s_ciaLen);
	if (!s_cia || fread(s_cia, 1, s_ciaLen, f) != s_ciaLen)
	{
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

// The CIA is written to the SD card before being installed: should the install
// fail, the file is still there to be inspected on a PC.
static bool writeToSd(const char* out_path)
{
	FILE* f;
	mkdir(OUT_DIR, 0777);
	f = fopen(out_path, "wb");
	if (!f) return false;
	if (fwrite(s_cia, 1, s_ciaLen, f) != s_ciaLen)
	{
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

// Returned when the id holds a title this app did not build.
#define INSTALL_REFUSED ((Result)0xC8A04BEF)

static Result installCia(u64 tid, bool owned)
{
	Handle handle;
	Result rc;
	u32 written;
	size_t offset = 0;

	// Rebuilding the same game should be a clean update rather than a refusal,
	// but only our own forwarder is ever removed. Anything else at this id
	// means the choice above went wrong: stop rather than delete it.
	if (isInstalled(tid))
	{
		if (!owned || !idInRange(tid) || !isOurForwarder(tid)) return INSTALL_REFUSED;
		AM_DeleteTitle(MEDIATYPE_SD, tid);
		AM_DeleteTicket(tid);
	}

	rc = AM_StartCiaInstall(MEDIATYPE_SD, &handle);
	if (R_FAILED(rc)) return rc;

	while (offset < s_ciaLen)
	{
		size_t chunk = s_ciaLen - offset;
		if (chunk > 0x8000) chunk = 0x8000;
		rc = FSFILE_Write(handle, &written, offset, s_cia + offset, chunk, 0);
		if (R_FAILED(rc))
		{
			AM_CancelCIAInstall(handle);
			return rc;
		}
		offset += written;
		printf("\r  installing : %3d%%", (int)(offset * 100 / s_ciaLen));
	}
	printf("\n");

	return AM_FinishCiaInstall(handle);
}

static void waitForExit(void)
{
	printf("\n  Press START to exit.\n");
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;
		uiFlush();
	}
}

int main(void)
{
	char rel_path[PATH_LEN];
	char rom_path[PATH_LEN + 8];
	char core_path[PATH_LEN + 8];
	char name[NAME_LEN];
	char art_name[NAME_LEN];
	const char* art_system = NULL;
	char out_path[PATH_LEN];
	cia_t cia;
	u64 tid;
	bool owned, moved;
	Result rc;
	int err;

	gfxInitDefault();
	uiInitScreens();
	romfsInit();
	amInit();
	httpcInit(0);

	if (!browserRun(rel_path, sizeof(rel_path), false))
		goto done;

	// The core is picked automatically when the card holds only one.
	if (!coreSelect(core_path, sizeof(core_path)))
		goto done;

	snprintf(rom_path, sizeof(rom_path), "sdmc:%s", rel_path);
	displayName(rom_path, name, sizeof(name));

	// The bottom screen still shows the browser help: clear it too.
	uiSelectBottom();
	uiClear();
	uiSelectTop();
	uiClear();
	printf(UI_TITLE "%s" UI_OFF "\n\n", name);
	printf("  rom      : %.40s\n", rom_path);
	printf("  core     : %.40s\n", core_path);

	mkdir(OUT_DIR, 0777);

	if (!loadTemplate())
	{
		printf("  " UI_ERR "Template missing from romfs." UI_OFF "\n");
		goto wait;
	}

	if ((err = cia_open(&cia, s_cia, s_ciaLen)) != CIA_OK) { printf("  cia_open: %d\n", err); goto wait; }

	// The template's product code is what tells our older forwarders apart.
	memcpy(s_productCode, s_cia + cia.ncch + 0x150, 16);
	s_productCode[16] = 0;

	// Without the list of installed titles a free id cannot be told from a
	// taken one: better to stop than to guess.
	s_installedKnown = loadInstalled();
	if (!s_installedKnown)
	{
		printf("  " UI_ERR "Could not list installed titles." UI_OFF "\n");
		goto wait;
	}
	loadRegistry();

	if (!chooseTitleId(rom_path, &tid, &owned, &moved))
	{
		printf("  " UI_ERR "No free title id left." UI_OFF "\n");
		goto wait;
	}
	printf("  title id : %016llX\n", tid);
	if (moved)
		printf("  " UI_WARN "id taken by another title, moved to a free one" UI_OFF "\n");
	else if (owned && isInstalled(tid))
		printf("  " UI_DIM "updating this game's forwarder" UI_OFF "\n");
	printf("\n");
	if ((err = cia_set_rom_path(&cia, rom_path)) != CIA_OK) { printf("  rom path: %d (too long?)\n", err); goto wait; }
	if ((err = cia_set_core_path(&cia, core_path)) != CIA_OK) { printf("  core path: %d\n", err); goto wait; }
	if ((err = cia_set_titles(&cia, name, "RetroArch forwarder")) != CIA_OK) { printf("  titles: %d\n", err); goto wait; }

	// One name for both the icon and the banner: settle it before either.
	if (!resolveArtName(rom_path, core_path, &art_system, art_name, sizeof(art_name)))
		art_name[0] = 0;
	else
		printf("  artwork  : %.40s\n", art_name);

	// Box art when one can be found, downloaded or already on the card.
	// Otherwise the template icon is kept.
	{
		char art[PATH_LEN];
		uint8_t* smdh = cia_icon_data(&cia);
		const char* how = "none found, template icon kept";

		if (smdh && findArt(rom_path, art_system, art_name, "Named_Boxarts", art, sizeof(art))
		    && iconApply(smdh, art))
			how = "box art applied";
		// A pbp carries its own icon, which keeps artwork working offline.
		else if (smdh && metaPbpIcon(rom_path, PBP_ICON_TMP))
		{
			if (iconApply(smdh, PBP_ICON_TMP)) how = "taken from the pbp";
			remove(PBP_ICON_TMP);
		}

		printf("  icon     : %s\n", how);
	}

	// Banner: the title screen when the repository has one, box art otherwise.
	{
		char art[PATH_LEN];
		size_t reserved = 0;
		uint8_t* bnr = cia_banner_data(&cia, &reserved);
		bool found = findArt(rom_path, art_system, art_name, "Named_Titles", art, sizeof(art))
			|| findArt(rom_path, art_system, art_name, "Named_Boxarts", art, sizeof(art));

		if (bnr && found)
		{
			uint8_t* rebuilt = malloc(reserved);
			size_t n = rebuilt ? bannerReplaceImage(bnr, reserved, art, rebuilt, reserved) : 0;
			printf("  banner   : %s\n",
				(n && cia_set_banner(&cia, rebuilt, n) == CIA_OK)
					? "applied" : "failed, template banner kept");
			free(rebuilt);
		}
		else
			printf("  banner   : template banner kept\n");
	}
	cia_set_title_id(&cia, tid);

	// The HOME menu caches icons and only reloads them when the title version
	// changes, so reinstalling at the same version would keep the old one.
	{
		AM_TitleEntry info;
		u64 id = tid;
		u16 next = R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &id, &info))
			? (u16)(info.version + 1) : 1024;
		cia_set_version(&cia, next);
		printf("  version  : %u\n", next);
	}
	cia_set_process_name(&cia, name);
	cia_reseal(&cia);
	printf("  CIA built.\n");

	snprintf(out_path, sizeof(out_path), "%s/%08lX.cia", OUT_DIR, (u32)(tid & 0xFFFFFFFF));
	if (writeToSd(out_path))
		printf("  saved to : %s\n", out_path);
	else
		printf("  could not save to SD (installing anyway)\n");

	rc = installCia(tid, owned);
	if (R_SUCCEEDED(rc))
	{
		printf("\n  " UI_OK "Installed." UI_OFF " The game is on the HOME menu.\n");
		if (!registryAdd(tid, rom_path))
			printf("  " UI_WARN "Could not update %s" UI_OFF "\n", REGISTRY);
	}
	else if (rc == INSTALL_REFUSED)
		printf("\n  " UI_ERR "Refused: %016llX belongs to another title." UI_OFF "\n", tid);
	else
		printf("\n  " UI_ERR "Install failed: 0x%08lX" UI_OFF "\n", rc);

wait:
	waitForExit();
done:
	free(s_cia);
	free(s_installed);
	free(s_registry);
	httpcExit();
	amExit();
	romfsExit();
	gfxExit();
	return 0;
}

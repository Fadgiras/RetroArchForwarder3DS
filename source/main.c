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

#define OUT_DIR "sdmc:/forwarders"
#define TEMPLATE "romfs:/template.cia"
#define PBP_ICON_TMP "sdmc:/forwarders/.pbp_icon.png"

// Title id range reserved for forwarders. Nothing outside it is ever removed.
#define UID_BASE 0xFF000
#define UID_MASK 0x00FFF

static u8* s_cia;
static size_t s_ciaLen;

// Deriving the id from the ROM path means two games never collide, and
// rebuilding the same game lands on the same id: a clean update, not a
// duplicate entry on the HOME menu.
static u64 titleIdFor(const char* rom_path)
{
	u8 h[32];
	u32 uid;
	sha256(rom_path, strlen(rom_path), h);
	uid = UID_BASE | ((((u32)h[0] << 8) | h[1]) & UID_MASK);
	return 0x0004000000000000ULL | ((u64)uid << 8);
}

// Settles on the name the artwork is filed under, once per game, because the
// last resort asks the user and doing that twice would be tiresome.
//
// The disc's own serial comes first: it is the only thing that identifies a
// file named "FF7.pbp". Then the file name, confirmed by an actual fetch
// rather than assumed. Then the user, with a sensible starting guess.
static bool resolveArtName(const char* rom_path, const char* core_path,
                           char* out, size_t out_len)
{
	const char* system = fetchSystemForCore(core_path);
	char serial[SERIAL_LEN], stem[NAME_LEN], title[NAME_LEN], probe[PATH_LEN];

	if (!system) return false;

	if (metaSerial(rom_path, serial, sizeof(serial))
	    && metaNameForSerial(system, serial, out, out_len))
	{
		printf("  serial   : %s\n", serial);
		return true;
	}

	artStem(rom_path, stem, sizeof(stem));
	if (fetchArtwork(system, stem, "Named_Boxarts", probe, sizeof(probe)))
	{
		snprintf(out, out_len, "%s", stem);
		return true;
	}

	// Nothing to filter without an index, so keep the keyboard shut and let
	// the template artwork stand.
	if (!metaHasIndex(system))
	{
		printf("  name     : not recognised, no index for %s\n", system);
		return false;
	}

	// A pbp states its own title, a far better starting point for the search
	// than a file called "FF7".
	printf("  name     : not recognised, asking\n");
	return namePick(system,
	                metaPbpTitle(rom_path, title, sizeof(title)) ? title : stem,
	                out, out_len);
}

// Artwork comes from the libretro repository when the network answers, and
// from the SD card otherwise.
static bool findArt(const char* rom_path, const char* core_path,
                    const char* art_name, const char* kind,
                    char* out, size_t out_len)
{
	const char* system = fetchSystemForCore(core_path);

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

static Result installCia(u64 tid)
{
	Handle handle;
	Result rc;
	u32 written;
	size_t offset = 0;

	// Rebuilding the same game should be a clean update rather than a refusal.
	// The safeguard: only titles inside the forwarder range are ever removed.
	{
		u32 uid = (u32)((tid >> 8) & 0xFFFFF);
		if (uid >= UID_BASE && uid <= (UID_BASE | UID_MASK))
		{
			AM_DeleteTitle(MEDIATYPE_SD, tid);
			AM_DeleteTicket(tid);
		}
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
	char out_path[PATH_LEN];
	cia_t cia;
	u64 tid;
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
	tid = titleIdFor(rom_path);

	// The bottom screen still shows the browser help: clear it too.
	uiSelectBottom();
	uiClear();
	uiSelectTop();
	uiClear();
	printf(UI_TITLE "%s" UI_OFF "\n\n", name);
	printf("  rom      : %.40s\n", rom_path);
	printf("  core     : %.40s\n", core_path);
	printf("  title id : %016llX\n\n", tid);

	mkdir(OUT_DIR, 0777);

	if (!loadTemplate())
	{
		printf("  " UI_ERR "Template missing from romfs." UI_OFF "\n");
		goto wait;
	}

	if ((err = cia_open(&cia, s_cia, s_ciaLen)) != CIA_OK) { printf("  cia_open: %d\n", err); goto wait; }
	if ((err = cia_set_rom_path(&cia, rom_path)) != CIA_OK) { printf("  rom path: %d (too long?)\n", err); goto wait; }
	if ((err = cia_set_core_path(&cia, core_path)) != CIA_OK) { printf("  core path: %d\n", err); goto wait; }
	if ((err = cia_set_titles(&cia, name, "RetroArch forwarder")) != CIA_OK) { printf("  titles: %d\n", err); goto wait; }

	// One name for both the icon and the banner: settle it before either.
	if (!resolveArtName(rom_path, core_path, art_name, sizeof(art_name)))
		art_name[0] = 0;
	else
		printf("  artwork  : %.40s\n", art_name);

	// Box art when one can be found, downloaded or already on the card.
	// Otherwise the template icon is kept.
	{
		char art[PATH_LEN];
		uint8_t* smdh = cia_icon_data(&cia);
		const char* how = "none found, template icon kept";

		if (smdh && findArt(rom_path, core_path, art_name, "Named_Boxarts", art, sizeof(art))
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
		bool found = findArt(rom_path, core_path, art_name, "Named_Titles", art, sizeof(art))
			|| findArt(rom_path, core_path, art_name, "Named_Boxarts", art, sizeof(art));

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

	rc = installCia(tid);
	if (R_SUCCEEDED(rc))
		printf("\n  " UI_OK "Installed." UI_OFF " The game is on the HOME menu.\n");
	else
		printf("\n  " UI_ERR "Install failed: 0x%08lX" UI_OFF "\n", rc);

wait:
	waitForExit();
done:
	free(s_cia);
	httpcExit();
	amExit();
	romfsExit();
	gfxExit();
	return 0;
}

#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "coresel.h"
#include "romfilter.h"
#include "ui.h"

// A full RetroArch 3dsx release drops over a hundred cores in one directory.
#define MAX_CORES 256
#define CORE_PATH_LEN 320
#define PAGE 24
#define COLS 49

// Usual places for a .3dsx core.
static const char* const SEARCH_DIRS[] = { "/3ds", "/retroarch/cores" };

static char s_cores[MAX_CORES][CORE_PATH_LEN];
static int s_count;

static void scanDir(const char* dir)
{
	char full[CORE_PATH_LEN];
	struct dirent* ent;
	DIR* d;

	snprintf(full, sizeof(full), "sdmc:%s", dir);
	d = opendir(full);
	if (!d) return;

	while ((ent = readdir(d)) && s_count < MAX_CORES)
	{
		if (ent->d_type == DT_DIR) continue;
		if (!isCoreFile(ent->d_name)) continue;
		snprintf(s_cores[s_count], CORE_PATH_LEN, "sdmc:%s/%s", dir, ent->d_name);
		s_count++;
	}
	closedir(d);
}

static const char* baseName(const char* path)
{
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void draw(int sel, int top)
{
	int i;

	uiSelectTop();
	uiClear();
	printf(UI_TITLE "Step 2/2: which core?" UI_OFF "\n\n");
	for (i = top; i < s_count && i < top + PAGE; i++)
		printf("%s%-*.*s%s\n",
			i == sel ? UI_SEL : "", COLS, COLS, baseName(s_cores[i]),
			i == sel ? UI_OFF : "");

	uiSelectBottom();
	uiClear();
	printf(UI_TITLE "Core" UI_OFF "\n\n");
	if (s_count) printf("%.39s\n", baseName(s_cores[sel]));
	printf("\n" UI_DIM "%d of %d" UI_OFF "\n", sel + 1, s_count);
	printf("\x1b[18;0H" UI_TITLE "Controls" UI_OFF "\n\n");
	printf("  A      select\n");
	printf("  L R    jump a page\n");
	printf("  B      cancel\n");
	uiSelectTop();
}

static void waitStart(void)
{
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;
		uiFlush();
	}
}

bool coreSelect(char* out_path, size_t out_len)
{
	size_t i;
	int sel = 0, top = 0;

	s_count = 0;
	for (i = 0; i < sizeof(SEARCH_DIRS) / sizeof(SEARCH_DIRS[0]); i++)
		scanDir(SEARCH_DIRS[i]);

	if (s_count == 0)
	{
		uiSelectTop();
		uiClear();
		printf(UI_ERR "No .3dsx core found." UI_OFF "\n\n");
		printf("  The forwarder needs the core as a .3dsx,\n");
		printf("  not as a CIA. Grab RetroArch_3dsx.7z from\n");
		printf("  the libretro buildbot and drop, say,\n");
		printf("  pcsx_rearmed_libretro.3dsx into\n");
		printf("  sdmc:/retroarch/cores/\n");
		printf("\n  Press START to exit.\n");
		waitStart();
		return false;
	}

	// Only one core on the card: no point asking.
	if (s_count == 1)
	{
		strncpy(out_path, s_cores[0], out_len - 1);
		out_path[out_len - 1] = 0;
		return true;
	}

	draw(sel, top);
	while (aptMainLoop())
	{
		u32 down;
		hidScanInput();
		down = hidKeysDown();

		if (down & KEY_B) return false;

		if (down & (KEY_DOWN | KEY_UP | KEY_L | KEY_R))
		{
			if (down & KEY_DOWN) sel++;
			if (down & KEY_UP) sel--;
			if (down & KEY_R) { sel += PAGE; if (sel >= s_count) sel = s_count - 1; }
			if (down & KEY_L) { sel -= PAGE; if (sel < 0) sel = 0; }
			if (sel < 0) sel = s_count - 1;
			if (sel >= s_count) sel = 0;
			if (sel < top) top = sel;
			if (sel >= top + PAGE) top = sel - PAGE + 1;
			draw(sel, top);
		}
		else if (down & KEY_A)
		{
			strncpy(out_path, s_cores[sel], out_len - 1);
			out_path[out_len - 1] = 0;
			return true;
		}

		uiFlush();
	}
	return false;
}

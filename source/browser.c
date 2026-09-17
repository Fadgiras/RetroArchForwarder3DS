#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "romfilter.h"
#include "ui.h"

#define MAX_ENTRIES 512
#define PAGE 24
#define COLS 49
#define BOT_COLS 39
#define CUE_MAX 65536

typedef struct
{
	char name[NAME_LEN];
	bool is_dir;
} entry_t;

static entry_t s_entries[MAX_ENTRIES];
static int s_count;
static bool s_pickCore;

static int compare(const void* a, const void* b)
{
	const entry_t* x = (const entry_t*)a;
	const entry_t* y = (const entry_t*)b;
	if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

// Reads a disc descriptor and drops the raw tracks it lists. Without this a
// multi-track game floods the listing and invites the wrong pick: opening the
// track instead of the .cue loses the audio tracks.
static void applyDescriptor(const char* dir_path, const char* descriptor, bool* hidden)
{
	char full[PATH_LEN + NAME_LEN + 8];
	char* text;
	size_t len;
	FILE* f;
	int i, written;

	written = snprintf(full, sizeof(full), "sdmc:%s%s%s", dir_path,
		(dir_path[strlen(dir_path) - 1] == '/') ? "" : "/", descriptor);

	// Truncated path: give up rather than open something else.
	if (written < 0 || (size_t)written >= sizeof(full)) return;

	f = fopen(full, "rb");
	if (!f) return;

	text = malloc(CUE_MAX + 1);
	if (!text) { fclose(f); return; }
	len = fread(text, 1, CUE_MAX, f);
	text[len] = 0;
	fclose(f);

	for (i = 0; i < s_count; i++)
		if (!hidden[i] && !s_entries[i].is_dir && isRawTrack(s_entries[i].name))
			if (cueReferences(text, s_entries[i].name))
				hidden[i] = true;

	free(text);
}

static void scan(const char* path)
{
	static bool hidden[MAX_ENTRIES];
	char full[PATH_LEN];
	struct dirent* ent;
	DIR* dir;
	int i, kept = 0;

	s_count = 0;
	memset(hidden, 0, sizeof(hidden));

	snprintf(full, sizeof(full), "sdmc:%s", path);
	dir = opendir(full);
	if (!dir) return;

	while ((ent = readdir(dir)) && s_count < MAX_ENTRIES)
	{
		bool is_dir = ent->d_type == DT_DIR;
		if (ent->d_name[0] == '.') continue;
		if (!is_dir && !(s_pickCore ? is3dsxFile(ent->d_name) : isRomFile(ent->d_name))) continue;
		strncpy(s_entries[s_count].name, ent->d_name, NAME_LEN - 1);
		s_entries[s_count].name[NAME_LEN - 1] = 0;
		s_entries[s_count].is_dir = is_dir;
		s_count++;
	}
	closedir(dir);

	if (!s_pickCore)
		for (i = 0; i < s_count; i++)
			if (!s_entries[i].is_dir && isDescriptor(s_entries[i].name))
				applyDescriptor(path, s_entries[i].name, hidden);

	// Fallback when the descriptor could not be read: same stem, same game.
	for (i = 0; i < s_count; i++)
	{
		int j;
		if (hidden[i] || s_entries[i].is_dir || !isRawTrack(s_entries[i].name)) continue;
		for (j = 0; j < s_count; j++)
			if (!s_entries[j].is_dir && isDescriptor(s_entries[j].name)
			    && sameStem(s_entries[i].name, s_entries[j].name))
				hidden[i] = true;
	}

	for (i = 0; i < s_count; i++)
		if (!hidden[i]) s_entries[kept++] = s_entries[i];
	s_count = kept;

	qsort(s_entries, s_count, sizeof(entry_t), compare);
}

// A long path reads better from its tail: the current folder is what matters,
// not the root.
static void drawPath(const char* path, int width)
{
	int len = (int)strlen(path);
	if (len <= width)
		printf(UI_DIM "sdmc:%s" UI_OFF "\n", path);
	else
		printf(UI_DIM "...%s" UI_OFF "\n", path + len - width + 3);
}

static void drawList(const char* path, int sel, int top)
{
	int i;

	uiSelectTop();
	uiClear();
	printf(UI_TITLE "%s" UI_OFF "\n",
		s_pickCore ? "Step 2/2: pick the core" : "Step 1/2: pick the ROM");
	drawPath(path, COLS);

	if (!s_count)
		printf("\n  " UI_DIM "%s" UI_OFF "\n",
			s_pickCore ? "(no .3dsx core here)" : "(no recognised ROM here)");

	for (i = top; i < s_count && i < top + PAGE; i++)
	{
		if (i == sel)
			printf(UI_SEL "%-*.*s" UI_OFF "\n", COLS, COLS, s_entries[i].name);
		else if (s_entries[i].is_dir)
			printf(UI_DIR "%-*.*s" UI_OFF "\n", COLS, COLS, s_entries[i].name);
		else
			printf("%-*.*s\n", COLS, COLS, s_entries[i].name);
	}

	// Position indicator, only when the listing does not fit on one screen.
	if (s_count > PAGE)
		printf("\x1b[29;0H" UI_DIM "%d-%d of %d" UI_OFF,
			top + 1, (top + PAGE < s_count) ? top + PAGE : s_count, s_count);
}

static void drawDetails(int sel)
{
	uiSelectBottom();
	uiClear();

	printf(UI_TITLE "Selection" UI_OFF "\n\n");

	if (s_count)
	{
		// Full name across several lines: the top listing truncates it.
		const char* name = s_entries[sel].name;
		int len = (int)strlen(name), off;
		for (off = 0; off < len && off < BOT_COLS * 5; off += BOT_COLS)
			printf("%.*s\n", BOT_COLS, name + off);

		printf("\n" UI_DIM "%s   %d of %d" UI_OFF "\n",
			s_entries[sel].is_dir ? "folder" : "file", sel + 1, s_count);
	}
	else
		printf(UI_DIM "nothing to pick in this folder" UI_OFF "\n");

	printf("\x1b[18;0H" UI_TITLE "Controls" UI_OFF "\n\n");
	printf("  A      %s\n",
		(s_count && s_entries[sel].is_dir) ? "open folder" : "select this file");
	printf("  B      go up\n");
	printf("  L / R  previous / next page\n");
	printf("  Y      back to root\n");
	printf("  START  quit\n");

	uiSelectTop();
}

static void draw(const char* path, int sel, int top)
{
	drawList(path, sel, top);
	drawDetails(sel);
}

static void clampView(int* sel, int* top)
{
	if (s_count == 0) { *sel = 0; *top = 0; return; }
	if (*sel < 0) *sel = s_count - 1;
	if (*sel >= s_count) *sel = 0;
	if (*sel < *top) *top = *sel;
	if (*sel >= *top + PAGE) *top = *sel - PAGE + 1;
	if (*top < 0) *top = 0;
}

bool browserRun(char* out_path, size_t out_len, bool pickCore)
{
	char path[PATH_LEN] = "/";
	int sel = 0, top = 0;

	s_pickCore = pickCore;
	scan(path);
	draw(path, sel, top);

	while (aptMainLoop())
	{
		u32 down;
		hidScanInput();
		down = hidKeysDown();

		if (down & KEY_START) return false;

		if (down & (KEY_DOWN | KEY_UP | KEY_L | KEY_R))
		{
			if (down & KEY_DOWN) sel++;
			if (down & KEY_UP) sel--;
			// Paging stops at both ends rather than wrapping around.
			if (down & KEY_R) { sel += PAGE; if (sel >= s_count) sel = s_count ? s_count - 1 : 0; }
			if (down & KEY_L) { sel -= PAGE; if (sel < 0) sel = 0; }
			clampView(&sel, &top);
			draw(path, sel, top);
		}
		else if ((down & KEY_Y) && strcmp(path, "/") != 0)
		{
			strcpy(path, "/");
			sel = top = 0;
			scan(path);
			draw(path, sel, top);
		}
		else if (down & KEY_B)
		{
			char* slash;
			if (strcmp(path, "/") == 0) return false;
			slash = strrchr(path, '/');
			if (slash == path) path[1] = 0; else *slash = 0;
			sel = top = 0;
			scan(path);
			draw(path, sel, top);
		}
		else if ((down & KEY_A) && s_count)
		{
			if (s_entries[sel].is_dir)
			{
				size_t len = strlen(path);
				snprintf(path + len, sizeof(path) - len, "%s%s",
					(len && path[len - 1] == '/') ? "" : "/", s_entries[sel].name);
				sel = top = 0;
				scan(path);
				draw(path, sel, top);
			}
			else
			{
				snprintf(out_path, out_len, "%s%s%s", path,
					(path[strlen(path) - 1] == '/') ? "" : "/", s_entries[sel].name);
				return true;
			}
		}

		uiFlush();
	}
	return false;
}

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "namepick.h"
#include "metadata.h"
#include "ui.h"

#define MAX_HITS 96
#define NAME_MAX 128
#define PAGE 24
#define COLS 49

static char s_hits[MAX_HITS][NAME_MAX];
static int s_count;

// Case insensitive substring search; the index is ASCII.
static bool contains(const char* haystack, const char* needle)
{
	size_t n = strlen(needle);
	if (!n) return true;
	for (; *haystack; haystack++)
		if (strncasecmp(haystack, needle, n) == 0) return true;
	return false;
}

static void search(const char* system, const char* term)
{
	char path[256], line[256];
	FILE* f;

	s_count = 0;
	if (!metaIndexPath(system, path, sizeof(path))) return;
	f = fopen(path, "r");
	if (!f) return;

	while (s_count < MAX_HITS && fgets(line, sizeof(line), f))
	{
		char* name = strchr(line, '|');
		int i;
		bool seen = false;

		if (!name) continue;
		name++;
		name[strcspn(name, "\r\n")] = 0;
		if (!*name || !contains(name, term)) continue;

		// Several serials share a title (revisions, EDC variants).
		for (i = 0; i < s_count && !seen; i++)
			seen = strcmp(s_hits[i], name) == 0;
		if (seen) continue;

		snprintf(s_hits[s_count], NAME_MAX, "%s", name);
		s_count++;
	}
	fclose(f);
}

static void draw(const char* term, int sel, int top)
{
	int i;

	uiSelectTop();
	uiClear();
	printf(UI_TITLE "Pick the game" UI_OFF "\n");
	printf(UI_DIM "search: %.40s" UI_OFF "\n", term);

	if (!s_count)
		printf("\n  " UI_DIM "no match" UI_OFF "\n");

	for (i = top; i < s_count && i < top + PAGE; i++)
		printf("%s%-*.*s%s\n", i == sel ? UI_SEL : "", COLS, COLS, s_hits[i],
			i == sel ? UI_OFF : "");

	if (s_count > PAGE)
		printf("\x1b[29;0H" UI_DIM "%d-%d of %d" UI_OFF,
			top + 1, (top + PAGE < s_count) ? top + PAGE : s_count, s_count);

	uiSelectBottom();
	uiClear();
	printf(UI_TITLE "Selection" UI_OFF "\n\n");
	if (s_count)
	{
		const char* name = s_hits[sel];
		int len = (int)strlen(name), off;
		for (off = 0; off < len && off < 39 * 4; off += 39)
			printf("%.39s\n", name + off);
		printf("\n" UI_DIM "%d of %d" UI_OFF "\n", sel + 1, s_count);
	}
	else
		printf(UI_DIM "Try fewer letters, or a different\nspelling." UI_OFF "\n");

	printf("\x1b[18;0H" UI_TITLE "Controls" UI_OFF "\n\n");
	printf("  A      use this name\n");
	printf("  X      search again\n");
	printf("  B      skip, keep template artwork\n");
	uiSelectTop();
}

// The system keyboard, so the user gets a real touch layout.
static bool askTerm(const char* initial, char* out, size_t out_len)
{
	static SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, (int)out_len - 1);
	swkbdSetHintText(&kb, "Part of the game title");
	swkbdSetInitialText(&kb, initial);
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, "Search", true);
	return swkbdInputText(&kb, out, out_len) == SWKBD_BUTTON_CONFIRM;
}

bool namePick(const char* system, const char* suggestion,
              char* out, size_t out_len)
{
	char term[64];
	int sel = 0, top = 0;

	if (!askTerm(suggestion ? suggestion : "", term, sizeof(term)))
		return false;

	search(system, term);
	draw(term, sel, top);

	while (aptMainLoop())
	{
		u32 down;
		hidScanInput();
		down = hidKeysDown();

		if (down & KEY_B) return false;

		if (down & KEY_X)
		{
			if (!askTerm(term, term, sizeof(term))) return false;
			search(system, term);
			sel = top = 0;
			draw(term, sel, top);
		}
		else if (down & (KEY_UP | KEY_DOWN | KEY_L | KEY_R))
		{
			if (down & KEY_DOWN) sel++;
			if (down & KEY_UP) sel--;
			if (down & KEY_R) { sel += PAGE; if (sel >= s_count) sel = s_count ? s_count - 1 : 0; }
			if (down & KEY_L) { sel -= PAGE; if (sel < 0) sel = 0; }
			if (s_count)
			{
				if (sel < 0) sel = s_count - 1;
				if (sel >= s_count) sel = 0;
				if (sel < top) top = sel;
				if (sel >= top + PAGE) top = sel - PAGE + 1;
			}
			draw(term, sel, top);
		}
		else if ((down & KEY_A) && s_count)
		{
			snprintf(out, out_len, "%s", s_hits[sel]);
			return true;
		}

		uiFlush();
	}
	return false;
}

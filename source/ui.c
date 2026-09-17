#include <stdio.h>

#include "ui.h"

static PrintConsole s_top;
static PrintConsole s_bottom;

void uiInitScreens(void)
{
	consoleInit(GFX_TOP, &s_top);
	consoleInit(GFX_BOTTOM, &s_bottom);
	consoleSelect(&s_top);
}

void uiSelectTop(void)    { consoleSelect(&s_top); }
void uiSelectBottom(void) { consoleSelect(&s_bottom); }

void uiClear(void) { printf("\x1b[2J\x1b[H"); }

void uiFlush(void)
{
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

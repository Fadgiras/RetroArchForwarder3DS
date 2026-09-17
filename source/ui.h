#pragma once
#include <3ds.h>

// Two consoles: the listing on top, details and help on the bottom screen.
void uiInitScreens(void);
void uiSelectTop(void);
void uiSelectBottom(void);
void uiClear(void);
void uiFlush(void);

// Colours, grouped so both screens stay consistent.
#define UI_TITLE "\x1b[36m"
#define UI_DIR   "\x1b[36m"
#define UI_DIM   "\x1b[30;1m"
#define UI_OK    "\x1b[32m"
#define UI_WARN  "\x1b[33m"
#define UI_ERR   "\x1b[31m"
#define UI_SEL   "\x1b[30;47m"
#define UI_OFF   "\x1b[0m"

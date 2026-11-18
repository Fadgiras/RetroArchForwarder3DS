#pragma once
#include <stdbool.h>
#include <stddef.h>

// Text helpers for the SteamGridDB lookup. Free of 3DS dependencies so they
// can be tested on a PC.

// "Resident Evil 2 - Dual Shock Ver. (USA) (Disc 1)" -> "Resident Evil 2 - Dual Shock Ver."
// Parenthesised and bracketed groups (region, disc, revision) are dropped,
// spaces collapsed.
void sgdbCleanTerm(const char* name, char* out, size_t out_len);

// "Resident Evil 2 - Dual Shock Ver." -> "Resident Evil 2". Returns false when
// there is no " - " subtitle to drop.
bool sgdbMainTitle(const char* term, char* out, size_t out_len);

// Same title, ignoring case, punctuation and spacing:
// "Metal Gear Solid: VR Missions" matches "Metal Gear Solid - VR Missions".
bool sgdbSameTitle(const char* a, const char* b);

// Percent-encodes a path segment.
void sgdbUrlEncode(const char* src, char* dst, size_t dst_len);

// A key as found in a text file: surrounding whitespace removed. Returns false
// when what remains does not look like a SteamGridDB API key.
bool sgdbParseKey(const char* text, char* out, size_t out_len);

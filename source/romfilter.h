#pragma once
#include <stdbool.h>
#include <stddef.h>

// File name filtering. Free of 3DS dependencies, so it is testable on a PC.

bool isRomFile(const char* name);
bool isDescriptor(const char* name);   // .cue .ccd .toc .m3u
bool isRawTrack(const char* name);     // .bin .img
bool sameStem(const char* a, const char* b);

// true when the raw track `name` is already described by one of `names`.
bool isCoveredTrack(const char* name, const char* const* names, int count);

// true when the descriptor lists `name` on one of its FILE lines.
// The only reliable method: tracks are often named "Game (Track 1).bin" while
// the descriptor is "Game.cue", so their stems differ.
bool cueReferences(const char* cue_text, const char* name);

// A RetroArch core is picked among the .3dsx files.
bool is3dsxFile(const char* name);

// A RetroArch core is recognised by its _libretro.3dsx suffix, which keeps
// FBI, this app and the other homebrew in /3ds/ out of the list.
bool isCoreFile(const char* name);


// Rewrites a game name the way the libretro thumbnail repository stores it.
void scrubThumbnailName(const char* src, char* dst, size_t dst_len);

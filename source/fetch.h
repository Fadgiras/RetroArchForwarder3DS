#pragma once
#include <stdbool.h>
#include <stddef.h>

// Fetching artwork from the public libretro thumbnail repository.
//
// It answers over plain HTTP, so no TLS is needed. The root certificates baked
// into the 3DS are too old for today's sites, and this avoids having to
// disable verification the way many homebrew apps do.
//
// Images are stored where RetroArch expects them, so it benefits too:
// sdmc:/retroarch/thumbnails/<system>/<kind>/<game>.png

// libretro system names a core covers, derived from its file name, most
// likely first. NULL once past the last one, and for index 0 when the core is
// unknown.
//
// A core often plays more than one machine, and the repository keeps each in
// its own place: Tetris is filed under "Nintendo - Game Boy", Pokemon Crystal
// under "Nintendo - Game Boy Color", never both. Nothing in a ROM says which,
// so the caller tries them in turn.
const char* fetchSystemForCore(const char* core_path, int index);

// Downloads the artwork when missing. Returns true if the file is there on
// return, whether it came from the network or was already present.
bool fetchArtwork(const char* system, const char* game, const char* kind,
                  char* out_path, size_t out_len);

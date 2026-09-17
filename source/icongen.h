#pragma once
#include <stdbool.h>
#include <stdint.h>

// Building the HOME menu icon from an image found on the card.
//
// An SMDH stores its icon in two fixed-size areas (24x24 then 48x48), tiled in
// 8x8 blocks whose pixels follow a Morton order. Since the size never changes,
// it can be replaced in place inside the template.

#define SMDH_ICON_SMALL_OFF 0x2040
#define SMDH_ICON_LARGE_OFF 0x24C0

// Position (x, y) of pixel number index inside an 8x8 tile: interleaved bits.
// Shared with the banner, which uses the very same tiling.
void tileMorton(int index, int* x, int* y);

// Average of the source square covered by one destination pixel.
void tileSampleBox(const unsigned char* src, int sw, int sh, int side,
                   int dx, int dy, unsigned char* out);

// Looks for an image for this ROM: next to it, then among the thumbnails
// downloaded by RetroArch. Returns false when nothing is found.
// "sdmc:/dir/Game (Europe).cue" -> "Game (Europe)"
void artStem(const char* path, char* out, size_t out_len);

// kind is "Named_Boxarts" (box art) or "Named_Titles" (title screen).
bool iconFindArtwork(const char* rom_path, const char* kind,
                     char* out_png, size_t out_len);

// Decodes the image and writes both icon areas of the SMDH.
bool iconApply(uint8_t* smdh, const char* png_path);

#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Replacing the texture of a 3DS banner.
//
// A banner is a CBMD: a header pointing at an LZ11-compressed CGFX and at the
// audio. The CGFX ends with the texture, 256x128 in RGBA4444, tiled in 8x8
// blocks just like icons.
//
// Only that texture is touched; the 3D model and the audio stay intact.

#define BANNER_TEX_W 256
#define BANNER_TEX_H 128
#define BANNER_TEX_SIZE (BANNER_TEX_W * BANNER_TEX_H * 2)

// Writes the rebuilt banner into out. Returns its size, or 0 on failure.
size_t bannerReplaceImage(const uint8_t* bnr, size_t bnr_len,
                          const char* image_path, uint8_t* out, size_t out_cap);

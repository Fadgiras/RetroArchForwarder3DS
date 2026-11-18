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

// Same, keeping the image's alpha: transparent areas let the HOME menu
// background show through. Opaque images give the same result as above.
size_t bannerReplaceImageAlpha(const uint8_t* bnr, size_t bnr_len,
                               const char* image_path, uint8_t* out, size_t out_cap);

// Room left for a logo inside the banner, so it never touches the edges.
#define BANNER_LOGO_W 232
#define BANNER_LOGO_H 100

// A transparent logo (SteamGridDB) over a background: the game's artwork
// blurred and dimmed, or a dark gradient when background_path is NULL.
size_t bannerReplaceWithLogo(const uint8_t* bnr, size_t bnr_len,
                             const char* background_path, const char* logo_path,
                             uint8_t* out, size_t out_cap);

// The composition itself, on plain buffers so it can be tested on a PC.
// out: 256x128 RGB. bg: RGB, may be NULL. logo: RGBA.
void bannerComposeLogo(uint8_t* out,
                       const uint8_t* bg, int bw, int bh,
                       const uint8_t* logo, int lw, int lh);

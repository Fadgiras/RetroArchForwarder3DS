#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "icongen.h"

#define THUMB_ROOT "sdmc:/retroarch/thumbnails"

// Position (x, y) of pixel number index inside an 8x8 tile: interleaved bits.
void tileMorton(int index, int* x, int* y)
{
	*x = (index & 1) | ((index >> 1) & 2) | ((index >> 2) & 4);
	*y = ((index >> 1) & 1) | ((index >> 2) & 2) | ((index >> 3) & 4);
}

static uint16_t rgb565(const unsigned char* p)
{
	return (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
}

// Box averaging: each destination pixel sums up the whole source square it
// covers. A 512x512 box art scaled to 48x48 otherwise loses its lettering.
void tileSampleBox(const unsigned char* src, int sw, int sh, int side,
                   int dx, int dy, unsigned char* out)
{
	int x0 = dx * sw / side, x1 = (dx + 1) * sw / side;
	int y0 = dy * sh / side, y1 = (dy + 1) * sh / side;
	unsigned r = 0, g = 0, b = 0, n = 0;
	int x, y;

	if (x1 <= x0) x1 = x0 + 1;
	if (y1 <= y0) y1 = y0 + 1;

	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++)
		{
			const unsigned char* p = src + ((size_t)y * sw + x) * 3;
			r += p[0]; g += p[1]; b += p[2];
			n++;
		}

	out[0] = (unsigned char)(r / n);
	out[1] = (unsigned char)(g / n);
	out[2] = (unsigned char)(b / n);
}

static void tileInto(uint8_t* dst, const unsigned char* src, int sw, int sh, int side)
{
	int ty, tx, i;
	for (ty = 0; ty < side; ty += 8)
		for (tx = 0; tx < side; tx += 8)
			for (i = 0; i < 64; i++)
			{
				unsigned char px[3];
				int x, y;
				tileMorton(i, &x, &y);
				tileSampleBox(src, sw, sh, side, tx + x, ty + y, px);
				*(uint16_t*)dst = rgb565(px);
				dst += 2;
			}
}

// Centre square crop, matching what the PC version does.
static unsigned char* squareCrop(unsigned char* img, int w, int h, int* side)
{
	int s = w < h ? w : h;
	int ox = (w - s) / 2, oy = (h - s) / 2;
	unsigned char* out = malloc((size_t)s * s * 3);
	int y;
	if (!out) return NULL;
	for (y = 0; y < s; y++)
		memcpy(out + (size_t)y * s * 3, img + (((size_t)(oy + y) * w) + ox) * 3, (size_t)s * 3);
	*side = s;
	return out;
}

static bool fileExists(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

// "sdmc:/dir/Game (Europe).cue" -> "Game (Europe)"
void artStem(const char* path, char* out, size_t out_len)
{
	const char* slash = strrchr(path, '/');
	const char* dot;
	size_t n;

	slash = slash ? slash + 1 : path;
	dot = strrchr(slash, '.');
	n = dot ? (size_t)(dot - slash) : strlen(slash);
	if (n >= out_len) n = out_len - 1;
	memcpy(out, slash, n);
	out[n] = 0;
}

bool iconFindArtwork(const char* rom_path, const char* kind,
                     char* out_png, size_t out_len)
{
	char stem[256];
	struct dirent* ent;
	DIR* dir;

	artStem(rom_path, stem, sizeof(stem));

	// 1. an image sitting next to the ROM (box art only)
	if (strcmp(kind, "Named_Boxarts") == 0)
	{
		const char* slash = strrchr(rom_path, '/');
		size_t dirlen = slash ? (size_t)(slash - rom_path) : 0;
		snprintf(out_png, out_len, "%.*s/%s.png", (int)dirlen, rom_path, stem);
		if (fileExists(out_png)) return true;
	}

	// 2. thumbnails downloaded by RetroArch, across every system folder
	dir = opendir(THUMB_ROOT);
	if (dir)
	{
		while ((ent = readdir(dir)))
		{
			if (ent->d_name[0] == '.') continue;
			snprintf(out_png, out_len, "%s/%s/%s/%s.png",
				THUMB_ROOT, ent->d_name, kind, stem);
			if (fileExists(out_png)) { closedir(dir); return true; }
		}
		closedir(dir);
	}

	out_png[0] = 0;
	return false;
}

bool iconApply(uint8_t* smdh, const char* png_path)
{
	int w, h, comp, side;
	unsigned char* img;
	unsigned char* sq;

	img = stbi_load(png_path, &w, &h, &comp, 3);
	if (!img) return false;

	sq = squareCrop(img, w, h, &side);
	stbi_image_free(img);
	if (!sq) return false;

	tileInto(smdh + SMDH_ICON_SMALL_OFF, sq, side, side, 24);
	tileInto(smdh + SMDH_ICON_LARGE_OFF, sq, side, side, 48);

	free(sq);
	return true;
}

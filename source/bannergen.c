#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#include "bannergen.h"
#include "icongen.h"

#define CGFX_OFFSET_FIELD 0x08
#define CWAV_OFFSET_FIELD 0x84
#define CWAV_SIZE_FIELD 0x0C

static uint32_t rd32(const uint8_t* p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Nintendo LZ11: one flag byte, then eight items, each either a literal or a
// back reference.
static size_t lz11Decompress(const uint8_t* src, uint8_t* dst, size_t dst_cap)
{
	size_t size = src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
	size_t pos = 4, len = 0;
	int bit;

	if (size > dst_cap) return 0;

	while (len < size)
	{
		uint8_t flags = src[pos++];
		for (bit = 0; bit < 8 && len < size; bit++)
		{
			size_t count, disp, i;
			uint8_t a, b, c, d;

			if (!(flags & (0x80 >> bit)))
			{
				dst[len++] = src[pos++];
				continue;
			}

			a = src[pos++];
			switch (a >> 4)
			{
				case 0:
					b = src[pos++]; c = src[pos++];
					count = (((size_t)(a & 0xF) << 4) | (b >> 4)) + 0x11;
					disp = (((size_t)(b & 0xF) << 8) | c) + 1;
					break;
				case 1:
					b = src[pos++]; c = src[pos++]; d = src[pos++];
					count = (((size_t)(a & 0xF) << 12) | ((size_t)b << 4) | (c >> 4)) + 0x111;
					disp = (((size_t)(c & 0xF) << 8) | d) + 1;
					break;
				default:
					b = src[pos++];
					count = (a >> 4) + 1;
					disp = (((size_t)(a & 0xF) << 8) | b) + 1;
					break;
			}

			if (disp > len || len + count > size) return 0;
			for (i = 0; i < count; i++, len++)
				dst[len] = dst[len - disp];
		}
	}
	return pos;
}

// Encoding without match searching: literals only. Always valid, and far
// simpler than a real compressor. Cost: one flag byte every eight.
static size_t lz11CompressLiteral(const uint8_t* src, size_t len, uint8_t* dst)
{
	size_t in = 0, out = 0;

	wr32(dst, ((uint32_t)len << 8) | 0x11);
	out = 4;

	while (in < len)
	{
		size_t chunk = len - in < 8 ? len - in : 8;
		dst[out++] = 0x00;
		memcpy(dst + out, src + in, chunk);
		out += chunk;
		in += chunk;
	}
	return out;
}

static bool encodeTexture(const char* image_path, uint8_t* dst)
{
	int w, h, comp, ty, tx, i;
	unsigned char* img = stbi_load(image_path, &w, &h, &comp, 3);
	if (!img) return false;

	for (ty = 0; ty < BANNER_TEX_H; ty += 8)
		for (tx = 0; tx < BANNER_TEX_W; tx += 8)
			for (i = 0; i < 64; i++)
			{
				unsigned char px[3];
				int x, y;
				uint16_t v;
				tileMorton(i, &x, &y);
				// The source is scaled to 256x128 rather than to a square, so width
				// and height are sampled independently.
				{
					int x0 = (tx + x) * w / BANNER_TEX_W, x1 = (tx + x + 1) * w / BANNER_TEX_W;
					int y0 = (ty + y) * h / BANNER_TEX_H, y1 = (ty + y + 1) * h / BANNER_TEX_H;
					unsigned r = 0, g = 0, b = 0, n = 0;
					int sx, sy;
					if (x1 <= x0) x1 = x0 + 1;
					if (y1 <= y0) y1 = y0 + 1;
					for (sy = y0; sy < y1; sy++)
						for (sx = x0; sx < x1; sx++)
						{
							const unsigned char* p = img + ((size_t)sy * w + sx) * 3;
							r += p[0]; g += p[1]; b += p[2]; n++;
						}
					px[0] = (unsigned char)(r / n);
					px[1] = (unsigned char)(g / n);
					px[2] = (unsigned char)(b / n);
				}
				v = (uint16_t)(((px[0] >> 4) << 12) | ((px[1] >> 4) << 8)
					| ((px[2] >> 4) << 4) | 0xF);
				dst[0] = (uint8_t)v; dst[1] = (uint8_t)(v >> 8);
				dst += 2;
			}

	stbi_image_free(img);
	return true;
}

size_t bannerReplaceImage(const uint8_t* bnr, size_t bnr_len,
                          const char* image_path, uint8_t* out, size_t out_cap)
{
	uint32_t cgfx_off = rd32(bnr + CGFX_OFFSET_FIELD);
	uint32_t cwav_off = rd32(bnr + CWAV_OFFSET_FIELD);
	uint32_t cwav_len;
	size_t cgfx_len, packed, pad, total;
	uint8_t* cgfx;

	if (cgfx_off >= bnr_len || cwav_off >= bnr_len) return 0;
	cwav_len = rd32(bnr + cwav_off + CWAV_SIZE_FIELD);
	if ((size_t)cwav_off + cwav_len > bnr_len) return 0;

	cgfx_len = bnr[cgfx_off + 1] | ((size_t)bnr[cgfx_off + 2] << 8)
		| ((size_t)bnr[cgfx_off + 3] << 16);
	if (cgfx_len < BANNER_TEX_SIZE) return 0;

	cgfx = malloc(cgfx_len);
	if (!cgfx) return 0;

	if (!lz11Decompress(bnr + cgfx_off, cgfx, cgfx_len)
	    || !encodeTexture(image_path, cgfx + cgfx_len - BANNER_TEX_SIZE))
	{
		free(cgfx);
		return 0;
	}

	// Header untouched, CGFX recompressed, audio re-appended, offset fixed.
	if (cgfx_off + 4 + cgfx_len + cgfx_len / 8 + 0x20 + cwav_len > out_cap)
	{
		free(cgfx);
		return 0;
	}

	memcpy(out, bnr, cgfx_off);
	packed = lz11CompressLiteral(cgfx, cgfx_len, out + cgfx_off);
	free(cgfx);

	pad = (0x20 - ((cgfx_off + packed) & 0x1F)) & 0x1F;
	memset(out + cgfx_off + packed, 0, pad);
	total = cgfx_off + packed + pad;

	memcpy(out + total, bnr + cwav_off, cwav_len);
	wr32(out + CWAV_OFFSET_FIELD, (uint32_t)total);
	return total + cwav_len;
}

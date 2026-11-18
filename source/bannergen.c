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

// Writes a 256x128 RGB picture as the banner texture: RGBA4444, 8x8 tiles in
// Morton order, fully opaque.
static void encodeRgb(const uint8_t* rgb, uint8_t* dst)
{
	int ty, tx, i;

	for (ty = 0; ty < BANNER_TEX_H; ty += 8)
		for (tx = 0; tx < BANNER_TEX_W; tx += 8)
			for (i = 0; i < 64; i++)
			{
				const uint8_t* px;
				int x, y;
				uint16_t v;
				tileMorton(i, &x, &y);
				px = rgb + ((size_t)(ty + y) * BANNER_TEX_W + tx + x) * 3;
				v = (uint16_t)(((px[0] >> 4) << 12) | ((px[1] >> 4) << 8)
					| ((px[2] >> 4) << 4) | 0xF);
				dst[0] = (uint8_t)v; dst[1] = (uint8_t)(v >> 8);
				dst += 2;
			}
}

// The source is scaled to 256x128 rather than to a square, so width and
// height are sampled independently. Each pixel averages the area it covers.
static void stretchRgb(const unsigned char* img, int w, int h, uint8_t* out)
{
	int x, y;

	for (y = 0; y < BANNER_TEX_H; y++)
		for (x = 0; x < BANNER_TEX_W; x++)
		{
			int x0 = x * w / BANNER_TEX_W, x1 = (x + 1) * w / BANNER_TEX_W;
			int y0 = y * h / BANNER_TEX_H, y1 = (y + 1) * h / BANNER_TEX_H;
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
			out[((size_t)y * BANNER_TEX_W + x) * 3 + 0] = (uint8_t)(r / n);
			out[((size_t)y * BANNER_TEX_W + x) * 3 + 1] = (uint8_t)(g / n);
			out[((size_t)y * BANNER_TEX_W + x) * 3 + 2] = (uint8_t)(b / n);
		}
}

static bool encodeTexture(const char* image_path, uint8_t* dst)
{
	int w, h, comp;
	uint8_t* rgb;
	unsigned char* img = stbi_load(image_path, &w, &h, &comp, 3);
	if (!img) return false;

	rgb = malloc(BANNER_TEX_W * BANNER_TEX_H * 3);
	if (!rgb) { stbi_image_free(img); return false; }
	stretchRgb(img, w, h, rgb);
	stbi_image_free(img);
	encodeRgb(rgb, dst);
	free(rgb);
	return true;
}

// ---------------------------------------------------------------- logo banner

// Scales the background so it covers the whole banner, cropping the excess
// around the centre, rather than stretching it.
static void coverRgb(const unsigned char* img, int w, int h, uint8_t* out)
{
	double sx = (double)w / BANNER_TEX_W, sy = (double)h / BANNER_TEX_H;
	double s = sx < sy ? sx : sy;
	double ox = (w - BANNER_TEX_W * s) / 2, oy = (h - BANNER_TEX_H * s) / 2;
	int x, y;

	for (y = 0; y < BANNER_TEX_H; y++)
		for (x = 0; x < BANNER_TEX_W; x++)
		{
			int x0 = (int)(ox + x * s), x1 = (int)(ox + (x + 1) * s);
			int y0 = (int)(oy + y * s), y1 = (int)(oy + (y + 1) * s);
			unsigned r = 0, g = 0, b = 0, n = 0;
			int px, py;
			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			if (x1 > w) x1 = w;
			if (y1 > h) y1 = h;
			if (x0 >= x1) x0 = x1 - 1;
			if (y0 >= y1) y0 = y1 - 1;
			for (py = y0; py < y1; py++)
				for (px = x0; px < x1; px++)
				{
					const unsigned char* p = img + ((size_t)py * w + px) * 3;
					r += p[0]; g += p[1]; b += p[2]; n++;
				}
			out[((size_t)y * BANNER_TEX_W + x) * 3 + 0] = (uint8_t)(r / n);
			out[((size_t)y * BANNER_TEX_W + x) * 3 + 1] = (uint8_t)(g / n);
			out[((size_t)y * BANNER_TEX_W + x) * 3 + 2] = (uint8_t)(b / n);
		}
}

// Three box blur passes approach a gaussian, cheaply.
static void blurRgb(uint8_t* rgb, int radius)
{
	uint8_t* tmp = malloc(BANNER_TEX_W * BANNER_TEX_H * 3);
	int pass, x, y, c, k;

	if (!tmp) return;
	for (pass = 0; pass < 3; pass++)
	{
		for (y = 0; y < BANNER_TEX_H; y++)
			for (x = 0; x < BANNER_TEX_W; x++)
				for (c = 0; c < 3; c++)
				{
					unsigned sum = 0, n = 0;
					for (k = -radius; k <= radius; k++)
					{
						int xx = x + k;
						if (xx < 0 || xx >= BANNER_TEX_W) continue;
						sum += rgb[((size_t)y * BANNER_TEX_W + xx) * 3 + c]; n++;
					}
					tmp[((size_t)y * BANNER_TEX_W + x) * 3 + c] = (uint8_t)(sum / n);
				}
		for (y = 0; y < BANNER_TEX_H; y++)
			for (x = 0; x < BANNER_TEX_W; x++)
				for (c = 0; c < 3; c++)
				{
					unsigned sum = 0, n = 0;
					for (k = -radius; k <= radius; k++)
					{
						int yy = y + k;
						if (yy < 0 || yy >= BANNER_TEX_H) continue;
						sum += tmp[((size_t)yy * BANNER_TEX_W + x) * 3 + c]; n++;
					}
					rgb[((size_t)y * BANNER_TEX_W + x) * 3 + c] = (uint8_t)(sum / n);
				}
	}
	free(tmp);
}

void bannerComposeLogo(uint8_t* out,
                       const uint8_t* bg, int bw, int bh,
                       const uint8_t* logo, int lw, int lh)
{
	int x, y, stride;
	double scale, fw, fh;
	int dw, dh, ox, oy;

	// Background: the game's own artwork, blurred and dimmed so the logo
	// reads on top. Without one, a dark vertical gradient.
	if (bg && bw > 0 && bh > 0)
	{
		coverRgb(bg, bw, bh, out);
		blurRgb(out, 3);
		for (x = 0; x < BANNER_TEX_W * BANNER_TEX_H * 3; x++)
			out[x] = (uint8_t)(out[x] * 45 / 100);
	}
	else
		for (y = 0; y < BANNER_TEX_H; y++)
			for (x = 0; x < BANNER_TEX_W; x++)
			{
				uint8_t* p = out + ((size_t)y * BANNER_TEX_W + x) * 3;
				p[0] = (uint8_t)(0x30 - y * 0x1C / BANNER_TEX_H);
				p[1] = (uint8_t)(0x30 - y * 0x1C / BANNER_TEX_H);
				p[2] = (uint8_t)(0x3C - y * 0x24 / BANNER_TEX_H);
			}

	if (!logo || lw <= 0 || lh <= 0) return;

	// Logos often come with empty transparent margins: trim them, so the
	// visible part is what gets centred and sized.
	{
		int minx = lw, miny = lh, maxx = -1, maxy = -1;
		for (y = 0; y < lh; y++)
			for (x = 0; x < lw; x++)
				if (logo[((size_t)y * lw + x) * 4 + 3] > 8)
				{
					if (x < minx) minx = x;
					if (x > maxx) maxx = x;
					if (y < miny) miny = y;
					if (y > maxy) maxy = y;
				}
		if (maxx < 0) return;
		stride = lw;
		logo += ((size_t)miny * lw + minx) * 4;
		lw = maxx - minx + 1;
		lh = maxy - miny + 1;
	}

	// The logo fits inside a margin, aspect kept, centred. Upscaling is
	// capped at 2x so a tiny logo does not turn to mush.
	fw = (double)BANNER_LOGO_W / lw;
	fh = (double)BANNER_LOGO_H / lh;
	scale = fw < fh ? fw : fh;
	if (scale > 2.0) scale = 2.0;
	dw = (int)(lw * scale + 0.5);
	dh = (int)(lh * scale + 0.5);
	if (dw < 1) dw = 1;
	if (dh < 1) dh = 1;
	ox = (BANNER_TEX_W - dw) / 2;
	oy = (BANNER_TEX_H - dh) / 2;

	for (y = 0; y < dh; y++)
		for (x = 0; x < dw; x++)
		{
			// Area average in premultiplied alpha: transparent pixels carry no
			// colour, so edges do not pick up a dark fringe.
			int x0 = (int)((double)x * lw / dw), x1 = (int)((double)(x + 1) * lw / dw);
			int y0 = (int)((double)y * lh / dh), y1 = (int)((double)(y + 1) * lh / dh);
			unsigned long r = 0, g = 0, b = 0, a = 0, n = 0;
			int sx, sy;
			uint8_t* d;

			if (x1 <= x0) x1 = x0 + 1;
			if (y1 <= y0) y1 = y0 + 1;
			if (x1 > lw) x1 = lw;
			if (y1 > lh) y1 = lh;
			for (sy = y0; sy < y1; sy++)
				for (sx = x0; sx < x1; sx++)
				{
					const uint8_t* p = logo + ((size_t)sy * stride + sx) * 4;
					r += (unsigned long)p[0] * p[3];
					g += (unsigned long)p[1] * p[3];
					b += (unsigned long)p[2] * p[3];
					a += p[3];
					n++;
				}
			if (!n || !a) continue;

			d = out + ((size_t)(oy + y) * BANNER_TEX_W + ox + x) * 3;
			{
				unsigned long alpha = a / n;            // 0..255
				unsigned long cr = r / a, cg = g / a, cb = b / a;
				d[0] = (uint8_t)((cr * alpha + d[0] * (255 - alpha)) / 255);
				d[1] = (uint8_t)((cg * alpha + d[1] * (255 - alpha)) / 255);
				d[2] = (uint8_t)((cb * alpha + d[2] * (255 - alpha)) / 255);
			}
		}
}

// Rebuilds the banner around a texture produced by fill().
static size_t rebuildBanner(const uint8_t* bnr, size_t bnr_len,
                            bool (*fill)(void* ctx, uint8_t* tex), void* ctx,
                            uint8_t* out, size_t out_cap);

static bool fillFromImage(void* ctx, uint8_t* tex)
{
	return encodeTexture((const char*)ctx, tex);
}

typedef struct { const char* bg; const char* logo; } logo_job;

static bool fillFromLogo(void* ctx, uint8_t* tex)
{
	logo_job* job = (logo_job*)ctx;
	int lw, lh, lc, bw = 0, bh = 0, bc;
	unsigned char* logo = stbi_load(job->logo, &lw, &lh, &lc, 4);
	unsigned char* bg = NULL;
	uint8_t* rgb;

	if (!logo) return false;
	if (job->bg) bg = stbi_load(job->bg, &bw, &bh, &bc, 3);

	rgb = malloc(BANNER_TEX_W * BANNER_TEX_H * 3);
	if (!rgb)
	{
		stbi_image_free(logo);
		if (bg) stbi_image_free(bg);
		return false;
	}
	bannerComposeLogo(rgb, bg, bw, bh, logo, lw, lh);
	stbi_image_free(logo);
	if (bg) stbi_image_free(bg);
	encodeRgb(rgb, tex);
	free(rgb);
	return true;
}

size_t bannerReplaceWithLogo(const uint8_t* bnr, size_t bnr_len,
                             const char* background_path, const char* logo_path,
                             uint8_t* out, size_t out_cap)
{
	logo_job job;
	job.bg = background_path;
	job.logo = logo_path;
	return rebuildBanner(bnr, bnr_len, fillFromLogo, &job, out, out_cap);
}

size_t bannerReplaceImage(const uint8_t* bnr, size_t bnr_len,
                          const char* image_path, uint8_t* out, size_t out_cap)
{
	return rebuildBanner(bnr, bnr_len, fillFromImage, (void*)image_path, out, out_cap);
}

static size_t rebuildBanner(const uint8_t* bnr, size_t bnr_len,
                            bool (*fill)(void* ctx, uint8_t* tex), void* ctx,
                            uint8_t* out, size_t out_cap)
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
	    || !fill(ctx, cgfx + cgfx_len - BANNER_TEX_SIZE))
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

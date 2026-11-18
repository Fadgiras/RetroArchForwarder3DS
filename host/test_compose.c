// Composes a logo over a background the way the banner does, and writes the
// 256x128 result as a PPM to look at on a PC.
//   test_compose <logo.png> <out.ppm> [background.png]
#include <stdio.h>
#include <stdlib.h>
#include "../source/stb_image.h"
#include "../source/bannergen.h"

int main(int argc, char** argv)
{
	int lw, lh, lc, bw = 0, bh = 0, bc;
	unsigned char *logo, *bg = NULL;
	static unsigned char out[256 * 128 * 3];
	FILE* f;

	if (argc < 3) { fprintf(stderr, "usage: test_compose <logo.png> <out.ppm> [bg.png]\n"); return 1; }
	logo = stbi_load(argv[1], &lw, &lh, &lc, 4);
	if (!logo) { fprintf(stderr, "logo not readable\n"); return 2; }
	if (argc > 3) bg = stbi_load(argv[3], &bw, &bh, &bc, 3);

	bannerComposeLogo(out, bg, bw, bh, logo, lw, lh);

	f = fopen(argv[2], "wb");
	fprintf(f, "P6\n256 128\n255\n");
	fwrite(out, 1, sizeof(out), f);
	fclose(f);
	printf("logo %dx%d, background %dx%d\n", lw, lh, bw, bh);
	return 0;
}

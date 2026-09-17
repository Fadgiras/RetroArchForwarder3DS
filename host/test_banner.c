// Runs bannerReplaceImage on a banner and an image, so the output can be
// compared against the Python reference.
#include <stdio.h>
#include <stdlib.h>
#include "../source/bannergen.h"

int main(int argc, char** argv)
{
	FILE* f;
	long len;
	unsigned char *bnr, *out;
	size_t n;

	if (argc < 4) { fprintf(stderr, "usage: test_banner <banner> <image> <out>\n"); return 1; }

	f = fopen(argv[1], "rb");
	if (!f) { fprintf(stderr, "banner not found\n"); return 2; }
	fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
	bnr = malloc(len); fread(bnr, 1, len, f); fclose(f);

	out = malloc(1 << 20);
	n = bannerReplaceImage(bnr, len, argv[2], out, 1 << 20);
	if (!n) { fprintf(stderr, "failed\n"); return 3; }

	f = fopen(argv[3], "wb"); fwrite(out, 1, n, f); fclose(f);
	printf("C: %lu bytes\n", (unsigned long)n);
	return 0;
}

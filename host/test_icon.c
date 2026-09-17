// Runs iconApply on a blank SMDH and dumps both icon areas, so the tiling can
// be compared against the Python reference.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/icongen.h"

int main(int argc, char** argv)
{
	unsigned char* smdh;
	FILE* f;

	if (argc < 3) { fprintf(stderr, "usage: test_icon <image> <out.bin>\n"); return 1; }

	smdh = calloc(1, 0x36C0);
	if (!iconApply(smdh, argv[1])) { fprintf(stderr, "cannot decode image\n"); return 2; }

	f = fopen(argv[2], "wb");
	fwrite(smdh + SMDH_ICON_SMALL_OFF, 1, 0x480, f);
	fwrite(smdh + SMDH_ICON_LARGE_OFF, 1, 0x1200, f);
	fclose(f);
	printf("wrote %s\n", argv[2]);
	return 0;
}

// Reads a ROM's own identity and resolves it to the canonical game name.
//
//   gcc -O2 -DINDEX_DIR='"romfs/db"' -o test_meta test_meta.c ../source/metadata.c
//   ./test_meta "Sony - PlayStation" game.pbp game.cue

#include <stdio.h>
#include "../source/metadata.h"

int main(int argc, char** argv)
{
	char serial[SERIAL_LEN], name[256], title[256], index[256];
	int i;

	if (argc < 3) { fprintf(stderr, "usage: test_meta <system> <rom> [rom...]\n"); return 1; }

	if (metaIndexPath(argv[1], index, sizeof(index)))
		printf("index: %s\n\n", index);

	for (i = 2; i < argc; i++)
	{
		printf("%s\n", argv[i]);

		if (metaPbpTitle(argv[i], title, sizeof(title)))
			printf("  embedded title : %s\n", title);

		if (!metaSerial(argv[i], serial, sizeof(serial)))
		{
			printf("  serial         : none readable\n\n");
			continue;
		}
		printf("  serial         : %s\n", serial);

		if (metaNameForSerial(argv[1], serial, name, sizeof(name)))
			printf("  canonical name : %s\n\n", name);
		else
			printf("  canonical name : not in the index\n\n");
	}
	return 0;
}

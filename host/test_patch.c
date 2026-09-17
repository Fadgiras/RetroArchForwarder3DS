// Host harness: reproduces exactly what the console app does, so its output
// can be compared against the Python reference.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/cia_patch.h"

static uint8_t* slurp(const char* path, size_t* len)
{
	FILE* f = fopen(path, "rb");
	uint8_t* buf;
	if (!f) { fprintf(stderr, "cannot open: %s\n", path); exit(2); }
	fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
	buf = malloc(*len);
	if (fread(buf, 1, *len, f) != *len) { fprintf(stderr, "short read\n"); exit(2); }
	fclose(f);
	return buf;
}

int main(int argc, char** argv)
{
	size_t len;
	uint8_t* data;
	cia_t cia;
	FILE* out;
	int rc;

	if (argc < 8) { fprintf(stderr, "usage: test_patch <template> <rom> <core> <name> <tid hex> <proc> <out>\n"); return 1; }

	data = slurp(argv[1], &len);
	if ((rc = cia_open(&cia, data, len)) != CIA_OK) { fprintf(stderr, "cia_open: %d\n", rc); return 3; }
	if ((rc = cia_set_rom_path(&cia, argv[2])) != CIA_OK) { fprintf(stderr, "rom_path: %d\n", rc); return 3; }
	if ((rc = cia_set_core_path(&cia, argv[3])) != CIA_OK) { fprintf(stderr, "core_path: %d\n", rc); return 3; }
	if ((rc = cia_set_titles(&cia, argv[4], "RetroArch forwarder")) != CIA_OK) { fprintf(stderr, "titles: %d\n", rc); return 3; }
	cia_set_title_id(&cia, strtoull(argv[5], NULL, 16));
	cia_set_process_name(&cia, argv[6]);
	cia_reseal(&cia);

	out = fopen(argv[7], "wb");
	fwrite(data, 1, len, out);
	fclose(out);
	printf("C: %lu bytes\n", (unsigned long)len);
	return 0;
}

#include <stdio.h>
#include <string.h>
#include "../source/sha256.h"

static void show(const char* in)
{
	uint8_t h[32];
	int i;
	sha256(in, strlen(in), h);
	printf("%-20s ", in[0] ? in : "(empty)");
	for (i = 0; i < 32; i++) printf("%02x", h[i]);
	printf("\n");
}

int main(void)
{
	show("");
	show("abc");
	show("ARGV");
	show("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
	return 0;
}

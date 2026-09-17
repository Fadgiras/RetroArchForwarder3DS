// Checks the substitution the libretro thumbnail repository applies to file
// names. Exercises the real function from source/romfilter.c rather than a
// copy of it, so the table under test is the one actually shipped.

#include <stdio.h>
#include <string.h>
#include "../source/romfilter.h"

static int fails;

static void expect(const char* label, const char* got, const char* want)
{
	if (strcmp(got, want) != 0) { printf("  FAIL %-40s got %s\n", label, got); fails++; }
	else printf("  ok   %-40s\n", label);
}

int main(void)
{
	// The eleven characters, built by code point so no escaping can lose one.
	static const unsigned char INVALID[] = {
		'&', '*', '/', ':', '`', '"', '<', '>', '?', 92, '|', 0
	};
	char in[64], out[64], label[64];
	size_t i;

	printf("every character of the table becomes an underscore\n");
	for (i = 0; INVALID[i]; i++)
	{
		in[0] = 'a'; in[1] = (char)INVALID[i]; in[2] = 'b'; in[3] = 0;
		scrubThumbnailName(in, out, sizeof(out));
		snprintf(label, sizeof(label), "0x%02X", INVALID[i]);
		expect(label, out, "a_b");
	}

	printf("\ncharacters the repository does keep\n");
	scrubThumbnailName("Abe's Oddysee (Europe) [!].cue", out, sizeof(out));
	expect("apostrophe, parentheses, brackets",
	       out, "Abe's Oddysee (Europe) [!].cue");

	printf("\na real case\n");
	scrubThumbnailName("Command & Conquer - Alerte Rouge (France) (Disc 2)",
	                   out, sizeof(out));
	expect("ampersand replaced",
	       out, "Command _ Conquer - Alerte Rouge (France) (Disc 2)");

	printf("\ntruncation stays safe\n");
	scrubThumbnailName("abcdefghij", out, 5);
	expect("stops at the buffer size", out, "abcd");

	printf("\n%s\n", fails ? "SOME TESTS FAIL" : "all tests pass");
	return fails != 0;
}

#include <stdio.h>
#include <string.h>
#include "../source/sgdbterm.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void clean(const char* in, const char* want)
{
	char out[128];
	sgdbCleanTerm(in, out, sizeof(out));
	if (strcmp(out, want) != 0) { printf("FAIL clean '%s' -> '%s', want '%s'\n", in, out, want); fails++; }
}

int main(void)
{
	char out[128];

	clean("Silent Hill (USA)", "Silent Hill");
	clean("Resident Evil 2 - Dual Shock Ver. (USA) (Disc 1)", "Resident Evil 2 - Dual Shock Ver.");
	clean("Ace Combat 3 - Electrosphere (Japan, Asia) (Rev 1)", "Ace Combat 3 - Electrosphere");
	clean("Metal Gear Solid (USA) (Disc 1) (Rev 1)", "Metal Gear Solid");
	clean("Final Fantasy VII [T-En]  (Disc 1)", "Final Fantasy VII");
	clean("  Crash_Bandicoot  ", "Crash Bandicoot");
	clean("Game - (Proto)", "Game");
	clean("(USA)", "");
	{
		char small[6];
		sgdbCleanTerm("Silent Hill", small, sizeof(small));
		CHECK(strcmp(small, "Silen") == 0);
	}

	CHECK(sgdbMainTitle("Resident Evil 2 - Dual Shock Ver.", out, sizeof(out)) && strcmp(out, "Resident Evil 2") == 0);
	CHECK(!sgdbMainTitle("Silent Hill", out, sizeof(out)));
	CHECK(!sgdbMainTitle(" - x", out, sizeof(out)) || out[0]);

	CHECK(sgdbSameTitle("Metal Gear Solid: VR Missions", "Metal Gear Solid - VR Missions"));
	CHECK(sgdbSameTitle("SILENT HILL", "Silent Hill"));
	CHECK(sgdbSameTitle("Ace Combat 3: Electrosphere", "Ace Combat 3 - Electrosphere"));
	CHECK(!sgdbSameTitle("Silent Hill 2", "Silent Hill"));
	CHECK(!sgdbSameTitle("Resident Evil 2", "Resident Evil"));
	CHECK(sgdbSameTitle("", ""));

	sgdbUrlEncode("Medal of Honor: Rising Sun & co", out, sizeof(out));
	CHECK(strcmp(out, "Medal%20of%20Honor%3A%20Rising%20Sun%20%26%20co") == 0);

	CHECK(sgdbParseKey("  0123456789abcdef0123456789abcdef\r\n", out, sizeof(out)) && strcmp(out, "0123456789abcdef0123456789abcdef") == 0);
	CHECK(!sgdbParseKey("short", out, sizeof(out)));
	CHECK(!sgdbParseKey("0123456789abcdef0123 extra", out, sizeof(out)));
	CHECK(!sgdbParseKey("0123456789abcdef01\"23", out, sizeof(out)));
	CHECK(!sgdbParseKey("", out, sizeof(out)));

	printf(fails ? "%d failure(s)\n" : "sgdbterm: all tests passed\n", fails);
	return fails != 0;
}

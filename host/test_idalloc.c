#include <stdio.h>
#include <string.h>
#include "../source/idalloc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

// A fake console: the 44 NDS forwarders and the one homebrew title found on a
// real card, all inside the forwarder range.
static uint64_t installed[64];
static int installedCount;

static bool taken(uint64_t tid, void* ctx)
{
	int i;
	const char* registry = (const char*)ctx;
	for (i = 0; i < installedCount; i++)
		if (installed[i] == tid) return true;
	return idRegistryHasTitle(registry, tid);
}

static bool allTaken(uint64_t tid, void* ctx)
{
	(void)tid; (void)ctx;
	return true;
}

int main(void)
{
	char line[600];
	uint64_t tid, pref;
	int i;
	const char* registry =
		"000400000FF42B00|sdmc:/Roms PSX/Old.chd\n"
		"garbage line\n"
		"000400000FF1C100|sdmc:/roms/psx/Medal of Honor (USA).chd\r\n"
		"000400000FF1C200|sdmc:/roms/psx/Medal of Honor (USA).chd\n"
		"0004000000123400|sdmc:/outside/range.chd\n";

	installed[installedCount++] = 0x000400000FF12800ULL;
	for (i = 0; i < 0x2c; i++)
		installed[installedCount++] = 0x000400000FF40000ULL | ((uint64_t)i << 8);

	// The hash is the one the original code used: same ids for old builds.
	CHECK(idPreferredTitle("sdmc:/Roms PSX/Resident Evil 3 - Nemesis (USA).chd") == 0x000400000FF40500ULL);
	CHECK(idPreferredTitle("sdmc:/Roms PSX/Metal Gear Solid - VR Missions (USA).chd") == 0x000400000FF42B00ULL);
	CHECK(idPreferredTitle("sdmc:/roms/psx/Air Combat (USA).chd") == 0x000400000FF0EA00ULL);

	// A taken id moves on to the next free one instead of being reused.
	pref = idPreferredTitle("sdmc:/Roms PSX/Resident Evil 3 - Nemesis (USA).chd");
	tid = idPickFree(pref, taken, (void*)"");
	CHECK(tid == 0x000400000FF42C00ULL);
	CHECK(tid != pref && idInRange(tid));

	// Registry entries reserve their id even when the title is not installed.
	tid = idPickFree(0x000400000FF1C100ULL, taken, (void*)registry);
	CHECK(tid == 0x000400000FF1C300ULL);

	// Wrapping at the top of the range.
	tid = idPickFree(0x000400000FFFFF00ULL, taken, (void*)"");
	CHECK(tid == 0x000400000FFFFF00ULL);
	installed[installedCount++] = 0x000400000FFFFF00ULL;
	tid = idPickFree(0x000400000FFFFF00ULL, taken, (void*)"");
	CHECK(tid == 0x000400000FF00000ULL);

	// Lookup: last line wins, CR stripped, out-of-range lines ignored.
	CHECK(idRegistryFind(registry, "sdmc:/roms/psx/Medal of Honor (USA).chd", &tid) && tid == 0x000400000FF1C200ULL);
	CHECK(!idRegistryFind(registry, "sdmc:/roms/psx/Medal of Honor (USA).ch", &tid));
	CHECK(!idRegistryFind(registry, "sdmc:/outside/range.chd", &tid));
	CHECK(!idRegistryFind("", "x", &tid));
	CHECK(!idRegistryFind(NULL, "x", &tid));
	CHECK(idRegistryHasTitle(registry, 0x000400000FF42B00ULL));
	CHECK(!idRegistryHasTitle(registry, 0x000400000FF42C00ULL));

	// Lines round-trip.
	CHECK(idRegistryLine(line, sizeof(line), 0x000400000FF0EA00ULL, "sdmc:/roms/psx/Air Combat (USA).chd") > 0);
	CHECK(strcmp(line, "000400000FF0EA00|sdmc:/roms/psx/Air Combat (USA).chd\n") == 0);
	CHECK(idRegistryFind(line, "sdmc:/roms/psx/Air Combat (USA).chd", &tid) && tid == 0x000400000FF0EA00ULL);
	CHECK(idRegistryLine(line, 10, 1, "long path") == 0);

	// Range check.
	CHECK(idInRange(0x000400000FF00000ULL));
	CHECK(!idInRange(0x000400000D921E00ULL));
	CHECK(!idInRange(0x000400000FF00001ULL));

	// A full range answers 0 instead of looping or reusing an id.
	CHECK(idPickFree(0x000400000FF12300ULL, allTaken, NULL) == 0);

	printf(fails ? "%d failure(s)\n" : "idalloc: all tests passed\n", fails);
	return fails != 0;
}

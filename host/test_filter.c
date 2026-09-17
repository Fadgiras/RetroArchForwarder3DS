// File name filtering, checked on the host: recognised extensions, reading
// FILE lines out of a disc descriptor, and hiding the tracks it describes.

#include <stdio.h>
#include <string.h>
#include "../source/romfilter.h"

static int fails;

static void expect(const char* label, bool got, bool want)
{
	if (got != want) { printf("  FAIL %-44s expected %d, got %d\n", label, want, got); fails++; }
	else printf("  ok   %-44s\n", label);
}

static const char CUE[] =
	"FILE \"Abe (Track 1).bin\" BINARY\r\n"
	"  TRACK 01 MODE2/2352\r\n"
	"    INDEX 01 00:00:00\r\n"
	"FILE \"Abe (Track 2).bin\" BINARY\r\n"
	"  TRACK 02 AUDIO\r\n";

static const char CUE_NOQUOTE[] = "FILE Tomb.bin BINARY\n  TRACK 01 MODE1/2352\n";

int main(void)
{
	static const char* const dir[] = {
		"Abe.cue", "Abe (Track 1).bin", "Abe (Track 2).bin",
		"Crash.chd", "FF9.pbp", "notes.txt", "Tomb.bin",
		"Tomb.CUE", "FF7.m3u", "FF7 (disc 1).chd",
	};
	const int n = sizeof(dir) / sizeof(dir[0]);

	printf("PlayStation extensions as declared by the core\n");
	expect("bin", isRomFile("a.bin"), true);
	expect("cue", isRomFile("a.cue"), true);
	expect("img", isRomFile("a.img"), true);
	expect("mdf", isRomFile("a.mdf"), true);
	expect("pbp", isRomFile("a.pbp"), true);
	expect("toc", isRomFile("a.toc"), true);
	expect("cbn", isRomFile("a.cbn"), true);
	expect("m3u", isRomFile("a.m3u"), true);
	expect("ccd", isRomFile("a.ccd"), true);
	expect("chd", isRomFile("a.chd"), true);
	expect("iso", isRomFile("a.iso"), true);
	expect("exe", isRomFile("a.exe"), true);
	expect("txt rejected", isRomFile("notes.txt"), false);
	expect("no extension rejected", isRomFile("README"), false);
	expect("uppercase CHD", isRomFile("A.CHD"), true);

	printf("\nreading the FILE lines of a descriptor\n");
	expect("track 1 listed", cueReferences(CUE, "Abe (Track 1).bin"), true);
	expect("track 2 listed", cueReferences(CUE, "Abe (Track 2).bin"), true);
	expect("another game not listed", cueReferences(CUE, "Other.bin"), false);
	expect("a prefix is not enough", cueReferences(CUE, "Abe (Track 1).bi"), false);
	expect("unquoted name", cueReferences(CUE_NOQUOTE, "Tomb.bin"), true);
	expect("case insensitive", cueReferences(CUE_NOQUOTE, "TOMB.BIN"), true);
	expect("TRACK is not FILE", cueReferences(CUE, "01"), false);

	printf("\nhiding by stem (simple case, without reading the file)\n");
	expect("Tomb.bin hidden by Tomb.CUE", isCoveredTrack("Tomb.bin", dir, n), true);
	expect("Crash.chd never hidden", isCoveredTrack("Crash.chd", dir, n), false);
	expect("Abe.cue never hidden", isCoveredTrack("Abe.cue", dir, n), false);

	printf("\nrecognising a RetroArch core\n");
	expect("pcsx_rearmed_libretro.3dsx", isCoreFile("pcsx_rearmed_libretro.3dsx"), true);
	expect("snes9x2010_libretro.3dsx", isCoreFile("snes9x2010_libretro.3dsx"), true);
	expect("FBI.3dsx rejected", isCoreFile("FBI.3dsx"), false);
	expect("fwdgen.3dsx rejected", isCoreFile("fwdgen.3dsx"), false);
	expect("the core CIA rejected", isCoreFile("pcsx_rearmed_libretro.cia"), false);
	expect("bare suffix rejected", isCoreFile("_libretro.3dsx"), false);

	printf("\n%s\n", fails ? "SOME TESTS FAIL" : "all tests pass");
	return fails != 0;
}

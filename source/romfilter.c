#include <string.h>
#include <strings.h>

#include "romfilter.h"

// PlayStation extensions exactly as the pcsx_rearmed core declares them,
// rounded out with the main other systems.
static const char* const ROM_EXT[] = {
	".bin", ".cue", ".img", ".mdf", ".pbp", ".toc", ".cbn",
	".m3u", ".ccd", ".chd", ".iso", ".exe",
	".sfc", ".smc", ".nes", ".fds", ".gba", ".gb", ".gbc",
	".md", ".gen", ".smd", ".32x", ".sms", ".gg",
	".n64", ".z64", ".v64", ".pce", ".ngp", ".ws", ".wsc",
	".col", ".a26", ".lnx", ".zip", ".7z",
};

static bool extIn(const char* name, const char* const* list, size_t count)
{
	const char* dot = strrchr(name, '.');
	size_t i;
	if (!dot) return false;
	for (i = 0; i < count; i++)
		if (strcasecmp(dot, list[i]) == 0) return true;
	return false;
}

bool isRomFile(const char* name)
{
	return extIn(name, ROM_EXT, sizeof(ROM_EXT) / sizeof(ROM_EXT[0]));
}

bool isDescriptor(const char* name)
{
	static const char* const EXT[] = { ".cue", ".ccd", ".toc", ".m3u" };
	return extIn(name, EXT, 4);
}

bool isRawTrack(const char* name)
{
	static const char* const EXT[] = { ".bin", ".img" };
	return extIn(name, EXT, 2);
}

bool sameStem(const char* a, const char* b)
{
	const char* da = strrchr(a, '.');
	const char* db = strrchr(b, '.');
	size_t na = da ? (size_t)(da - a) : strlen(a);
	size_t nb = db ? (size_t)(db - b) : strlen(b);
	return na == nb && strncasecmp(a, b, na) == 0;
}

// A bin/cue game shows its raw tracks next to the descriptor. Picking a track
// instead of the .cue loses the audio tracks, and a multi-track game floods
// the listing. So any track already described is hidden.
bool isCoveredTrack(const char* name, const char* const* names, int count)
{
	int i;
	if (!isRawTrack(name)) return false;
	for (i = 0; i < count; i++)
		if (isDescriptor(names[i]) && sameStem(name, names[i])) return true;
	return false;
}

static const char* skipSpace(const char* p)
{
	while (*p == ' ' || *p == '\t') p++;
	return p;
}

bool cueReferences(const char* cue_text, const char* name)
{
	const char* p = cue_text;
	size_t want = strlen(name);

	while (*p)
	{
		const char* line = skipSpace(p);

		if (strncasecmp(line, "FILE", 4) == 0 && (line[4] == ' ' || line[4] == '\t'))
		{
			const char* f = skipSpace(line + 4);
			const char* end;

			if (*f == '"')
			{
				f++;
				end = strchr(f, '"');
			}
			else
			{
				end = f;
				while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') end++;
			}

			if (end && (size_t)(end - f) == want && strncasecmp(f, name, want) == 0)
				return true;
		}

		while (*p && *p != '\n') p++;
		if (*p) p++;
	}
	return false;
}

bool is3dsxFile(const char* name)
{
	static const char* const EXT[] = { ".3dsx" };
	return extIn(name, EXT, 1);
}

bool isCoreFile(const char* name)
{
	size_t n = strlen(name);
	static const char SUFFIX[] = "_libretro.3dsx";
	size_t s = sizeof(SUFFIX) - 1;
	return n > s && strcasecmp(name + n - s, SUFFIX) == 0;
}


// The libretro thumbnail repository replaces characters that are awkward in a
// file name with an underscore. Checked against its 9339 PlayStation entries:
// not one contains any of them. Without this, "Command & Conquer" is never
// found. Lives here rather than in fetch.c so it can be tested on a host.
void scrubThumbnailName(const char* src, char* dst, size_t dst_len)
{
	static const char INVALID[] = "&*/:`\"<>?\\|";
	size_t i;

	for (i = 0; src[i] && i + 1 < dst_len; i++)
		dst[i] = strchr(INVALID, src[i]) ? '_' : src[i];
	dst[i] = 0;
}

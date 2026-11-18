#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Title id allocation. Deliberately free of 3DS dependencies so it can be
// compiled and tested on a PC, like the patching routines.
//
// Forwarders live in the unique id range 0xFF000-0xFFFFF. That range is not
// reserved for this app: other tools (NDS forwarders, homebrew built with a
// default RSF) install titles there too. So an id is never taken on the
// strength of the hash alone: it must be free, or provably ours.

#define ID_UID_BASE  0xFF000u
#define ID_UID_MASK  0x00FFFu
#define ID_UID_COUNT 0x1000u

uint64_t idTitleForUid(uint32_t uid);
uint32_t idUidForTitle(uint64_t tid);
bool idInRange(uint64_t tid);

// The id the ROM path hashes to, before any collision is resolved.
uint64_t idPreferredTitle(const char* rom_path);

typedef bool (*id_taken_fn)(uint64_t tid, void* ctx);

// First title id at or after `start` (wrapping inside the range) for which
// taken() answers false. Returns 0 when all 4096 are taken.
uint64_t idPickFree(uint64_t start, id_taken_fn taken, void* ctx);

// The registry remembers which id each ROM received, one line per forwarder:
//   0004000000FF1C00|sdmc:/roms/psx/Medal of Honor (USA).chd
// so rebuilding a game updates its own forwarder, and an id once handed out
// is never given to another game.
bool idRegistryFind(const char* text, const char* rom_path, uint64_t* tid);
bool idRegistryHasTitle(const char* text, uint64_t tid);

// Formats a registry line, newline included. Returns the length written, or 0
// when it does not fit.
size_t idRegistryLine(char* out, size_t out_len, uint64_t tid, const char* rom_path);

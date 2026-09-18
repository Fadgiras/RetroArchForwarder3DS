#pragma once
#include <stdbool.h>
#include <stddef.h>

// Reading a disc's own identity, rather than trusting its file name.
//
// A file called "FF7.pbp" matches nothing in the thumbnail repository, and no
// amount of fuzzy name matching bridges that gap. The disc itself knows what
// it is:
//
//   pbp      PARAM.SFO holds DISC_ID and TITLE, in the clear, first kilobyte
//   bin/cue  track 1 holds SYSTEM.CNF with a "BOOT=cdrom:\SLES_000.49;1" line
//   chd      nothing readable: the data is LZMA compressed
//
// romfs:/db/<system>.txt then maps that serial to the canonical game name,
// the one the thumbnail repository files are named after. One index per
// system, built by tools/make_index.py.

#define SERIAL_LEN 16

// Reads the serial out of the ROM. Normalised as "SLES-00049".
bool metaSerial(const char* rom_path, char* out, size_t out_len);

// Looks the serial up in that system's index. A linear scan over half a
// megabyte, which is instant and needs no parser.
//
// Also used to locate the index file itself.
bool metaIndexPath(const char* system, char* out, size_t out_len);
bool metaNameForSerial(const char* system, const char* serial,
                       char* out, size_t out_len);

// The title a pbp declares for itself. Poorer than the canonical name (no
// region, no disc number) but it survives without the index.
bool metaPbpTitle(const char* rom_path, char* out, size_t out_len);

// Writes the icon a pbp carries to dest. Lets a forwarder keep artwork even
// with no network at all.
bool metaPbpIcon(const char* rom_path, const char* dest);

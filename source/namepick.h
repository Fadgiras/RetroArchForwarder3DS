#pragma once
#include <stdbool.h>
#include <stddef.h>

// Last resort when nothing identifies the game on its own.
//
// A chd keeps its data compressed, so its serial is out of reach, and a file
// called "FF7.pbp" matches no repository entry by name. Rather than give up,
// let the user type a few letters and pick the title from the same index the
// serial lookup uses - its names are the ones the artwork is filed under.
//
// Returns true and fills out with a canonical game name, false if cancelled.
bool namePick(const char* system, const char* suggestion,
              char* out, size_t out_len);

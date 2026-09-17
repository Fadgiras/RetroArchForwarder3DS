#pragma once
#include <stdbool.h>
#include <stddef.h>

// Core selection, without making the user browse when there is no need: a
// single core found is taken outright, several give a short list.
// Returns true and fills out_path (with the sdmc: prefix), false if the user
// backs out or no core is present.
bool coreSelect(char* out_path, size_t out_len);

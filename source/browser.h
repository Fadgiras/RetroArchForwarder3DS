#pragma once
#include <stdbool.h>
#include <stddef.h>

#define PATH_LEN 512
#define NAME_LEN 256

// Text file browser: returns true and fills out_path (without the sdmc:
// prefix) once a file is picked, false if the user backs out.
// pickCore filters on .3dsx files instead of ROMs.
bool browserRun(char* out_path, size_t out_len, bool pickCore);

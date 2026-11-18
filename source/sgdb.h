#pragma once
#include <stdbool.h>
#include <stddef.h>

// Banner logos from SteamGridDB (https://www.steamgriddb.com), section Logos:
// transparent PNGs that are composed over the game's artwork.
//
// Off unless the user puts an API key in sdmc:/forwarders/steamgriddb.txt.
// The key never leaves that file except in the Authorization header sent to
// the SteamGridDB API.
//
// SteamGridDB only accepts modern TLS (ECDHE ciphers, ECDSA certificates),
// which the console's own SSL service does not offer. So this goes through
// libcurl and mbedTLS from the devkitPro portlibs, checking certificates
// against the Mozilla bundle in romfs: verification stays on.

typedef enum
{
	SGDB_OK,          // logo on the card
	SGDB_OFF,         // no key file
	SGDB_BADKEY,      // key file present but unreadable, or rejected (401/403)
	SGDB_NONET,       // network or TLS failure
	SGDB_NOGAME,      // no game picked
	SGDB_NOLOGO,      // the game has no PNG logo
} sgdb_result;

// Finds the logo for a game named like the libretro databases do
// ("Silent Hill (USA)"), asking the user to pick when the search is
// ambiguous. Logos are cached in sdmc:/forwarders/logos/.
sgdb_result sgdbFetchLogo(const char* game_name, char* out_path, size_t out_len);

const char* sgdbDescribe(sgdb_result r);

// Releases the network stack, if it was started.
void sgdbExit(void);

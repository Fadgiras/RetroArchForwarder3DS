#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <curl/curl.h>
#include <jansson.h>

#include "sgdb.h"
#include "sgdbterm.h"
#include "romfilter.h"
#include "ui.h"

#define KEY_PATH "sdmc:/forwarders/steamgriddb.txt"
#define LOGO_DIR "sdmc:/forwarders/logos"
#define API "https://www.steamgriddb.com/api/v2"
#define CA_BUNDLE "romfs:/cacert.pem"
#define USER_AGENT "fwdgen/1.1 (3DS RetroArch forwarder generator)"

#define SOC_SIZE 0x100000
#define MAX_BODY (8 * 1024 * 1024)
#define MAX_GAMES 10
#define NAME_LEN 96

// A logo larger than this is fetched as its thumbnail instead: decoding a
// 4K PNG would not fit in memory, and the banner is 256 pixels wide anyway.
#define MAX_PIXELS (1024 * 1024)

static u32* s_soc;
static bool s_net;
static char s_key[128];
static char s_auth[160];

typedef struct { char* data; size_t len; } body_t;

typedef struct
{
	int id;
	char name[NAME_LEN];
	int year;
} game_t;

static game_t s_games[MAX_GAMES];
static int s_gameCount;

// ---------------------------------------------------------------- plumbing

static sgdb_result loadKey(void)
{
	char text[256];
	size_t n;
	FILE* f = fopen(KEY_PATH, "rb");

	if (!f) return SGDB_OFF;
	n = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[n] = 0;
	if (!sgdbParseKey(text, s_key, sizeof(s_key))) return SGDB_BADKEY;
	snprintf(s_auth, sizeof(s_auth), "Authorization: Bearer %s", s_key);
	return SGDB_OK;
}

static bool netUp(void)
{
	if (s_net) return true;
	s_soc = memalign(0x1000, SOC_SIZE);
	if (!s_soc) return false;
	if (R_FAILED(socInit(s_soc, SOC_SIZE)))
	{
		free(s_soc);
		s_soc = NULL;
		return false;
	}
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
	{
		socExit();
		free(s_soc);
		s_soc = NULL;
		return false;
	}
	s_net = true;
	return true;
}

void sgdbExit(void)
{
	if (!s_net) return;
	curl_global_cleanup();
	socExit();
	free(s_soc);
	s_soc = NULL;
	s_net = false;
}

static size_t onData(char* p, size_t size, size_t n, void* ud)
{
	body_t* b = (body_t*)ud;
	size_t k = size * n;
	char* grown;

	if (b->len + k > MAX_BODY) return 0;
	grown = realloc(b->data, b->len + k + 1);
	if (!grown) return 0;
	b->data = grown;
	memcpy(b->data + b->len, p, k);
	b->len += k;
	b->data[b->len] = 0;
	return k;
}

// GET into memory. Returns the HTTP status, or -1 when the request never got
// an answer (network, DNS, TLS). The key is only sent when auth is set, and
// curl drops it on a redirect to another host.
static long httpGet(const char* url, bool auth, body_t* out)
{
	CURL* c = curl_easy_init();
	struct curl_slist* headers = NULL;
	long status = -1;
	CURLcode rc;

	out->data = NULL;
	out->len = 0;
	if (!c) return -1;

	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 4L);
	curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
	if (auth)
	{
		headers = curl_slist_append(headers, s_auth);
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
	}

	rc = curl_easy_perform(c);
	if (rc == CURLE_OK)
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);

	curl_slist_free_all(headers);
	curl_easy_cleanup(c);
	if (rc != CURLE_OK)
	{
		free(out->data);
		out->data = NULL;
		out->len = 0;
	}
	return status;
}

// API calls answer {"success": true, "data": ...}.
static sgdb_result apiGet(const char* url, json_t** root, json_t** data)
{
	body_t body;
	json_error_t err;
	long status = httpGet(url, true, &body);

	*root = NULL;
	*data = NULL;
	if (status < 0) return SGDB_NONET;
	if (status == 401 || status == 403) { free(body.data); return SGDB_BADKEY; }
	if (status == 404) { free(body.data); return SGDB_NOGAME; }
	if (status != 200 || !body.data) { free(body.data); return SGDB_NONET; }

	*root = json_loadb(body.data, body.len, 0, &err);
	free(body.data);
	if (!*root) return SGDB_NONET;
	if (!json_is_true(json_object_get(*root, "success"))
	    || !json_is_array(*data = json_object_get(*root, "data")))
	{
		json_decref(*root);
		*root = NULL;
		return SGDB_NONET;
	}
	return SGDB_OK;
}

// ---------------------------------------------------------------- search

static sgdb_result search(const char* term)
{
	char enc[256], url[400];
	json_t *root, *data, *item;
	size_t i;
	sgdb_result r;

	s_gameCount = 0;
	sgdbUrlEncode(term, enc, sizeof(enc));
	snprintf(url, sizeof(url), API "/search/autocomplete/%s", enc);
	r = apiGet(url, &root, &data);
	if (r == SGDB_NOGAME) return SGDB_OK;   // nothing found is an empty list
	if (r != SGDB_OK) return r;

	json_array_foreach(data, i, item)
	{
		json_t* id = json_object_get(item, "id");
		json_t* name = json_object_get(item, "name");
		json_t* date = json_object_get(item, "release_date");
		game_t* g;

		if (s_gameCount >= MAX_GAMES) break;
		if (!json_is_integer(id) || !json_is_string(name)) continue;
		g = &s_games[s_gameCount++];
		g->id = (int)json_integer_value(id);
		snprintf(g->name, sizeof(g->name), "%s", json_string_value(name));
		g->year = 0;
		if (json_is_integer(date))
		{
			time_t t = (time_t)json_integer_value(date);
			struct tm* tm = gmtime(&t);
			if (tm) g->year = tm->tm_year + 1900;
		}
	}
	json_decref(root);
	return SGDB_OK;
}

static bool askTerm(const char* initial, char* out, size_t out_len)
{
	static SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, (int)out_len - 1);
	swkbdSetHintText(&kb, "Game title on SteamGridDB");
	swkbdSetInitialText(&kb, initial);
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, "Search", true);
	return swkbdInputText(&kb, out, out_len) == SWKBD_BUTTON_CONFIRM;
}

static void drawPicker(const char* term, int sel)
{
	int i;

	uiSelectBottom();
	uiClear();
	printf(UI_TITLE "SteamGridDB logo" UI_OFF "\n");
	printf(UI_DIM "%.39s" UI_OFF "\n\n", term);
	if (!s_gameCount)
		printf("  " UI_DIM "no match" UI_OFF "\n");
	for (i = 0; i < s_gameCount; i++)
	{
		char line[40];
		if (s_games[i].year)
			snprintf(line, sizeof(line), "%.32s %d", s_games[i].name, s_games[i].year);
		else
			snprintf(line, sizeof(line), "%.39s", s_games[i].name);
		printf("%s%-39.39s%s\n", i == sel ? UI_SEL : "", line, i == sel ? UI_OFF : "");
	}
	printf("\x1b[24;0H" UI_TITLE "Controls" UI_OFF "\n");
	printf("  A  use this game\n");
	printf("  X  search again\n");
	printf("  B  skip, no logo\n");
	uiSelectTop();
}

// The first hit is taken when its title is the one searched for; anything
// less certain is the user's call, since a wrong logo is worse than none.
static int pickGame(const char* term_in, sgdb_result* err)
{
	char term[64];
	int sel = 0, i;

	*err = SGDB_OK;
	snprintf(term, sizeof(term), "%s", term_in);

	if ((*err = search(term)) != SGDB_OK) return -1;
	if (!s_gameCount)
	{
		char shorter[64];
		if (sgdbMainTitle(term, shorter, sizeof(shorter)))
		{
			snprintf(term, sizeof(term), "%s", shorter);
			if ((*err = search(term)) != SGDB_OK) return -1;
		}
	}
	for (i = 0; i < s_gameCount; i++)
		if (sgdbSameTitle(s_games[i].name, term_in) || sgdbSameTitle(s_games[i].name, term))
			return s_games[i].id;

	drawPicker(term, sel);
	while (aptMainLoop())
	{
		u32 down;
		hidScanInput();
		down = hidKeysDown();

		if ((down & KEY_DOWN) && s_gameCount) { sel = (sel + 1) % s_gameCount; drawPicker(term, sel); }
		else if ((down & KEY_UP) && s_gameCount) { sel = (sel + s_gameCount - 1) % s_gameCount; drawPicker(term, sel); }
		else if ((down & KEY_A) && s_gameCount) { uiSelectBottom(); uiClear(); uiSelectTop(); return s_games[sel].id; }
		else if (down & KEY_X)
		{
			char again[64];
			if (askTerm(term, again, sizeof(again)))
			{
				snprintf(term, sizeof(term), "%s", again);
				if ((*err = search(term)) != SGDB_OK) break;
				sel = 0;
			}
			drawPicker(term, sel);
		}
		else if (down & KEY_B) break;
		uiFlush();
	}
	uiSelectBottom();
	uiClear();
	uiSelectTop();
	if (*err == SGDB_OK) *err = SGDB_NOGAME;
	return -1;
}

// ---------------------------------------------------------------- logos

static sgdb_result downloadLogo(int game_id, const char* dest)
{
	char url[256];
	json_t *root, *data, *item;
	const char* pick = NULL;
	size_t i;
	sgdb_result r;
	body_t img;
	long status;
	FILE* f;

	snprintf(url, sizeof(url), API "/logos/game/%d?types=static&mimes=image/png&nsfw=false&humor=false", game_id);
	r = apiGet(url, &root, &data);
	if (r == SGDB_NOGAME) return SGDB_NOLOGO;
	if (r != SGDB_OK) return r;

	// Best scored first, as the site sorts them.
	json_array_foreach(data, i, item)
	{
		const char* mime = json_string_value(json_object_get(item, "mime"));
		const char* full = json_string_value(json_object_get(item, "url"));
		const char* thumb = json_string_value(json_object_get(item, "thumb"));
		json_int_t w = json_integer_value(json_object_get(item, "width"));
		json_int_t h = json_integer_value(json_object_get(item, "height"));

		if (!mime || strcmp(mime, "image/png") != 0 || !full) continue;
		pick = (w * h > MAX_PIXELS && thumb) ? thumb : full;
		break;
	}
	if (!pick) { json_decref(root); return SGDB_NOLOGO; }

	snprintf(url, sizeof(url), "%s", pick);
	json_decref(root);

	// The CDN gets no key.
	status = httpGet(url, false, &img);
	if (status != 200 || img.len < 8 || memcmp(img.data, "\x89PNG", 4) != 0)
	{
		free(img.data);
		return status < 0 ? SGDB_NONET : SGDB_NOLOGO;
	}

	mkdir("sdmc:/forwarders", 0777);
	mkdir(LOGO_DIR, 0777);
	f = fopen(dest, "wb");
	if (!f) { free(img.data); return SGDB_NONET; }
	fwrite(img.data, 1, img.len, f);
	fclose(f);
	free(img.data);
	return SGDB_OK;
}

sgdb_result sgdbFetchLogo(const char* game_name, char* out_path, size_t out_len)
{
	char scrubbed[200], term[64];
	FILE* f;
	sgdb_result r;
	int id;

	scrubThumbnailName(game_name, scrubbed, sizeof(scrubbed));
	snprintf(out_path, out_len, LOGO_DIR "/%s.png", scrubbed);

	// Cached from an earlier build: no key, no network needed. Delete the
	// file to fetch it again.
	if ((f = fopen(out_path, "rb"))) { fclose(f); return SGDB_OK; }

	if ((r = loadKey()) != SGDB_OK) return r;
	if (!netUp()) return SGDB_NONET;

	sgdbCleanTerm(game_name, term, sizeof(term));
	if (!term[0]) return SGDB_NOGAME;

	printf("  logo     : searching SteamGridDB...\n");
	id = pickGame(term, &r);
	if (id < 0) return r;
	return downloadLogo(id, out_path);
}

const char* sgdbDescribe(sgdb_result r)
{
	switch (r)
	{
		case SGDB_OK:     return "SteamGridDB logo";
		case SGDB_OFF:    return "no SteamGridDB key";
		case SGDB_BADKEY: return "SteamGridDB key rejected";
		case SGDB_NONET:  return "SteamGridDB unreachable";
		case SGDB_NOGAME: return "no SteamGridDB game picked";
		case SGDB_NOLOGO: return "no PNG logo on SteamGridDB";
	}
	return "?";
}

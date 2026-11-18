#include <ctype.h>
#include <string.h>

#include "sgdbterm.h"

void sgdbCleanTerm(const char* name, char* out, size_t out_len)
{
	size_t n = 0;
	int depth = 0;
	bool space = false;

	if (!out_len) return;
	for (; *name; name++)
	{
		char c = *name;
		if (c == '(' || c == '[') { depth++; continue; }
		if ((c == ')' || c == ']') && depth) { depth--; continue; }
		if (depth) continue;
		if (c == ' ' || c == '\t' || c == '_')
		{
			space = n > 0;
			continue;
		}
		if (space && n + 1 < out_len) out[n++] = ' ';
		space = false;
		if (n + 1 < out_len) out[n++] = c;
	}
	// A trailing separator left behind by a dropped group: "Game -".
	while (n && (out[n - 1] == '-' || out[n - 1] == ' ')) n--;
	out[n] = 0;
}

bool sgdbMainTitle(const char* term, char* out, size_t out_len)
{
	const char* sep = strstr(term, " - ");
	size_t n;

	if (!sep || sep == term) return false;
	n = (size_t)(sep - term);
	if (n >= out_len) n = out_len - 1;
	memcpy(out, term, n);
	out[n] = 0;
	return true;
}

static const char* nextAlnum(const char* p)
{
	while (*p && !isalnum((unsigned char)*p)) p++;
	return p;
}

bool sgdbSameTitle(const char* a, const char* b)
{
	a = nextAlnum(a);
	b = nextAlnum(b);
	while (*a && *b)
	{
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		a = nextAlnum(a + 1);
		b = nextAlnum(b + 1);
	}
	return !*a && !*b;
}

void sgdbUrlEncode(const char* src, char* dst, size_t dst_len)
{
	static const char HEX[] = "0123456789ABCDEF";
	size_t out = 0;

	if (!dst_len) return;
	for (; *src && out + 4 < dst_len; src++)
	{
		unsigned char c = (unsigned char)*src;
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			dst[out++] = (char)c;
		else
		{
			dst[out++] = '%';
			dst[out++] = HEX[c >> 4];
			dst[out++] = HEX[c & 0xF];
		}
	}
	dst[out] = 0;
}

bool sgdbParseKey(const char* text, char* out, size_t out_len)
{
	size_t n = 0;

	while (*text && isspace((unsigned char)*text)) text++;
	while (text[n] && !isspace((unsigned char)text[n]))
	{
		if (!isalnum((unsigned char)text[n])) return false;
		n++;
	}
	// Whatever follows the key on its line or after it must be blank.
	{
		const char* rest = text + n;
		while (*rest && isspace((unsigned char)*rest)) rest++;
		if (*rest) return false;
	}
	if (n < 16 || n + 1 > out_len) return false;
	memcpy(out, text, n);
	out[n] = 0;
	return true;
}

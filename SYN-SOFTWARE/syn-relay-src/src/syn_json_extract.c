/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-CLIENT (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_json_extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Finds `"key":` anywhere in json and returns a pointer just past the
 * colon, or NULL if not found. Matches the field regardless of where it
 * sits (top-level or inside a nested {..} block), which is fine given
 * this repo's stats-server replies never reuse a key name across
 * nesting levels. */
static const char *find_value_start(const char *json, const char *key) {
	char needle[128];
	snprintf(needle, sizeof(needle), "\"%s\":", key);
	const char *p = strstr(json, needle);
	if (!p) {
		return NULL;
	}
	return p + strlen(needle);
}

bool syn_json_number(const char *json, const char *key, double *out) {
	const char *v = find_value_start(json, key);
	if (!v) {
		return false;
	}
	if (strncmp(v, "null", 4) == 0) {
		return false;
	}
	char *end;
	double val = strtod(v, &end);
	if (end == v) {
		return false;
	}
	*out = val;
	return true;
}

bool syn_json_string(const char *json, const char *key, char *out, size_t out_size) {
	const char *v = find_value_start(json, key);
	if (!v || *v != '"') {
		return false;
	}
	v++;
	size_t i = 0;
	while (*v && *v != '"' && i < out_size - 1) {
		if (*v == '\\' && *(v + 1)) {
			v++;
		}
		out[i++] = *v++;
	}
	out[i] = '\0';
	return true;
}

bool syn_json_bool(const char *json, const char *key, bool *out) {
	const char *v = find_value_start(json, key);
	if (!v) {
		return false;
	}
	if (strncmp(v, "true", 4) == 0) {
		*out = true;
		return true;
	}
	if (strncmp(v, "false", 5) == 0) {
		*out = false;
		return true;
	}
	return false;
}

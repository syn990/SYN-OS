/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_wallgen_theme.h"

#include <stdio.h>
#include <string.h>

const char *syn_wg_theme_get(const syn_wg_theme_vals *t, const char *key, const char *def) {
	for (int i = 0; i < t->n; i++) {
		if (strcmp(t->kv[i].key, key) == 0) {
			return t->kv[i].val;
		}
	}
	return def;
}

int syn_wg_parse_theme(const char *path, syn_wg_theme_vals *out) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return -1;
	}
	out->n = 0;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq) {
			continue;
		}
		char *q1 = strchr(eq, '"');
		if (!q1) {
			continue;
		}
		char *q2 = strchr(q1 + 1, '"');
		if (!q2) {
			continue;
		}
		*eq = '\0';
		char *key = line;
		while (*key == ' ' || *key == '\t') {
			key++;
		}
		if (strncmp(key, "SYN_", 4) != 0) {
			continue;
		}
		if (out->n >= SYN_WG_MAX_KV) {
			break;
		}
		size_t klen = strlen(key);
		if (klen >= sizeof(out->kv[0].key)) {
			continue; /* not a real SYN_* key this project uses */
		}
		syn_wg_kv *kv = &out->kv[out->n];
		memcpy(kv->key, key, klen + 1); /* +1 for the '\0' key already has */
		size_t vlen = (size_t)(q2 - q1 - 1);
		if (vlen >= sizeof(kv->val)) {
			vlen = sizeof(kv->val) - 1;
		}
		memcpy(kv->val, q1 + 1, vlen);
		kv->val[vlen] = '\0';
		out->n++;
	}
	fclose(f);
	return 0;
}

static int hexval(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return 0;
}

syn_wg_rgb syn_wg_hexrgb(const char *hex) {
	if (*hex == '#') {
		hex++;
	}
	syn_wg_rgb c;
	c.r = hexval(hex[0]) * 16 + hexval(hex[1]);
	c.g = hexval(hex[2]) * 16 + hexval(hex[3]);
	c.b = hexval(hex[4]) * 16 + hexval(hex[5]);
	return c;
}

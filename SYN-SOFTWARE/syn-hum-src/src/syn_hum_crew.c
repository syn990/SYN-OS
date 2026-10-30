/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _GNU_SOURCE /* st_mtim */
#include "syn_hum_crew.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void crew_path(char *out, size_t len) {
	const char *dir = getenv("XDG_RUNTIME_DIR");
	snprintf(out, len, "%s/syn-crew.state", dir ? dir : "/tmp");
}

static char state_letter(const char *word) {
	if (!strcmp(word, "working")) return 'w';
	if (!strcmp(word, "waiting")) return 'h';
	if (!strcmp(word, "talking")) return 't';
	return 'i';
}

int syn_hum_crew_read(syn_hum_crew *c, const syn_hum_conf *conf) {
	char path[PATH_MAX];
	crew_path(path, sizeof(path));
	struct stat sb;
	int there = stat(path, &sb) == 0 && time(NULL) - sb.st_mtime <= SYN_HUM_CREW_STALE && conf->crew;
	if (!there) {
		int was = c->count || c->talk;
		c->count = c->talk = 0;
		c->seen.tv_sec = c->seen.tv_nsec = 0;
		return was;
	}
	if (sb.st_mtim.tv_sec == c->seen.tv_sec && sb.st_mtim.tv_nsec == c->seen.tv_nsec) return 0;
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	syn_hum_crew next;
	memset(&next, 0, sizeof(next));
	next.seen = sb.st_mtim;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char key[16], id[16], word[16];
		int n = 0, p = 0;
		if (line[0] == '#') continue;
		if (sscanf(line, "%15s %d", key, &n) == 2 && !strcmp(key, "talk")) {
			next.talk = n > 0;
		} else if (sscanf(line, "%15s %15s %15s %d", key, id, word, &p) >= 3 && !strcmp(key, "crew")) {
			if (next.count >= SYN_HUM_CREW_MAX || syn_hum_conf_muted(conf, id)) continue;
			snprintf(next.member[next.count].id, sizeof(next.member[0].id), "%s", id);
			next.member[next.count].state = state_letter(word);
			next.member[next.count].priority = p >= 0 && p <= 5 ? p : 0;
			next.count++;
		}
	}
	fclose(f);
	int changed = memcmp(&next.member, &c->member, sizeof(next.member)) || next.count != c->count || next.talk != c->talk;
	*c = next;
	return changed;
}

int syn_hum_crew_keys(const syn_hum_crew *c, char *out, size_t len) {
	int n = snprintf(out, len, " talk=%d", c->talk);
	if (c->count && n >= 0 && (size_t)n < len) n += snprintf(out + n, len - (size_t)n, " crew=");
	for (int i = 0; i < c->count && n >= 0 && (size_t)n < len; i++) {
		n += snprintf(out + n, len - (size_t)n, "%s%s:%c%d", i ? "," : "", c->member[i].id, c->member[i].state,
		              c->member[i].priority);
	}
	if (n < 0 || (size_t)n >= len) return -1;
	return n;
}

void syn_hum_crew_words(const syn_hum_crew *c, char *out, size_t len) {
	size_t n = 0;
	out[0] = '\0';
	for (int i = 0; i < c->count && n < len; i++) {
		const char *word = c->member[i].state == 'w' ? "working" : c->member[i].state == 'h' ? "waiting"
		                 : c->member[i].state == 't' ? "talking" : "idle";
		int k = c->member[i].priority ? snprintf(out + n, len - n, "%s%s %s P%d", n ? ", " : "", c->member[i].id, word, c->member[i].priority)
		                              : snprintf(out + n, len - n, "%s%s %s", n ? ", " : "", c->member[i].id, word);
		if (k < 0) break;
		n += (size_t)k;
	}
	if (c->talk && n < len) snprintf(out + n, len - n, "%s(someone talking)", n ? "; " : "");
}

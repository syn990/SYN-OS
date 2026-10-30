/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_hum_conf.h"
#include "syn_hum_styles.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void conf_dir(char *out, size_t len) {
	const char *config = getenv("XDG_CONFIG_HOME");
	if (config && *config) {
		snprintf(out, len, "%s/syn-os", config);
	} else {
		snprintf(out, len, "%s/.config/syn-os", getenv("HOME") ? getenv("HOME") : "");
	}
}

double syn_hum_volume_gain(const char *word) {
	if (!strcmp(word, "quiet")) return 0.5;
	if (!strcmp(word, "normal")) return 1.0;
	if (!strcmp(word, "loud")) return 2.0;
	return -1.0;
}

int syn_hum_conf_muted(const syn_hum_conf *c, const char *id) {
	for (int i = 0; i < c->mute_count; i++) {
		if (!strcmp(c->mute[i], id)) return 1;
	}
	return 0;
}

int syn_hum_conf_mute(syn_hum_conf *c, const char *id, int on) {
	for (int i = 0; i < c->mute_count; i++) {
		if (strcmp(c->mute[i], id)) continue;
		if (on) {
			memmove(c->mute[i], c->mute[i + 1], sizeof(c->mute[0]) * (size_t)(c->mute_count - i - 1));
			c->mute_count--;
		}
		return 0;
	}
	if (on) return 0;
	if (c->mute_count >= SYN_HUM_MUTE_MAX) return -1;
	snprintf(c->mute[c->mute_count++], sizeof(c->mute[0]), "%s", id);
	return 0;
}

void syn_hum_conf_load(syn_hum_conf *c) {
	c->style = SYN_HUM_UPLINK;
	snprintf(c->volume, sizeof(c->volume), "normal");
	c->crew = 1;
	c->mute_count = 0;

	char dir[PATH_MAX - 16], path[PATH_MAX];
	conf_dir(dir, sizeof(dir));
	snprintf(path, sizeof(path), "%s/hum.conf", dir);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char key[32], value[16];
		if (line[0] == '#' || sscanf(line, "%31s %15s", key, value) != 2) continue;
		if (!strcmp(key, "style") && syn_hum_style_find(value) >= 0) {
			c->style = syn_hum_style_find(value);
		} else if (!strcmp(key, "volume") && syn_hum_volume_gain(value) > 0.0) {
			snprintf(c->volume, sizeof(c->volume), "%s", value);
		} else if (!strcmp(key, "crew")) {
			c->crew = strcmp(value, "off") != 0;
		} else if (!strcmp(key, "mute")) {
			syn_hum_conf_mute(c, value, 0);
		}
	}
	fclose(f);
}

int syn_hum_conf_save(const syn_hum_conf *c) {
	char dir[PATH_MAX - 16], path[PATH_MAX], tmp[PATH_MAX + 8];
	conf_dir(dir, sizeof(dir));
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
	snprintf(path, sizeof(path), "%s/hum.conf", dir);
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) return -1;
	fprintf(f,
	        "# syn-hum, the acoustic monitor: how it sounds. Set from the menu\n"
	        "# (Preferences > Desktop > Acoustic Monitor) or with `syn-hum set`.\n"
	        "# style: " SYN_HUM_STYLE_LIST "\n"
	        "# (`syn-hum styles` says what each plays). volume: quiet, normal or loud.\n"
	        "# crew: on or off, the crew's bed (techno); mute ID leaves one member out.\n"
	        "style %s\n"
	        "volume %s\n"
	        "crew %s\n",
	        SYN_HUM_STYLES[c->style].name, c->volume, c->crew ? "on" : "off");
	for (int i = 0; i < c->mute_count; i++) fprintf(f, "mute %s\n", c->mute[i]);
	if (fclose(f) != 0) {
		unlink(tmp);
		return -1;
	}
	return rename(tmp, path);
}

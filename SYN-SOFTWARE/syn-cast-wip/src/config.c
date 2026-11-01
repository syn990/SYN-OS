/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "config.h"
#include "util.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *config_default_path(char *out, int cap) {
	const char *env = getenv("SYN_CAST_CONFIG");
	if (env && *env) {
		snprintf(out, (size_t)cap, "%s", env);
		return out;
	}
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) {
		snprintf(out, (size_t)cap, "%s/syn-cast/config.json", xdg);
	} else {
		snprintf(out, (size_t)cap, "%s/.config/syn-cast/config.json", getenv("HOME") ? getenv("HOME") : ".");
	}
	return out;
}

static void defaults(config *c) {
	memset(c, 0, sizeof *c);
	str_copy(c->server, sizeof c->server, "wss://zello.io/ws");
	c->auto_play = true;
	c->noise_suppression = true;
	c->vox_threshold = 0.85;
	c->max_talk_seconds = 300;
	c->notify = true;
	c->tones = true;
	c->history_days = 30;
	c->history_mb = 512;
	str_copy(c->font, sizeof c->font, "Terminus:size=14");
	c->width = 980;
	c->height = 640;
	const char *data = getenv("SYN_CAST_DATA");
	const char *xdg = getenv("XDG_DATA_HOME");
	if (data && *data) {
		str_copy(c->data_dir, sizeof c->data_dir, data);
	} else if (xdg && *xdg) {
		snprintf(c->data_dir, sizeof c->data_dir, "%s/syn-cast", xdg);
	} else {
		snprintf(c->data_dir, sizeof c->data_dir, "%s/.local/share/syn-cast", getenv("HOME") ? getenv("HOME") : ".");
	}
}

static void s(json_t *o, const char *key, char *dst, size_t cap) {
	json_t *v = json_object_get(o, key);
	if (json_is_string(v)) {
		str_copy(dst, cap, json_string_value(v));
	}
}

static void b(json_t *o, const char *key, bool *dst) {
	json_t *v = json_object_get(o, key);
	if (json_is_boolean(v)) {
		*dst = json_is_true(v);
	}
}

static void n(json_t *o, const char *key, double *dst) {
	json_t *v = json_object_get(o, key);
	if (json_is_number(v)) {
		*dst = json_number_value(v);
	}
}

static void i(json_t *o, const char *key, int *dst) {
	json_t *v = json_object_get(o, key);
	if (json_is_number(v)) {
		*dst = (int)json_number_value(v);
	}
}

int config_load(config *c, char *err, int errn) {
	defaults(c);
	config_default_path(c->path, sizeof c->path);
	if (access(c->path, F_OK) != 0) {
		return 1;
	}
	json_error_t jerr;
	json_t *o = json_load_file(c->path, 0, &jerr);
	if (!json_is_object(o)) {
		snprintf(err, (size_t)errn, "%s:%d: %s", c->path, jerr.line, jerr.text);
		json_decref(o);
		return -1;
	}
	struct stat st;
	if (stat(c->path, &st) == 0 && (st.st_mode & 077)) {
		LOG("%s is readable by others (mode %o); it holds a password, chmod 600 it",
			c->path, st.st_mode & 0777);
	}

	s(o, "server", c->server, sizeof c->server);
	s(o, "username", c->username, sizeof c->username);
	s(o, "password", c->password, sizeof c->password);
	s(o, "issuer", c->issuer, sizeof c->issuer);
	char key[1024] = "";
	s(o, "key", key, sizeof key);
	if (key[0]) {
		expand_home(key, c->key_path, sizeof c->key_path);
	}
	s(o, "auth_token", c->auth_token, sizeof c->auth_token);

	json_t *chans = json_object_get(o, "channels");
	if (json_is_array(chans)) {
		size_t k;
		json_t *v;
		json_array_foreach(chans, k, v) {
			if (json_is_string(v) && c->n_channels < CFG_MAX_CHANNELS) {
				str_copy(c->channels[c->n_channels++], sizeof c->channels[0], json_string_value(v));
			}
		}
	}
	char one[128] = "";
	s(o, "channel", one, sizeof one);
	if (one[0] && c->n_channels == 0) {
		str_copy(c->channels[c->n_channels++], sizeof c->channels[0], one);
	}

	char mode[16] = "";
	s(o, "ptt_mode", mode, sizeof mode);
	c->ptt_toggle = strcmp(mode, "toggle") == 0;
	b(o, "auto_play", &c->auto_play);
	b(o, "noise_suppression", &c->noise_suppression);
	b(o, "vox", &c->vox);
	n(o, "vox_threshold", &c->vox_threshold);
	n(o, "mic_gain_db", &c->mic_gain_db);
	n(o, "play_gain_db", &c->play_gain_db);
	i(o, "max_talk_seconds", &c->max_talk_seconds);
	b(o, "transcriptions", &c->transcriptions);
	b(o, "notify", &c->notify);
	b(o, "tones", &c->tones);
	i(o, "history_days", &c->history_days);
	i(o, "history_mb", &c->history_mb);
	s(o, "font", c->font, sizeof c->font);
	i(o, "width", &c->width);
	i(o, "height", &c->height);
	char data[1024] = "";
	s(o, "data_dir", data, sizeof data);
	if (data[0] && !getenv("SYN_CAST_DATA")) {
		expand_home(data, c->data_dir, sizeof c->data_dir);
	}
	json_decref(o);

	if (c->max_talk_seconds < 5) c->max_talk_seconds = 5;
	if (c->vox_threshold <= 0 || c->vox_threshold >= 1) c->vox_threshold = 0.85;
	if (c->width < 480) c->width = 480;
	if (c->height < 360) c->height = 360;
	return 0;
}

const char *config_bot_username(char *out, int cap) {
	char path[1024];
	expand_home("~/.config/syn-caster/crew/config.json", path, sizeof path);
	out[0] = 0;
	json_error_t jerr;
	json_t *o = json_load_file(path, 0, &jerr);
	json_t *u = json_object_get(o, "username");
	if (json_is_string(u)) {
		str_copy(out, (size_t)cap, json_string_value(u));
	}
	json_decref(o);
	return out;
}

bool config_is_bot_account(const char *username) {
	char bot[128];
	config_bot_username(bot, sizeof bot);
	return bot[0] && username && strcasecmp(bot, username) == 0;
}

bool config_has_account(const config *c) {
	return c->n_channels > 0 && c->server[0] &&
		(c->auth_token[0] || (c->issuer[0] && c->key_path[0]));
}

int config_save_account(const config *c) {
	json_error_t jerr;
	json_t *o = json_load_file(c->path, 0, &jerr);
	if (!json_is_object(o)) {
		json_decref(o);
		o = json_object();
	}
	json_object_set_new(o, "server", json_string(c->server));
	json_object_set_new(o, "username", json_string(c->username));
	json_object_set_new(o, "password", json_string(c->password));
	if (c->issuer[0]) {
		json_object_set_new(o, "issuer", json_string(c->issuer));
	}
	if (c->key_path[0]) {
		json_object_set_new(o, "key", json_string(c->key_path));
	}
	json_t *arr = json_array();
	for (int k = 0; k < c->n_channels; k++) {
		json_array_append_new(arr, json_string(c->channels[k]));
	}
	json_object_set_new(o, "channels", arr);
	char *text = json_dumps(o, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
	json_decref(o);
	char dir[1024];
	str_copy(dir, sizeof dir, c->path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = 0;
		mkdir_p(dir, 0700);
	}
	size_t len = strlen(text);
	text = realloc(text, len + 2);
	text[len] = '\n';
	text[len + 1] = 0;
	int rc = write_file(c->path, text, len + 1, 0600);
	free(text);
	return rc;
}

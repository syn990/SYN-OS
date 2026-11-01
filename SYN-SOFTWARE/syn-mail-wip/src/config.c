/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* The accounts, in plain JSON. No secret is ever written here: passwords and tokens live in the keyring (secret.c). */
#include "config.h"
#include "util.h"

#include <errno.h>
#include <jansson.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char path_buf[PATH_MAX];

const char *config_path(void) {
	const char *e = getenv("SYN_MAIL_CONFIG");
	if (e && *e) {
		return e;
	}
	const char *x = getenv("XDG_CONFIG_HOME");
	if (x && *x) {
		snprintf(path_buf, sizeof path_buf, "%s/syn-mail/config.json", x);
	} else {
		snprintf(path_buf, sizeof path_buf, "%s/.config/syn-mail/config.json", getenv("HOME") ? getenv("HOME") : ".");
	}
	return path_buf;
}

static void jstr(json_t *o, const char *k, char *dst, size_t n) {
	json_t *v = json_object_get(o, k);
	str_copy(dst, n, json_is_string(v) ? json_string_value(v) : "");
}

int config_load(Config *c, char *err, size_t errn) {
	*c = (Config){0};
	const char *p = config_path();
	if (access(p, F_OK) != 0) {
		return 0;
	}
	json_error_t je;
	json_t *root = json_load_file(p, 0, &je);
	if (!root) {
		snprintf(err, errn, "%s:%d: %s", p, je.line, je.text);
		return -1;
	}
	json_t *arr = json_object_get(root, "accounts");
	size_t i;
	json_t *o;
	if (json_is_array(arr)) {
		c->acc = calloc(json_array_size(arr) + 1, sizeof *c->acc);
		json_array_foreach(arr, i, o) {
			Account *a = &c->acc[c->n++];
			jstr(o, "name", a->name, sizeof a->name);
			jstr(o, "address", a->address, sizeof a->address);
			jstr(o, "display_name", a->display, sizeof a->display);
			jstr(o, "imap_host", a->imap_host, sizeof a->imap_host);
			jstr(o, "smtp_host", a->smtp_host, sizeof a->smtp_host);
			a->imap_port = (int)json_integer_value(json_object_get(o, "imap_port"));
			a->smtp_port = (int)json_integer_value(json_object_get(o, "smtp_port"));
			const char *au = json_string_value(json_object_get(o, "auth"));
			a->auth = au && strcmp(au, "oauth") == 0 ? AUTH_OAUTH : AUTH_PASSWORD;
		}
	}
	json_decref(root);
	return 0;
}

int config_save(const Config *c, char *err, size_t errn) {
	const char *p = config_path();
	char dir[PATH_MAX];
	str_copy(dir, sizeof dir, p);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = 0;
		if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
			snprintf(err, errn, "%s: %s", dir, strerror(errno));
			return -1;
		}
	}
	json_t *arr = json_array();
	for (int i = 0; i < c->n; i++) {
		const Account *a = &c->acc[i];
		json_array_append_new(arr, json_pack("{s:s, s:s, s:s, s:s, s:i, s:s, s:i, s:s}",
			"name", a->name, "address", a->address, "display_name", a->display,
			"imap_host", a->imap_host, "imap_port", a->imap_port,
			"smtp_host", a->smtp_host, "smtp_port", a->smtp_port,
			"auth", a->auth == AUTH_OAUTH ? "oauth" : "password"));
	}
	json_t *root = json_pack("{s:o}", "accounts", arr);
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.tmp", p);
	mode_t old = umask(077);
	int rc = json_dump_file(root, tmp, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
	umask(old);
	json_decref(root);
	if (rc != 0 || chmod(tmp, 0600) != 0 || rename(tmp, p) != 0) {
		snprintf(err, errn, "cannot write %s: %s", p, strerror(errno));
		unlink(tmp);
		return -1;
	}
	return 0;
}

Account *config_find(Config *c, const char *name) {
	for (int i = 0; i < c->n; i++) {
		if (strcmp(c->acc[i].name, name) == 0) {
			return &c->acc[i];
		}
	}
	return NULL;
}

int config_preset(Account *a, const char *preset) {
	if (strcmp(preset, "google") == 0) {
		/* Google refuses plain passwords since 14 March 2025: OAuth (XOAUTH2) or an app password */
		str_copy(a->imap_host, sizeof a->imap_host, "imap.gmail.com");
		str_copy(a->smtp_host, sizeof a->smtp_host, "smtp.gmail.com");
		a->auth = AUTH_OAUTH;
	} else if (strcmp(preset, "ionos") == 0) {
		str_copy(a->imap_host, sizeof a->imap_host, "imap.ionos.co.uk");
		str_copy(a->smtp_host, sizeof a->smtp_host, "smtp.ionos.co.uk");
		a->auth = AUTH_PASSWORD;
	} else {
		return -1;
	}
	a->imap_port = 993;
	a->smtp_port = 465;
	return 0;
}

void config_free(Config *c) {
	free(c->acc);
	*c = (Config){0};
}

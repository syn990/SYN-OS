/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-SERVER (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_apps.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* Strips a KEY=value line down to just `value`. Returns NULL if `line`
 * doesn't start with `key=` (same technique syn_theme.c uses for .theme
 * files — no double-quote unwrapping needed here, .desktop values aren't
 * quoted). */
static const char *extract_value(const char *line, const char *key, char *buf, size_t buflen) {
	size_t key_len = strlen(key);
	if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') {
		return NULL;
	}
	const char *v = line + key_len + 1;
	size_t i = 0;
	while (v[i] && v[i] != '\n' && v[i] != '\r' && i < buflen - 1) {
		buf[i] = v[i];
		i++;
	}
	buf[i] = '\0';
	return buf;
}

static bool parse_desktop_file(const char *path, syn_app_entry *out) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	snprintf(out->id, sizeof(out->id), "%s", path);

	bool no_display = false, hidden = false;
	char value[512];
	char line[1024];
	bool in_desktop_entry = false;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '[') {
			in_desktop_entry = strncmp(line, "[Desktop Entry]", 15) == 0;
			continue;
		}
		if (!in_desktop_entry) {
			continue;
		}

		const char *v;
		if ((v = extract_value(line, "Name", value, sizeof(value)))) {
			snprintf(out->name, sizeof(out->name), "%s", v);
		} else if ((v = extract_value(line, "Exec", value, sizeof(value)))) {
			snprintf(out->exec, sizeof(out->exec), "%s", v);
		} else if ((v = extract_value(line, "Icon", value, sizeof(value)))) {
			snprintf(out->icon, sizeof(out->icon), "%s", v);
		} else if ((v = extract_value(line, "NoDisplay", value, sizeof(value)))) {
			no_display = strcmp(v, "true") == 0;
		} else if ((v = extract_value(line, "Hidden", value, sizeof(value)))) {
			hidden = strcmp(v, "true") == 0;
		}
	}
	fclose(f);

	return out->name[0] && out->exec[0] && !no_display && !hidden;
}

static int scan_dir(const char *dir, syn_app_entry *out, int count, int max) {
	DIR *d = opendir(dir);
	if (!d) {
		return count;
	}

	struct dirent *ent;
	while (count < max && (ent = readdir(d))) {
		size_t nlen = strlen(ent->d_name);
		if (nlen < 9 || strcmp(ent->d_name + nlen - 8, ".desktop") != 0) {
			continue;
		}
		char path[600];
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		if (parse_desktop_file(path, &out[count])) {
			count++;
		}
	}
	closedir(d);
	return count;
}

int syn_apps_list(syn_app_entry *out, int max) {
	int count = 0;
	count = scan_dir("/usr/share/applications", out, count, max);

	const char *home = getenv("HOME");
	if (home) {
		char local_dir[512];
		snprintf(local_dir, sizeof(local_dir), "%s/.local/share/applications", home);
		count = scan_dir(local_dir, out, count, max);
	}
	return count;
}

void syn_apps_strip_field_codes(char *exec) {
	char cleaned[512];
	size_t j = 0;
	for (size_t i = 0; exec[i] && j < sizeof(cleaned) - 1; i++) {
		if (exec[i] == '%' && exec[i + 1] != '\0') {
			i++; /* skip the field-code letter too */
			continue;
		}
		cleaned[j++] = exec[i];
	}
	cleaned[j] = '\0';
	snprintf(exec, 512, "%s", cleaned);
}

bool syn_apps_launch(const char *id) {
	syn_app_entry entry;
	if (!parse_desktop_file(id, &entry)) {
		return false;
	}
	syn_apps_strip_field_codes(entry.exec);

	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		/* Detach: new session so the launched app outlives this
		 * request/response and isn't tied to the stats server role's lifetime. */
		setsid();
		execlp("/bin/sh", "sh", "-c", entry.exec, (char *)NULL);
		_exit(127);
	}
	return true;
}

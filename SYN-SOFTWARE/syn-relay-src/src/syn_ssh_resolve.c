/* ------------------------------------------------------------------------
 *   `ssh -G <host>` prints ssh_config's fully-merged option set after
 *   evaluating ~/.ssh/config + /etc/ssh/ssh_config for that host —
 *   Match blocks, Include, wildcard patterns, HostName overrides, all of
 *   it — without connecting to anything (documented OpenSSH behavior
 *   since 5.4, 2010). Shelling out to the real client here means this
 *   never has to re-implement ssh_config's grammar to figure out what
 *   e.g. `Host nas` / `HostName 192.168.1.50` actually resolves to —
 *   the exact case syn-relay's own getaddrinfo()-only resolution used to
 *   get wrong for any aliased/ProxyJump host.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_ssh_resolve.h"

#include <stdio.h>
#include <string.h>

/* Wraps `s` in single quotes for safe use inside a popen() shell
 * command line, escaping any single quote in `s` itself via the
 * standard '\''-close-quote-reopen trick. `host_alias` may originate
 * from user input (typed via the dialpad, or picked from a menu built
 * off ~/.ssh/config), so this can't be interpolated raw — that's
 * exactly the class of bug this whole redesign is fixing elsewhere
 * (unescaped host interpolated into a shell-executed pipe-menu command). */
static bool shell_quote(const char *s, char *out, size_t out_size) {
	size_t i = 0;
	if (i + 1 >= out_size) return false;
	out[i++] = '\'';
	for (const char *p = s; *p; p++) {
		if (*p == '\'') {
			if (i + 4 >= out_size) return false;
			out[i++] = '\'';
			out[i++] = '\\';
			out[i++] = '\'';
			out[i++] = '\'';
		} else {
			if (i + 1 >= out_size) return false;
			out[i++] = *p;
		}
	}
	if (i + 2 >= out_size) return false;
	out[i++] = '\'';
	out[i] = '\0';
	return true;
}

bool syn_ssh_resolve_hostname(const char *host_alias, char *resolved_out, size_t resolved_out_size) {
	resolved_out[0] = '\0';

	char quoted[512];
	if (!shell_quote(host_alias, quoted, sizeof(quoted))) {
		return false;
	}

	char cmd[600];
	int n = snprintf(cmd, sizeof(cmd), "ssh -G -- %s 2>/dev/null", quoted);
	if (n < 0 || (size_t)n >= sizeof(cmd)) {
		return false;
	}

	FILE *pipe = popen(cmd, "r");
	if (!pipe) {
		return false;
	}

	char line[512];
	bool found = false;
	while (fgets(line, sizeof(line), pipe)) {
		if (strncmp(line, "hostname ", 9) == 0) {
			size_t len = strlen(line);
			while (len > 9 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
				line[--len] = '\0';
			}
			snprintf(resolved_out, resolved_out_size, "%s", line + 9);
			found = true;
			break;
		}
	}
	pclose(pipe);
	return found && resolved_out[0];
}

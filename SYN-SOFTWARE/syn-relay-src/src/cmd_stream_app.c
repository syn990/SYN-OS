/* ------------------------------------------------------------------------
 *   Beams an app's window over via `waypipe ssh`, standalone — separate
 *   from LAUNCH_APP, which still runs an app headless on the remote.
 *
 *   ssh_host doubles as both syn-relay's stats-port target (:47991) and
 *   the SSH destination; only correct if that name resolves the same way
 *   for a plain TCP connect and for `ssh` itself (not guaranteed for
 *   jump-host/proxy setups).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "cmd_stream_app.h"
#include "cmd_stats_client.h"
#include "syn_apps.h"
#include "syn_relay_conn.h"
#include "syn_bar_notify.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* Digit 'D' (941/1633Hz) for a failed stream-app launch — syn-bar-core
 * never sees this event at all (it's not a relay-state-file transition,
 * just this one waypipe invocation succeeding or failing), so this asks
 * the bar to play it directly over its socket rather than syn-relay
 * linking any tone-synthesis code itself (see syn_bar_notify.h). No
 * success tone: a stream-app window simply appearing on screen already
 * confirms success, unlike a failure, which can otherwise pass silently
 * if the terminal that ran this isn't being watched. */
#define STREAM_APP_FAIL_DTMF_LOW 941.0
#define STREAM_APP_FAIL_DTMF_HIGH 1633.0
#define STREAM_APP_FAIL_DTMF_SECONDS 0.15

int cmd_stream_app(const char *id, const char *ssh_host) {
	char reply[8192];
	if (!syn_relay_request(ssh_host, "LIST_APPS", reply, sizeof(reply))) {
		fprintf(stderr, "syn-relay: %s unreachable on the stats port (:47991)\n", ssh_host);
		syn_bar_notify_tone_dtmf(STREAM_APP_FAIL_DTMF_LOW, STREAM_APP_FAIL_DTMF_HIGH, STREAM_APP_FAIL_DTMF_SECONDS);
		return 1;
	}

	static syn_app_entry apps[SYN_APPS_MAX];
	int count = syn_apps_parse_wire_reply(reply, apps, SYN_APPS_MAX);

	const syn_app_entry *match = NULL;
	for (int i = 0; i < count; i++) {
		if (strcmp(apps[i].id, id) == 0) {
			match = &apps[i];
			break;
		}
	}
	if (!match || !match->exec[0]) {
		fprintf(stderr, "syn-relay: app \"%s\" not found on %s\n", id, ssh_host);
		syn_bar_notify_tone_dtmf(STREAM_APP_FAIL_DTMF_LOW, STREAM_APP_FAIL_DTMF_HIGH, STREAM_APP_FAIL_DTMF_SECONDS);
		return 1;
	}

	char exec[sizeof(match->exec)];
	snprintf(exec, sizeof(exec), "%s", match->exec);
	syn_apps_strip_field_codes(exec);

	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}
	if (pid == 0) {
		execlp("waypipe", "waypipe", "ssh", ssh_host, exec, (char *)NULL);
		fprintf(stderr, "syn-relay: failed to exec waypipe (is it installed?)\n");
		_exit(127);
	}

	int status = 0;
	waitpid(pid, &status, 0);
	int rc = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	if (rc != 0) {
		syn_bar_notify_tone_dtmf(STREAM_APP_FAIL_DTMF_LOW, STREAM_APP_FAIL_DTMF_HIGH, STREAM_APP_FAIL_DTMF_SECONDS);
	}
	return rc;
}

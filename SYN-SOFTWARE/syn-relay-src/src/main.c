/* ------------------------------------------------------------------------
 *                              S Y N - R E L A Y
 *
 *   One tool for everything about connecting to and interacting with
 *   another SYN-OS machine: stats client, stats server, screen
 *   watch/control viewer, screen watch/control host. One binary, one
 *   PID/state model per role — a machine can be several of these roles
 *   at once (e.g. serving stats to one peer while being watched by
 *   another), each tracked independently (see syn_node_state.h and
 *   syn_agent_state.h).
 *
 *   No authentication on either the stats protocol or the video/input
 *   channels. Deliberate for v1 — trusted LAN only. See PROTOCOL.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "cmd_stats_client.h"
#include "cmd_stats_server.h"
#include "cmd_watch.h"
#include "cmd_host.h"
#include "cmd_stream_app.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s <flag> [arg]\n"
		"\n"
		"Stats client (connect out to another machine's stats server):\n"
		"  --activate              Enter ACTIVE state (waiting to connect)\n"
		"  --connect [host]        Connect (prompts via the dialpad if host omitted)\n"
		"  --disconnect            Return to ABSENT\n"
		"  --stat-cpu/-mem/-disk   waybar JSON stat modules\n"
		"  --stat-relay-status     waybar persistent connection-status JSON\n"
		"  --list-apps             Pipe-menu XML of the connected node's apps\n"
		"  --list-apps-for <host>  Same, for any SSH-reachable host, standalone\n"
		"  --launch <id>           Launch app <id> on the connected node (headless)\n"
		"  --stream-app <id> <ssh-host>\n"
		"                          Beam app <id> over as its own window here via\n"
		"                          waypipe, standalone (no --connect needed)\n"
		"\n"
		"Stats server (let other machines connect to this one):\n"
		"  --start                 Self-daemonize, serve stats on :47991\n"
		"  --stop                  Stop the running server\n"
		"  --status                Human-readable running state\n"
		"  --stat-serving          waybar JSON\n"
		"\n"
		"Screen watch (view/control a remote machine's screen):\n"
		"  --watch [ip]            Start watching (prompts via the dialpad if omitted)\n"
		"  --stop-watching         Stop the current watch session\n"
		"  --stat-watching         waybar JSON\n"
		"\n"
		"Screen host (let a remote machine view/control this one):\n"
		"  --host-watched [ip]     Start being watched (prompts via the dialpad if omitted;\n"
		"                          'host' here means this machine, not the remote one)\n"
		"  --stop-hosting          Stop being watched\n"
		"  --stat-agent            waybar JSON\n",
		argv0);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	const char *flag = argv[1];
	const char *arg = argc >= 3 ? argv[2] : NULL;

	/* ---- stats client role ---- */
	if (!strcmp(flag, "--activate")) return cmd_activate();
	if (!strcmp(flag, "--connect")) return cmd_connect(arg);
	if (!strcmp(flag, "--disconnect")) return cmd_disconnect();
	if (!strcmp(flag, "--stat-cpu")) return cmd_stat_cpu();
	if (!strcmp(flag, "--stat-mem")) return cmd_stat_mem();
	if (!strcmp(flag, "--stat-disk")) return cmd_stat_disk();
	if (!strcmp(flag, "--stat-relay-status")) return cmd_stat_relay_status();
	if (!strcmp(flag, "--list-apps")) return cmd_list_apps(NULL);
	if (!strcmp(flag, "--list-apps-for") && arg) return cmd_list_apps(arg);
	if (!strcmp(flag, "--launch") && arg) return cmd_launch(arg);
	if (!strcmp(flag, "--stream-app") && arg && argc >= 4) return cmd_stream_app(arg, argv[3]);

	/* ---- stats server role ---- */
	if (!strcmp(flag, "--start")) return cmd_server_start();
	if (!strcmp(flag, "--stop")) return cmd_server_stop();
	if (!strcmp(flag, "--status")) return cmd_server_status();
	if (!strcmp(flag, "--stat-serving")) return cmd_stat_serving();

	/* ---- screen watch role ---- */
	if (!strcmp(flag, "--watch")) return cmd_watch_start(arg);
	if (!strcmp(flag, "--stop-watching")) return cmd_watch_stop();
	if (!strcmp(flag, "--stat-watching")) return cmd_stat_watching();

	/* ---- screen host role ---- */
	if (!strcmp(flag, "--host-watched")) return cmd_host_start(arg);
	if (!strcmp(flag, "--stop-hosting")) return cmd_host_stop();
	if (!strcmp(flag, "--stat-agent")) return cmd_stat_agent();

	if (!strcmp(flag, "--help") || !strcmp(flag, "-h")) {
		print_usage(argv[0]);
		return 0;
	}

	fprintf(stderr, "syn-relay: unrecognized arguments\n");
	print_usage(argv[0]);
	return 1;
}

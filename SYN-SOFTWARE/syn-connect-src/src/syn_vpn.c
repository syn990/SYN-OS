/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L
#include "syn_vpn.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <spawn.h>
#include <pwd.h>

extern char **environ;

static void set_err(char *err, size_t err_len, const char *fmt, ...) {
	if (!err || err_len == 0) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, err_len, fmt, ap);
	va_end(ap);
}

static const char *config_dir(void) {
	static char dir[512] = {0};
	if (dir[0]) {
		return dir;
	}
	const char *home = getenv("HOME");
	if (!home) {
		struct passwd *pw = getpwuid(getuid());
		home = pw ? pw->pw_dir : "/root";
	}
	snprintf(dir, sizeof(dir), "%s/.ovpn", home);
	return dir;
}

static const char *logfile_path(void) {
	static char path[512] = {0};
	if (path[0]) {
		return path;
	}
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) runtime_dir = "/tmp";
	snprintf(path, sizeof(path), "%s/syn-connect-vpn.log", runtime_dir);
	return path;
}

int syn_vpn_list_configs(syn_vpn_config *out, int out_cap) {
	DIR *d = opendir(config_dir());
	if (!d) {
		return 0; /* no ~/.ovpn dir yet — not an error, just nothing configured */
	}

	int count = 0;
	struct dirent *entry;
	while (count < out_cap && (entry = readdir(d)) != NULL) {
		size_t len = strlen(entry->d_name);
		if (len < 6 || strcmp(entry->d_name + len - 5, ".ovpn") != 0) continue;

		syn_vpn_config *cfg = &out[count];
		memset(cfg, 0, sizeof(*cfg));
		snprintf(cfg->name, sizeof(cfg->name), "%.*s", (int)(len - 5), entry->d_name);
		snprintf(cfg->path, sizeof(cfg->path), "%s/%s", config_dir(), entry->d_name);
		cfg->has_auth_file = !syn_vpn_needs_credentials(cfg);
		count++;
	}
	closedir(d);
	return count;
}

/* Reads the relative filename after "auth-user-pass" in `cfg`'s config
 * (openvpn's own directive for a username+password file, as opposed to
 * cert-based auth) and resolves it against the config's own directory —
 * openvpn resolves relative auth-user-pass paths the same way when run
 * with --config from that directory. Returns false (no filename found)
 * if the config doesn't use auth-user-pass at all. */
static bool resolve_auth_path(const char *cfg_path, char *out, size_t out_len) {
	FILE *f = fopen(cfg_path, "r");
	if (!f) return false;

	char line[512];
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		char fname[256];
		if (sscanf(line, "auth-user-pass %255s", fname) == 1) {
			/* Directory this cfg_path lives in, then the auth filename. */
			const char *slash = strrchr(cfg_path, '/');
			if (slash) {
				snprintf(out, out_len, "%.*s/%s", (int)(slash - cfg_path), cfg_path, fname);
			} else {
				snprintf(out, out_len, "%s", fname);
			}
			found = true;
			break;
		}
	}
	fclose(f);
	return found;
}

bool syn_vpn_needs_credentials(const syn_vpn_config *cfg) {
	char auth_path[600];
	if (!resolve_auth_path(cfg->path, auth_path, sizeof(auth_path))) {
		return false; /* no auth-user-pass directive at all — cert-based or passwordless */
	}
	return access(auth_path, F_OK) != 0;
}

#define DIALPAD_BIN "/usr/lib/syn-os/syn-uplink-dialpad"

bool syn_vpn_write_credentials(const syn_vpn_config *cfg, char *err, size_t err_len) {
	char auth_path[600];
	if (!resolve_auth_path(cfg->path, auth_path, sizeof(auth_path))) {
		set_err(err, err_len, "%s has no auth-user-pass directive", cfg->name);
		return false;
	}

	char title[160];
	snprintf(title, sizeof(title), "VPN: %s", cfg->name);
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "%s --login '%s'", DIALPAD_BIN, title);

	FILE *pipe = popen(cmd, "r");
	if (!pipe) {
		set_err(err, err_len, "Could not start syn-uplink-dialpad: %s", strerror(errno));
		return false;
	}

	char username[256] = {0}, password[256] = {0};
	bool got_user = fgets(username, sizeof(username), pipe) != NULL;
	bool got_pass = got_user && fgets(password, sizeof(password), pipe) != NULL;
	int rc = pclose(pipe);

	username[strcspn(username, "\n")] = '\0';
	password[strcspn(password, "\n")] = '\0';

	if (!got_user || !got_pass || username[0] == '\0') {
		/* Esc in the dialpad popup prints nothing and exits 0 — same
		 * "no output at all" cancel contract syn_dialpad_prompt() in
		 * syn-relay already relies on — so an empty/short read here is a
		 * real cancel, not necessarily a Wayland failure. Only call out
		 * "no desktop session" specifically when the process itself
		 * couldn't even start against a compositor (non-zero exit with
		 * nothing read at all is the closest signal available without
		 * capturing stderr too). */
		if (!got_user && WIFEXITED(rc) && WEXITSTATUS(rc) != 0) {
			set_err(err, err_len, "no desktop session — VPN credential entry needs syn-uplink-dialpad, which needs a running Wayland session");
		} else {
			set_err(err, err_len, "Cancelled");
		}
		memset(password, 0, sizeof(password));
		return false;
	}

	int fd = open(auth_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		set_err(err, err_len, "Could not create %s: %s", auth_path, strerror(errno));
		memset(password, 0, sizeof(password));
		return false;
	}
	FILE *f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		set_err(err, err_len, "Could not write %s: %s", auth_path, strerror(errno));
		memset(password, 0, sizeof(password));
		return false;
	}
	/* openvpn's own format for this file: username on line 1, password
	 * on line 2, nothing else. */
	fprintf(f, "%s\n%s\n", username, password);
	fclose(f);
	memset(password, 0, sizeof(password));
	return true;
}

/* Finds the running `openvpn ... --config <cfg_path> ...` process (if
 * any) by walking /proc directly and reading each process's own
 * /proc/<pid>/cmdline — not by reading a pidfile (openvpn runs as root
 * under doas, so any pidfile it writes via --writepid is root-owned and
 * unreadable by this unprivileged process afterward — confirmed
 * empirically, even doas itself can't overwrite a file already owned by
 * another user under this system's doas config), and not by shelling out
 * to `pgrep -f` (tried and confirmed broken a different way: piping the
 * search pattern through `popen("pgrep -f -- '--config <path>' ")`
 * means the intermediate `sh -c` process's own argv contains that exact
 * search string, which `pgrep -f` then happily matches against itself —
 * disconnecting genuinely killed openvpn, but this function kept
 * reporting "still connected" because it was matching its own search
 * command, not the real process). Reading /proc/<pid>/cmdline directly
 * has no such self-reference problem — this process's own argv never
 * contains the search string, only the openvpn process's does.
 * /proc/<pid>/cmdline is world-readable on a normal Linux system
 * regardless of who owns the process — the same reason `ps aux` from
 * any user shows every user's command lines — so no special privilege
 * is needed to read another user's (even root's) here.
 *
 * Matches on argv containing exactly "--config" immediately followed by
 * cfg_path as a separate argument (cmdline's NUL-separated fields are
 * read one at a time, so this can't accidentally match a differently-
 * flagged argument that merely contains the same substring, e.g. --log
 * pointing at a path with "--config" in its name). Returns the PID, or
 * -1 if nothing matching is running. */
static pid_t find_running_pid(const char *cfg_path) {
	DIR *proc = opendir("/proc");
	if (!proc) return -1;

	pid_t found = -1;
	struct dirent *entry;
	while (found < 0 && (entry = readdir(proc)) != NULL) {
		char *endptr;
		long pid = strtol(entry->d_name, &endptr, 10);
		if (*endptr != '\0' || pid <= 0) continue; /* not a /proc/<pid> dir */

		char cmdline_path[64];
		snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%ld/cmdline", pid);
		FILE *f = fopen(cmdline_path, "r");
		if (!f) continue; /* process exited between readdir() and here, or genuinely unreadable */

		/* cmdline is NUL-separated argv, no trailing NUL guaranteed on
		 * the last field — read the whole thing into a fixed buffer and
		 * walk it as a sequence of NUL-terminated strings. */
		char buf[4096];
		size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
		buf[n] = '\0';

		bool is_openvpn = false;
		bool prev_was_config_flag = false;
		size_t off = 0;
		while (off < n) {
			const char *arg = buf + off;
			size_t arg_len = strlen(arg);
			if (off == 0) {
				/* argv[0] — either the bare binary name or a full path
				 * ending in it (posix_spawnp resolves via PATH but argv[0]
				 * itself is passed through as given, "openvpn" here). */
				const char *slash = strrchr(arg, '/');
				is_openvpn = strcmp(slash ? slash + 1 : arg, "openvpn") == 0;
			} else if (prev_was_config_flag && strcmp(arg, cfg_path) == 0) {
				if (is_openvpn) found = (pid_t)pid;
				break;
			}
			prev_was_config_flag = strcmp(arg, "--config") == 0;
			off += arg_len + 1;
		}
	}
	closedir(proc);
	return found;
}

bool syn_vpn_is_connected(const syn_vpn_config *cfg, char *connected_name, size_t connected_name_len) {
	pid_t pid = find_running_pid(cfg->path);
	if (pid <= 0) {
		return false;
	}
	if (connected_name && connected_name_len) {
		snprintf(connected_name, connected_name_len, "%s", cfg->name);
	}
	return true;
}

bool syn_vpn_disconnect(const syn_vpn_config *cfg, char *err, size_t err_len) {
	pid_t pid = find_running_pid(cfg->path);
	if (pid <= 0) {
		set_err(err, err_len, "Not connected");
		return false;
	}

	/* openvpn was started via doas, so it runs as root — SIGTERM from
	 * our unprivileged process won't reach it (kill() itself would just
	 * fail with EPERM). Route the signal through doas the same way the
	 * connect side does, rather than silently failing to actually stop
	 * the tunnel. openvpn handles SIGTERM as a clean shutdown (tears
	 * down the tun interface/routes itself). */
	char pid_str[32];
	snprintf(pid_str, sizeof(pid_str), "%ld", (long)pid);
	pid_t doas_pid;
	char *argv[] = {"doas", "kill", pid_str, NULL};
	int r = posix_spawnp(&doas_pid, "doas", NULL, NULL, argv, environ);
	if (r != 0) {
		set_err(err, err_len, "Could not run doas kill: %s", strerror(r));
		return false;
	}
	int status;
	waitpid(doas_pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		set_err(err, err_len, "doas kill failed");
		return false;
	}

	/* waitpid() above only confirms `doas kill` itself returned — SIGTERM
	 * is a request, not synchronous, and openvpn needs a moment to tear
	 * down tun0/close its socket/exit after receiving it. Returning
	 * immediately here raced the caller's very next connected-state check
	 * (confirmed live: the dashboard still showed "connected" right after
	 * a disconnect that had, by every other measure, genuinely worked) —
	 * wait for the process to actually leave the table before reporting
	 * success, not just for the signal to have been sent. */
	for (int i = 0; i < 20 && find_running_pid(cfg->path) > 0; i++) {
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000000};
		nanosleep(&ts, NULL);
	}
	return true;
}

bool syn_vpn_connect(const syn_vpn_config *cfg, char *err, size_t err_len) {
	unlink(logfile_path());

	/* --cd resolves the config's own relative auth-user-pass (and any
	 * ca/cert/key) paths against the .ovpn's directory — without it
	 * openvpn resolves them against whatever this process's own current
	 * working directory happens to be, which is almost never the same
	 * place (confirmed: this is exactly what broke the first attempt —
	 * "cannot stat file 'pass.txt': No such file or directory" in
	 * openvpn's own log, because it looked in syn-connect's CWD instead
	 * of ~/.ovpn/). No --writepid — see find_running_pid()'s comment for
	 * why a root-owned pidfile doesn't work for this unprivileged
	 * process to read back.
	 *
	 * Plain doas subprocess, not syn-uplink-dialpad --exec — that mode's
	 * "phase 2" (see its own main.c comment) deliberately takes over the
	 * calling terminal for the child's entire remaining lifetime, which
	 * is right for something like syn-iso-builder re-exec'ing a whole
	 * long-lived interactive TUI under doas, but wrong here: `openvpn
	 * --daemon` authenticates and forks itself into the background in
	 * under a second, then needs no further terminal access at all.
	 * Using --exec anyway was tried and confirmed broken — once doas
	 * authenticated, phase 2 put this terminal in raw mode and started
	 * relaying openvpn's own output onto it, corrupting the screen (this
	 * is the "unprivileged binary drops to a plain interactive doas
	 * prompt only for the actual privileged step" pattern syn-iso-
	 * builder's menu.xml entry already documents — the caller (main.c)
	 * must call syn_tui_end() before this and syn_tui_init() after,
	 * exactly bracketing this one call, same as that existing
	 * convention). */
	pid_t pid;
	char *argv[] = {
		"doas", "openvpn",
		"--cd", (char *)config_dir(),
		"--config", (char *)cfg->path,
		"--daemon", (char *)cfg->name,
		"--log", (char *)logfile_path(),
		NULL
	};
	int r = posix_spawnp(&pid, "doas", NULL, NULL, argv, environ);
	if (r != 0) {
		set_err(err, err_len, "Could not start doas openvpn: %s", strerror(r));
		return false;
	}

	int status = 0;
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		set_err(err, err_len, "openvpn exited with an error (see %s)", logfile_path());
		return false;
	}

	/* --daemon makes openvpn fork and this doas/openvpn foreground call
	 * return once initialization succeeds — but "succeeded" here means
	 * "didn't fail immediately" (bad config, missing auth file), not
	 * "tunnel is up" (that can still take a few seconds against a real
	 * remote). Give the daemonized process a moment to actually show up
	 * in the process table before treating "not found yet" as failure. */
	pid_t running = -1;
	for (int i = 0; i < 20 && running <= 0; i++) {
		running = find_running_pid(cfg->path);
		if (running > 0) break;
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000000};
		nanosleep(&ts, NULL);
	}
	if (running <= 0) {
		set_err(err, err_len, "openvpn did not stay running (see %s)", logfile_path());
		return false;
	}
	return true;
}

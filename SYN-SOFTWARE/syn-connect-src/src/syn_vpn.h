/* ------------------------------------------------------------------------
 *   OpenVPN tab: openvpn has no system D-Bus API the way iwd/BlueZ do, so
 *   unlike the Wi-Fi/Bluetooth tabs this one really does have to run real
 *   subprocesses. Two different privileged-auth tools for two different
 *   shapes of problem, deliberately not the same one everywhere:
 *
 *     - Credentials (username+password): syn-uplink-dialpad --login
 *       (SYN-SOFTWARE/syn-uplink-dialpad-src/) — a real graphical Wayland
 *       popup, self-contained start to finish, no terminal involved.
 *
 *     - The actual privileged step (`doas openvpn --daemon`): a plain
 *       bracketed doas subprocess (syn_tui_end() before, syn_tui_init()
 *       after — see main.c's VPN tab handling), NOT syn-uplink-dialpad
 *       --exec. That mode's "phase 2" (see its own main.c) deliberately
 *       takes over the calling terminal for the rest of the child's
 *       life — right for syn-iso-builder re-exec'ing a whole long-lived
 *       interactive TUI under doas, wrong for `openvpn --daemon`, which
 *       authenticates and forks itself into the background in under a
 *       second and needs no further terminal access. Using --exec here
 *       was tried and confirmed broken: once doas authenticated, phase 2
 *       put the terminal in raw mode and relayed openvpn's own output
 *       onto it, corrupting syn-connect's ncurses screen underneath.
 *
 *   Connected state is tracked by matching the running process's command
 *   line (see find_running_pid() in syn_vpn.c), not a PID file — openvpn
 *   runs as root under doas, so any pidfile it writes via --writepid is
 *   root-owned and unreadable by this unprivileged process afterwards
 *   (confirmed empirically: even `doas` itself refuses to overwrite a
 *   file already owned by another user under this system's doas config,
 *   so there's no cheap workaround either). /proc/<pid>/cmdline is
 *   world-readable on a normal Linux system regardless of who owns the
 *   process — the same reason `ps aux` from any user shows every user's
 *   command lines — so `pgrep -f` needs no special privilege here.
 *
 *   syn-uplink-dialpad is a native Wayland client (wl_display_connect()
 *   unconditionally in its main(), confirmed by reading it directly —
 *   no TTY/ncurses fallback exists in that file today) — so credential
 *   entry specifically only works with a real compositor running.
 *   syn_vpn_write_credentials() reports this distinctly (see its doc
 *   below) so the caller can show "VPN needs the desktop session" rather
 *   than a generic failure when run from the live installer TTY. The
 *   plain-doas connect/disconnect steps have no such restriction.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_VPN_H
#define SYN_VPN_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	char name[128];        /* config filename without .ovpn, e.g. "nl" */
	char path[512];        /* full path to the .ovpn file */
	bool has_auth_file;     /* the config's own auth-user-pass file already exists */
} syn_vpn_config;

/* Scans ~/.ovpn/ for *.ovpn files (the convention already in use on this
 * machine — see nl.ovpn) into *out (caller-allocated, out_cap entries). Returns the
 * real count, 0 if the directory doesn't exist or is empty (not an
 * error — just nothing configured yet). */
int syn_vpn_list_configs(syn_vpn_config *out, int out_cap);

/* True if `cfg` has a running `doas openvpn --config <cfg->path> ...`
 * process right now (see this file's header comment for how — no PID
 * file involved). Only one VPN config can realistically be connected at
 * a time (one default route to hand off), but this checks `cfg`
 * specifically rather than assuming, so the caller can check every
 * listed config and find whichever one (if any) is actually running. */
bool syn_vpn_is_connected(const syn_vpn_config *cfg, char *connected_name, size_t connected_name_len);

/* True if `cfg`'s config references an auth-user-pass file (relative to
 * the .ovpn's own directory) that doesn't exist yet — the one case the
 * caller needs to prompt for credentials before connecting. */
bool syn_vpn_needs_credentials(const syn_vpn_config *cfg);

/* Pops syn-uplink-dialpad --login to collect a username+password, then
 * writes them to the auth file `cfg`'s config expects (mode 600). Called
 * when syn_vpn_needs_credentials() was true; the file persists so future
 * connects don't need to prompt again, matching how the pre-existing
 * nl.ovpn + pass.txt pair already works on this machine. Returns false
 * with *err set on: user cancelled (Esc in the dialpad popup), the
 * dialpad couldn't reach a Wayland compositor (err starts with "no
 * desktop session" — check for this prefix if the caller wants to
 * distinguish it from a real failure), or the write itself failed. */
bool syn_vpn_write_credentials(const syn_vpn_config *cfg, char *err, size_t err_len);

/* Disconnects `cfg` specifically (no-op with *err set to "Not connected"
 * if it isn't currently running) by sending SIGTERM to its process (via
 * a plain `doas kill` subprocess — an unprivileged kill() can't signal a
 * root-owned process) — openvpn handles SIGTERM as a clean shutdown
 * (tears down the tun interface/routes itself). Prints doas's own
 * password prompt to whatever this process's controlling terminal is —
 * the caller must bracket this with syn_tui_end() before and
 * syn_tui_init() after if it owns an ncurses screen on that same
 * terminal, exactly like syn_vpn_connect() below. */
bool syn_vpn_disconnect(const syn_vpn_config *cfg, char *err, size_t err_len);

/* Runs `doas openvpn --cd <config dir> --config <cfg->path> --daemon
 * <cfg->name> --log <logfile>` as a plain subprocess — a real
 * interactive doas prompt on this process's controlling terminal. --cd
 * matters: without it openvpn resolves auth-user-pass (and any ca/cert/
 * key) paths against this process's own working directory instead of
 * the .ovpn's directory, which is almost never the same place
 * (confirmed: this exact mismatch was the first real bug found here —
 * "cannot stat file 'pass.txt'" in openvpn's log, because it looked in
 * the wrong directory entirely). The caller must have already torn down
 * any ncurses screen on that terminal (syn_tui_end()) before calling
 * this and rebuild it after (syn_tui_init()), same bracketing syn-iso-
 * builder's own doas step uses — see this file's header comment for why
 * syn-uplink-dialpad --exec is deliberately not used here. Blocks until
 * the doas+openvpn foreground process exits (openvpn daemonizes itself
 * once the tunnel is up, so this returns quickly on success — a real
 * connect failure also exits fast rather than hanging). Returns false
 * with *err set if doas was cancelled/failed or openvpn exited non-zero
 * before daemonizing, or didn't stay running afterward. */
bool syn_vpn_connect(const syn_vpn_config *cfg, char *err, size_t err_len);

#endif

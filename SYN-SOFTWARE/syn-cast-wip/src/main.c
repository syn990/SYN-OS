/* ------------------------------------------------------------------------
 *   syn-cast: push-to-talk on SYN-CAST channels, for SYN-OS. It speaks
 *   SYN-CAST protocol v1 (SYN-CASTER's docs/cast-protocol.md) to the
 *   crew's own server; nothing here touches Zello.
 *
 *   syn-cast                    the window
 *   syn-cast --headless         no window (the control socket still works)
 *   syn-cast setup              write the account and channel config
 *   syn-cast ptt down|up|toggle, say, text, image, loc, to, channel,
 *           replay, stop, room, vox, speed, status, quit
 *                              drive the running instance
 *   syn-cast decode IN.spt OUT.wav   a stored voice message to WAV
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "app.h"
#include "ctl.h"
#include "ui.h"
#include "util.h"

#include <jansson.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define VERSION SYN_CAST_VERSION

static volatile sig_atomic_t got_signal;

static void on_signal(int sig) {
	(void)sig;
	got_signal = 1;
}

static void usage(FILE *f) {
	fprintf(f,
		"syn-cast %s: push-to-talk on SYN-CAST channels, for SYN-OS\n\n"
		"  syn-cast                    open the window\n"
		"  syn-cast --headless         run without a window\n"
		"  syn-cast setup              write ~/.config/syn-cast/config.json\n"
		"  syn-cast decode IN.spt OUT.wav\n\n"
		"Commands for the running instance (a labwc keybind can call these):\n"
		"  ptt down|up|toggle   say FILE.wav   text MESSAGE   image FILE.jpg\n"
		"  loc LAT LON [ADDRESS]   channel NAME   replay   stop\n"
		"  room   vox   speed   offline   online   status   quit\n",
		VERSION);
}

enum { FD_DISPLAY, FD_REPEAT, FD_CLIP, FD_CONNECT, FD_CAPTURE, FD_CTL, FD_SESSION };

static int run(bool gui) {
	struct sigaction sa = {.sa_handler = on_signal};
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	int lfd = ctl_listen();
	if (lfd == -2) {
		fprintf(stderr, "syn-cast is already running; use `syn-cast status`\n");
		return 1;
	}
	if (app_init() != 0) {
		ctl_close(lfd);
		return 1;
	}
	if (!config_has_account(&app.cfg)) {
		LOG("no account in %s: run `syn-cast setup`", app.cfg.path);
	}
	if (gui && ui_init() != 0) {
		fprintf(stderr, "syn-cast: no Wayland display (WAYLAND_DISPLAY unset?); --headless runs without one\n");
		app_shutdown();
		ctl_close(lfd);
		return 1;
	}

	struct pollfd pfd[64];
	int kind[64], arg[64];
	while (!app.quit && !got_signal && (!gui || ui_running())) {
		int n = 0;
		if (gui) {
			pfd[n] = (struct pollfd){ui_display_fd(), POLLIN, 0};
			kind[n++] = FD_DISPLAY;
			if (ui_repeat_fd() >= 0) {
				pfd[n] = (struct pollfd){ui_repeat_fd(), POLLIN, 0};
				kind[n++] = FD_REPEAT;
			}
			if (ui_clip_fd() >= 0) {
				pfd[n] = (struct pollfd){ui_clip_fd(), POLLIN, 0};
				kind[n++] = FD_CLIP;
			}
		}
		pfd[n] = (struct pollfd){app.connect_pipe[0], POLLIN, 0};
		kind[n++] = FD_CONNECT;
		pfd[n] = (struct pollfd){app.capture_pipe[0], POLLIN, 0};
		kind[n++] = FD_CAPTURE;
		int cfds[CTL_MAX_CLIENTS + 1];
		int nc = ctl_fds(lfd, cfds, CTL_MAX_CLIENTS + 1);
		for (int i = 0; i < nc; i++) {
			pfd[n] = (struct pollfd){cfds[i], POLLIN, 0};
			kind[n] = FD_CTL;
			arg[n++] = cfds[i];
		}
		int sfds[MAX_SESSIONS], sidx[MAX_SESSIONS];
		short sev[MAX_SESSIONS];
		int ns = app_session_fds(sfds, sev, sidx, MAX_SESSIONS);
		for (int i = 0; i < ns; i++) {
			pfd[n] = (struct pollfd){sfds[i], sev[i], 0};
			kind[n] = FD_SESSION;
			arg[n++] = sidx[i];
		}

		int timeout = app_timeout_ms();
		if (gui) {
			int t = ui_timeout_ms();
			if (t < timeout) timeout = t;
			ui_before_poll();
		}
		int r = poll(pfd, (nfds_t)n, timeout);
		short display_ev = 0;
		for (int i = 0; r > 0 && i < n; i++) {
			if (kind[i] == FD_DISPLAY) {
				display_ev = pfd[i].revents;
			}
		}
		if (gui) {
			ui_after_poll(display_ev);
		}
		for (int i = 0; r > 0 && i < n; i++) {
			if (!pfd[i].revents) {
				continue;
			}
			switch (kind[i]) {
			case FD_REPEAT: ui_on_repeat(); break;
			case FD_CLIP: ui_on_clip(); break;
			case FD_CONNECT: app_on_connect_pipe(); break;
			case FD_CAPTURE: app_on_capture_pipe(); break;
			case FD_CTL:
				if (arg[i] == lfd) ctl_on_listen(lfd);
				else ctl_on_client(arg[i]);
				break;
			case FD_SESSION: app_on_session_io(arg[i], pfd[i].revents); break;
			default: break;
			}
		}
		app_tick();
		if (gui) {
			ui_frame();
		}
	}
	if (gui) {
		ui_shutdown();
	}
	app_shutdown();
	ctl_close(lfd);
	return 0;
}

/* ---------- setup ---------- */

static void ask(const char *prompt, char *out, size_t cap, bool secret) {
	printf("%s", prompt);
	fflush(stdout);
	struct termios old, quiet;
	bool tty = isatty(0) && tcgetattr(0, &old) == 0;
	if (secret && tty) {
		quiet = old;
		quiet.c_lflag &= ~(tcflag_t)ECHO;
		tcsetattr(0, TCSANOW, &quiet);
	}
	if (!fgets(out, (int)cap, stdin)) {
		out[0] = 0;
	}
	out[strcspn(out, "\r\n")] = 0;
	if (secret && tty) {
		tcsetattr(0, TCSANOW, &old);
		printf("\n");
	}
}

static int setup(void) {
	config c;
	char err[400];
	if (config_load(&c, err, sizeof err) < 0) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	printf("syn-cast setup: %s\n\n", c.path);
	printf("SYN-CAST signs in to the crew's own server with a member token. The first time, paste\n"
		"the one-time invite code the crew gave you (syn-caster cast invite MEMBER); the token it\n"
		"earns is kept here and used from then on.\n\n");

	char buf[2048], v[512];
	snprintf(buf, sizeof buf, "Server (wss://HOST:PORT%s)%s%s%s: ", ZP_PATH, c.server[0] ? " [" : "", c.server,
		c.server[0] ? "]" : "");
	ask(buf, v, sizeof v, false);
	if (v[0]) {
		str_copy(c.server, sizeof c.server, v);
	}
	if (c.server[0] && !strstr(c.server, "/cast/")) {
		/* the host alone was given: the path is always the same */
		size_t n = strlen(c.server);
		if (n && c.server[n - 1] == '/') c.server[n - 1] = 0;
		strncat(c.server, ZP_PATH, sizeof c.server - strlen(c.server) - 1);
	}
	snprintf(buf, sizeof buf, "Invite code%s: ", c.token[0] ? " (empty keeps the token you have)" : "");
	ask(buf, v, sizeof v, false);
	if (v[0]) {
		str_copy(c.invite, sizeof c.invite, v);
		c.token[0] = 0;
	}
	snprintf(buf, sizeof buf, "The server's certificate, PEM path, for a self-signed server%s%s%s: ",
		c.cert_path[0] ? " [" : " (empty: the system's trust store)", c.cert_path, c.cert_path[0] ? "]" : "");
	ask(buf, v, sizeof v, false);
	if (v[0]) {
		expand_home(v, c.cert_path, sizeof c.cert_path);
	}
	snprintf(buf, sizeof buf, "Channels, comma separated (empty: every channel you belong to)%s%s%s: ",
		c.n_channels ? " [" : "", c.n_channels ? c.channels[0] : "", c.n_channels ? "...]" : "");
	char list[1024];
	ask(buf, list, sizeof list, false);
	if (list[0]) {
		c.n_channels = 0;
		for (char *tok = strtok(list, ","); tok && c.n_channels < CFG_MAX_CHANNELS; tok = strtok(NULL, ",")) {
			while (*tok == ' ') tok++;
			char *e = tok + strlen(tok);
			while (e > tok && e[-1] == ' ') *--e = 0;
			if (*tok) str_copy(c.channels[c.n_channels++], sizeof c.channels[0], tok);
		}
	}
	if (!c.server[0] || (!c.invite[0] && !c.token[0])) {
		fprintf(stderr, "a server and an invite code (or a token) are needed\n");
		return 1;
	}
	if (config_save_account(&c) != 0) {
		fprintf(stderr, "cannot write %s\n", c.path);
		return 1;
	}
	printf("\nWritten (mode 600). Start it with: syn-cast\n");
	return 0;
}

/* ---------- decode ---------- */

static int decode(const char *in, const char *out) {
	hv_file f;
	if (hv_load(in, &f) != 0) {
		fprintf(stderr, "%s is not a syn-cast voice file\n", in);
		return 1;
	}
	rx_decoder d;
	if (rxdec_init(&d) != 0) {
		hv_free(&f);
		return 1;
	}
	size_t cap = (f.n + 1) * MAX_PACKET_SAMPLES * 2, n = 0;
	int16_t *pcm = malloc(cap * sizeof *pcm);
	for (size_t k = 0; k < f.n; k++) {
		if (f.p[k].len == 0) {
			rxdec_lost(&d, 1);
			continue;
		}
		int got = rxdec_decode(&d, f.p[k].data, f.p[k].len, pcm + n, cap - n);
		if (got > 0) n += (size_t)got;
	}
	int rc = wav_save(out, pcm, n, AUDIO_RATE);
	printf("%s: %zu packets, %.2f s\n", out, f.n, (double)n / AUDIO_RATE);
	free(pcm);
	rxdec_free(&d);
	hv_free(&f);
	return rc == 0 ? 0 : 1;
}

/* ---------- main ---------- */

static const char *const VERBS[] = {"ptt", "say", "text", "image", "loc", "channel", "replay",
	"stop", "room", "vox", "speed", "offline", "online", "status", "snapshot", "quit"};

int main(int argc, char **argv) {
	if (argc == 1) {
		return run(true);
	}
	const char *a = argv[1];
	if (strcmp(a, "--headless") == 0) {
		return run(false);
	}
	if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 || strcmp(a, "help") == 0) {
		usage(stdout);
		return 0;
	}
	if (strcmp(a, "--version") == 0) {
		printf("syn-cast %s\n", VERSION);
		return 0;
	}
	if (strcmp(a, "setup") == 0) {
		return setup();
	}
	if (strcmp(a, "decode") == 0) {
		if (argc != 4) {
			usage(stderr);
			return 2;
		}
		return decode(argv[2], argv[3]);
	}
	for (size_t v = 0; v < ARRAY_LEN(VERBS); v++) {
		if (strcmp(a, VERBS[v]) != 0) {
			continue;
		}
		char line[2048];
		size_t o = (size_t)snprintf(line, sizeof line, "%s", a);
		for (int i = 2; i < argc && o < sizeof line; i++) {
			char path[1024];
			const char *arg = argv[i];
			/* the running instance may have another working directory */
			if ((strcmp(a, "say") == 0 || strcmp(a, "image") == 0) && arg[0] != '/' &&
				realpath(arg, path)) {
				arg = path;
			}
			o += (size_t)snprintf(line + o, sizeof line - o, " %s", arg);
		}
		return ctl_call(line);
	}
	usage(stderr);
	return 2;
}

/* ------------------------------------------------------------------------
 *                             S Y N - H U M
 *
 *   The acoustic monitor: the machine as quiet sound. Samples CPU (each
 *   core), memory pressure, network, disk, new inbound connections and
 *   log errors ten times a second, turns them into steps 0-15, and sends
 *   them to syn-bar-core, which plays them in the style hum.conf names.
 *   syn-bar-core stays the only thing in SYN-OS that plays sound; this
 *   only says what's going on.
 *
 *   Started and stopped by its waybar button (toggle), its style and
 *   volume set from labwc's menu (set), and nothing ever asks anything:
 *   the daemon's own output goes to $XDG_RUNTIME_DIR/syn-hum.log.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _GNU_SOURCE
#include "syn_bar_notify.h"
#include "syn_hum_conf.h"
#include "syn_hum_crew.h"
#include "syn_hum_map.h"
#include "syn_hum_sample.h"
#include "syn_hum_send.h"
#include "syn_hum_styles.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SAMPLE_NS 100000000L /* ten times a second */
#define BAR_SIGNAL 8         /* waybar's custom/hum has "signal": 8, re-asking on SIGRTMIN+8 */

static volatile sig_atomic_t stopping, reload;

static void on_stop(int sig) {
	(void)sig;
	stopping = 1;
}

static void on_reload(int sig) {
	(void)sig;
	reload = 1;
}

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s <command>\n"
		"\n"
		"  toggle            start if it's stopped, stop if it's running (the bar's button)\n"
		"  start | stop\n"
		"  status [--json]   on or off, its style, and what it last played (--json for waybar)\n"
		"  styles            every style, and what each signal sounds like in it\n"
		"  set style NAME    " SYN_HUM_STYLE_LIST "\n"
		"  set volume LEVEL  quiet, normal or loud\n"
		"  set crew on|off   the crew's bed (SYN-CASTER's crew, in techno)\n"
		"  set crew ID on|off  one member in or out of it\n"
		"  test [STYLE]      every sound of a style in turn, each named as it plays\n"
		"  watch             print the signals ten times a second, sending nothing (Ctrl+C stops)\n",
		argv0);
}

static void runtime_path(const char *name, char *out, size_t len) {
	const char *dir = getenv("XDG_RUNTIME_DIR");
	snprintf(out, len, "%s/%s", dir ? dir : "/tmp", name);
}

/* The running daemon's pid, or 0. A pid file from before a reboot can
 * name some other process now, so the pid has to be a syn-hum too. */
static pid_t running_pid(void) {
	char path[PATH_MAX];
	runtime_path("syn-hum.pid", path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	long pid = 0;
	if (fscanf(f, "%ld", &pid) != 1) pid = 0;
	fclose(f);
	if (pid <= 0 || kill((pid_t)pid, 0) != 0) return 0;
	char comm_path[64], comm[32] = "";
	snprintf(comm_path, sizeof(comm_path), "/proc/%ld/comm", pid);
	FILE *c = fopen(comm_path, "r");
	if (c) {
		if (!fgets(comm, sizeof(comm), c)) comm[0] = '\0';
		fclose(c);
	}
	comm[strcspn(comm, "\n")] = '\0';
	return strcmp(comm, "syn-hum") == 0 ? (pid_t)pid : 0;
}

/* Tells waybar to ask `syn-hum status --json` now, not at its next interval */
static void bar_refresh(void) {
	DIR *d = opendir("/proc");
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		char path[300], comm[32] = "";
		snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
		FILE *f = fopen(path, "r");
		if (!f) continue;
		if (fgets(comm, sizeof(comm), f)) {
			comm[strcspn(comm, "\n")] = '\0';
			if (!strcmp(comm, "waybar")) kill((pid_t)atol(e->d_name), SIGRTMIN + BAR_SIGNAL);
		}
		fclose(f);
	}
	closedir(d);
}

/* ---- the daemon ------------------------------------------------------- */

/* What the daemon is hearing, in words, for `status` and the bar's
 * tooltip: $XDG_RUNTIME_DIR/syn-hum.state, rewritten once a second.
 * `sig` is that second's ten samples added up: levels times their
 * seconds, so dividing by the seconds gives the second's average, and
 * the counts as they are. */
static void write_state(const syn_hum_signals *sig, const syn_hum_steps *st, const syn_hum_crew *crew) {
	char path[PATH_MAX], tmp[PATH_MAX + 8];
	runtime_path("syn-hum.state", path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) return;
	double secs = sig->seconds > 0.0 ? sig->seconds : 1.0;
	fprintf(f, "cpu %.0f%% (each core's step:", sig->cpu / secs * 100.0);
	for (int i = 0; i < st->core_count; i++) fprintf(f, " %d", st->cores[i]);
	fprintf(f, "), network %.0f packets/s, disk %.0f%% busy, ", (double)sig->packets / secs, sig->disk_busy / secs * 100.0);
	if (sig->mem_stall < 0.0) {
		fprintf(f, "memory pressure unknown, ");
	} else {
		fprintf(f, "memory stalled %.1f%%, ", sig->mem_stall / secs * 100.0);
	}
	fprintf(f, "%.0f processes/s started, %.0f context switches/s", (double)sig->spawns / secs, (double)sig->switches / secs);
	if (sig->cpu_wait >= 0.0 && sig->io_wait >= 0.0) {
		fprintf(f, ", waiting for a CPU %.1f%%, on IO %.1f%%", sig->cpu_wait / secs * 100.0, sig->io_wait / secs * 100.0);
	}
	fputc('\n', f);
	if (crew->count || crew->talk) {
		char words[512];
		syn_hum_crew_words(crew, words, sizeof(words));
		fprintf(f, "%s\n", words);
	}
	if (fclose(f) == 0) rename(tmp, path);
}

static int daemon_loop(void) {
	struct sigaction sa = {0};
	sa.sa_handler = on_stop;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sa.sa_handler = on_reload;
	sigaction(SIGHUP, &sa, NULL);

	syn_hum_conf conf;
	syn_hum_conf_load(&conf);
	syn_hum_sampler *sampler = syn_hum_sampler_open();
	if (!sampler) {
		fprintf(stderr, "syn-hum: out of memory\n");
		return 1;
	}
	syn_hum_map map;
	memset(&map, 0, sizeof(map));
	syn_bar_notify_meaning("TOGGLE_ON");
	fprintf(stderr, "syn-hum: on, %s, %s\n", SYN_HUM_STYLES[conf.style].name, conf.volume);
	if (!syn_hum_sampler_has_psi(sampler)) fprintf(stderr, "syn-hum: no /proc/pressure/memory, so memory pressure stays silent\n");
	if (!syn_hum_sampler_has_logs(sampler)) fprintf(stderr, "syn-hum: can't read the logs, so errors stay silent\n");

	int core_there = 1;
	long tick = 0;
	syn_hum_crew crew;
	memset(&crew, 0, sizeof(crew));
	syn_hum_signals second; /* the samples since the state was last written, added up */
	memset(&second, 0, sizeof(second));
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (!stopping) {
		next.tv_nsec += SAMPLE_NS;
		if (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) != 0) continue;
		if (reload) {
			reload = 0;
			syn_hum_conf_load(&conf);
			crew.seen.tv_sec = crew.seen.tv_nsec = 0; /* the mutes may have changed: read the crew again */
			fprintf(stderr, "syn-hum: now %s, %s, crew %s\n", SYN_HUM_STYLES[conf.style].name, conf.volume, conf.crew ? "on" : "off");
		}
		syn_hum_crew_read(&crew, &conf);

		syn_hum_signals sig;
		syn_hum_steps st;
		syn_hum_sample(sampler, &sig);
		syn_hum_map_signals(&map, &sig, &st);
		second.seconds += sig.seconds;
		second.cpu += sig.cpu * sig.seconds;
		second.packets += sig.packets;
		second.disk_busy += sig.disk_busy * sig.seconds;
		second.mem_stall = sig.mem_stall < 0.0 ? -1.0 : second.mem_stall + sig.mem_stall * sig.seconds;
		second.cpu_wait = sig.cpu_wait < 0.0 ? -1.0 : second.cpu_wait + sig.cpu_wait * sig.seconds;
		second.io_wait = sig.io_wait < 0.0 ? -1.0 : second.io_wait + sig.io_wait * sig.seconds;
		second.spawns += sig.spawns;
		second.switches += sig.switches;
		if (++tick % 10 == 0) {
			write_state(&second, &st, &crew);
			memset(&second, 0, sizeof(second));
		}
		char line[1024];
		int n = syn_hum_steps_line(&st, SYN_HUM_STYLES[conf.style].name, syn_hum_volume_gain(conf.volume), line, sizeof(line));
		if (n > 0 && syn_hum_crew_keys(&crew, line + n, sizeof(line) - (size_t)n) >= 0) {
			/* syn-bar-core may come and go (a theme switch restarts the
			 * bar); say so once each way rather than every tenth second */
			int there = syn_hum_send(line) == 0;
			if (there != core_there) {
				fprintf(stderr, there ? "syn-hum: syn-bar-core is back\n" : "syn-hum: syn-bar-core isn't running, nothing plays until it is\n");
				core_there = there;
			}
		}

		/* Fell more than a second behind: start the beat again from now */
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec > next.tv_sec + 1) next = now;
	}

	syn_hum_send("HUM-OFF");
	syn_bar_notify_meaning("TOGGLE_OFF");
	syn_hum_sampler_close(sampler);
	char state[PATH_MAX];
	runtime_path("syn-hum.state", state, sizeof(state));
	unlink(state);
	fprintf(stderr, "syn-hum: off\n");
	return 0;
}

static int cmd_start(void) {
	if (running_pid()) return 0;
	pid_t child = fork();
	if (child < 0) {
		perror("syn-hum: fork");
		return 1;
	}
	if (child == 0) {
		/* Twice, so the daemon isn't a child of whatever started it
		 * (waybar) and can't get a terminal back */
		setsid();
		pid_t daemon = fork();
		if (daemon != 0) _exit(daemon < 0);
		if (chdir("/") != 0) _exit(1);
		int null = open("/dev/null", O_RDWR | O_CLOEXEC);
		char path[PATH_MAX];
		runtime_path("syn-hum.log", path, sizeof(path));
		int log = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		dup2(null, STDIN_FILENO);
		dup2(null, STDOUT_FILENO);
		dup2(log >= 0 ? log : null, STDERR_FILENO);

		runtime_path("syn-hum.pid", path, sizeof(path));
		FILE *f = fopen(path, "w");
		if (f) {
			fprintf(f, "%ld\n", (long)getpid());
			fclose(f);
		}
		bar_refresh();
		int rc = daemon_loop();
		unlink(path);
		bar_refresh();
		_exit(rc);
	}
	waitpid(child, NULL, 0);
	/* until the pid file is there, so `status` straight after says on */
	for (int i = 0; i < 40 && !running_pid(); i++) usleep(25000);
	return running_pid() ? 0 : 1;
}

static int cmd_stop(void) {
	pid_t pid = running_pid();
	if (!pid) return 0;
	kill(pid, SIGTERM);
	for (int i = 0; i < 40 && kill(pid, 0) == 0; i++) usleep(50000);
	return 0;
}

/* ---- status, for people and for waybar ------------------------------- */

/* JSON string contents, and Pango-safe, since waybar's tooltip is markup */
static void json_text(FILE *out, const char *s) {
	for (; *s; s++) {
		switch (*s) {
		case '"': fputs("\\\"", out); break;
		case '\\': fputs("\\\\", out); break;
		case '\n': fputs("\\n", out); break;
		case '&': fputs("&amp;", out); break;
		case '<': fputs("&lt;", out); break;
		case '>': fputs("&gt;", out); break;
		default:
			if ((unsigned char)*s >= 0x20) fputc(*s, out);
		}
	}
}

static int cmd_status(int json) {
	pid_t pid = running_pid();
	syn_hum_conf conf;
	syn_hum_conf_load(&conf);
	char heard[1024] = "";
	int core = syn_hum_ask("HUM-HEARD", heard, sizeof(heard));
	const char *style = SYN_HUM_STYLES[conf.style].name;

	char tip[2048];
	int n = snprintf(tip, sizeof(tip), "Acoustic monitor: %s (%s, %s)", pid ? "on" : "off", style, conf.volume);
	if (pid) {
		char path[PATH_MAX], hearing[512] = "";
		runtime_path("syn-hum.state", path, sizeof(path));
		FILE *f = fopen(path, "r");
		if (f) {
			if (!fgets(hearing, sizeof(hearing), f)) hearing[0] = '\0';
			fclose(f);
		}
		hearing[strcspn(hearing, "\n")] = '\0';
		if (hearing[0]) n += snprintf(tip + n, sizeof(tip) - (size_t)n, "\nHearing: %s", hearing);
	}
	if (core < 0) {
		n += snprintf(tip + n, sizeof(tip) - (size_t)n, "\nsyn-bar-core isn't running, so nothing plays");
	} else if (core > 0) {
		n += snprintf(tip + n, sizeof(tip) - (size_t)n, "\n\nLast heard:\n%s", heard);
		while (n > 0 && tip[n - 1] == '\n') tip[--n] = '\0';
	}
	if (json) {
		snprintf(tip + n, sizeof(tip) - (size_t)n, "\n\nClick to %s. Right-click: watch the signals.\nStyle and volume: Preferences > Desktop > Acoustic Monitor",
		         pid ? "stop" : "start");
		/* alt is the style, which waybar's format-icons turns into the
		 * style's icon; class is on or off, which the stylesheet colours */
		printf("{\"text\":\"HUM\",\"alt\":\"%s\",\"class\":\"%s\",\"tooltip\":\"", style, pid ? "on" : "off");
		json_text(stdout, tip);
		printf("\"}\n");
		return 0; /* waybar hides a module whose command fails */
	}
	printf("%s\n", tip);
	return pid ? 0 : 1;
}

static int cmd_styles(void) {
	for (int i = 0; i < SYN_HUM_STYLE_COUNT; i++) {
		printf("%s%s\n  %s\n\n", i ? "\n" : "", SYN_HUM_STYLES[i].name, SYN_HUM_STYLES[i].summary);
		for (int k = 0; k < SYN_HUM_OUT_COUNT; k++) {
			if (SYN_HUM_STYLES[i].out[k]) printf("  %-16s %s\n", SYN_HUM_OUT_NAMES[k], SYN_HUM_STYLES[i].out[k]);
		}
	}
	return 0;
}

static int cmd_set(int argc, char **argv) {
	if (argc < 4) {
		print_usage(argv[0]);
		return 1;
	}
	syn_hum_conf conf;
	syn_hum_conf_load(&conf);
	if (!strcmp(argv[2], "style")) {
		int style = syn_hum_style_find(argv[3]);
		if (style < 0) {
			fprintf(stderr, "syn-hum: no style '%s': " SYN_HUM_STYLE_LIST "\n", argv[3]);
			return 1;
		}
		conf.style = style;
	} else if (!strcmp(argv[2], "volume")) {
		if (syn_hum_volume_gain(argv[3]) < 0.0) {
			fprintf(stderr, "syn-hum: no volume '%s': quiet, normal or loud\n", argv[3]);
			return 1;
		}
		snprintf(conf.volume, sizeof(conf.volume), "%s", argv[3]);
	} else if (!strcmp(argv[2], "crew") && argc == 4 && (!strcmp(argv[3], "on") || !strcmp(argv[3], "off"))) {
		conf.crew = !strcmp(argv[3], "on");
	} else if (!strcmp(argv[2], "crew") && argc == 5 && (!strcmp(argv[4], "on") || !strcmp(argv[4], "off"))) {
		if (strlen(argv[3]) >= sizeof(conf.mute[0]) || syn_hum_conf_mute(&conf, argv[3], !strcmp(argv[4], "on")) != 0) {
			fprintf(stderr, "syn-hum: can't mute '%s': a name of 15 letters at most, %d muted at most\n", argv[3], SYN_HUM_MUTE_MAX);
			return 1;
		}
	} else {
		print_usage(argv[0]);
		return 1;
	}
	if (syn_hum_conf_save(&conf) != 0) {
		perror("syn-hum: hum.conf");
		return 1;
	}
	pid_t pid = running_pid();
	if (pid) kill(pid, SIGHUP);
	bar_refresh();
	return 0;
}

/* syn-bar-core plays the tour; this names each part as it starts */
static int cmd_test(const char *name) {
	syn_hum_conf conf;
	syn_hum_conf_load(&conf);
	int style = name ? syn_hum_style_find(name) : conf.style;
	if (style < 0) {
		fprintf(stderr, "syn-hum: no style '%s': " SYN_HUM_STYLE_LIST "\n", name);
		return 1;
	}
	char line[64];
	snprintf(line, sizeof(line), "HUM-TOUR %s", SYN_HUM_STYLES[style].name);
	if (syn_hum_send(line) != 0) {
		fprintf(stderr, "syn-hum: syn-bar-core isn't running, and it's what plays the sound\n");
		return 1;
	}
	struct sigaction sa = {0};
	sa.sa_handler = on_stop;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	printf("%s: %s\n\n", SYN_HUM_STYLES[style].name, SYN_HUM_STYLES[style].summary);
	for (int sec = 0, n = 0; sec < SYN_HUM_TOUR_SECTIONS && !stopping; sec++) {
		if (syn_hum_tour_skips(style, sec)) continue;
		n++;
		int out = SYN_HUM_TOUR[sec].out, then = SYN_HUM_TOUR[sec].then;
		const char *label = SYN_HUM_TOUR[sec].label;
		if (out < 0) {
			printf("%2d. %s\n", n, label);
		} else {
			printf("%2d. %s%s%s%s: %s", n, SYN_HUM_OUT_NAMES[out], label ? " (" : "", label ? label : "",
			       label ? ")" : "", SYN_HUM_STYLES[style].out[out]);
			if (then >= 0) printf("; heavy: %s", SYN_HUM_STYLES[style].out[then]);
			printf("\n");
		}
		fflush(stdout);
		for (int t = 0; t < (int)(SYN_HUM_TOUR[sec].seconds * 10) && !stopping; t++) {
			struct timespec tick = {0, SAMPLE_NS};
			nanosleep(&tick, NULL);
		}
	}
	if (stopping) syn_hum_send("HUM-OFF");
	return 0;
}

/* ---- watch: the signals, printed ------------------------------------- */

/* 512B, 1.2K, 34M */
static void human_bytes(double bytes, char *out, size_t len) {
	static const char units[] = "BKMGT";
	int u = 0;
	while (bytes >= 1000.0 && u < 4) {
		bytes /= 1024.0;
		u++;
	}
	snprintf(out, len, u && bytes < 10.0 ? "%.1f%c" : "%.0f%c", bytes, units[u]);
}

/* One bar per core, eight heights */
static void core_bars(const syn_hum_signals *sig, char *out, size_t len) {
	static const char *const heights[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
	size_t used = 0;
	out[0] = '\0';
	for (int i = 0; i < sig->core_count; i++) {
		int h = (int)(sig->core[i] * 8.0 + 0.5);
		const char *bar = heights[h < 0 ? 0 : h > 8 ? 8 : h];
		size_t bar_len = strlen(bar);
		if (used + bar_len >= len) {
			break;
		}
		memcpy(out + used, bar, bar_len + 1);
		used += bar_len;
	}
}

static void print_event(int count, const char *what, int tty) {
	char when[16];
	time_t now = time(NULL);
	struct tm tm_buf;
	localtime_r(&now, &tm_buf);
	strftime(when, sizeof(when), "%H:%M:%S", &tm_buf);
	printf("%s%s  %d %s%s\n", tty ? "\r\033[K" : "", when, count, what, count == 1 ? "" : "s");
}

/* On a terminal the levels redraw on one line and events scroll above
 * it; piped, every sample is a line of its own */
static int watch(void) {
	syn_hum_sampler *s = syn_hum_sampler_open();
	if (!s) {
		fprintf(stderr, "syn-hum: out of memory\n");
		return 1;
	}
	if (!syn_hum_sampler_has_psi(s)) {
		fprintf(stderr, "syn-hum: no /proc/pressure/memory (a kernel without CONFIG_PSI), memory stall shows -\n");
	}
	if (!syn_hum_sampler_has_logs(s)) {
		fprintf(stderr, "syn-hum: can't read the logs, errors won't show\n");
	}

	struct sigaction sa = {0};
	sa.sa_handler = on_stop;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	int tty = isatty(STDOUT_FILENO);
	/* The monitor's sounds, from syn-bar-core, each printed once as it
	 * plays: what was heard last time, to tell the new ones apart */
	char seen[1024] = "";
	syn_hum_ask("HUM-HEARD", seen, sizeof(seen));
	long tick = 0;
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (!stopping) {
		next.tv_nsec += SAMPLE_NS;
		if (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) != 0) {
			continue; /* Ctrl+C */
		}

		syn_hum_signals sig;
		syn_hum_sample(s, &sig);

		if (tick++ % 5 == 0) {
			char heard[1024];
			if (syn_hum_ask("HUM-HEARD", heard, sizeof(heard)) > 0 && strcmp(heard, seen) != 0) {
				/* newest first: print, oldest first, the lines that weren't there */
				char *lines[16];
				int count = 0;
				char copy[1024];
				memcpy(copy, heard, sizeof(copy));
				for (char *save = NULL, *l = strtok_r(copy, "\n", &save); l && count < 16; l = strtok_r(NULL, "\n", &save)) {
					lines[count++] = l;
				}
				for (int i = count - 1; i >= 0; i--) {
					/* each line is "HH:MM:SS what" */
					if (!strstr(seen, lines[i]) && strlen(lines[i]) > 9) {
						printf("%s%.8s  sound: %s\n", tty ? "\r\033[K" : "", lines[i], lines[i] + 9);
					}
				}
				memcpy(seen, heard, sizeof(seen));
			}
		}

		if (sig.inbound) {
			print_event(sig.inbound, "new inbound connection", tty);
		}
		if (sig.log_errors) {
			print_event(sig.log_errors, "log error", tty);
		}

		char bars[SYN_HUM_MAX_CORES * 4];
		char stall[16];
		char disk[16];
		core_bars(&sig, bars, sizeof(bars));
		if (sig.mem_stall < 0.0) {
			snprintf(stall, sizeof(stall), "-");
		} else {
			snprintf(stall, sizeof(stall), "%.1f%%", sig.mem_stall * 100.0);
		}
		human_bytes((double)sig.disk_bytes / sig.seconds, disk, sizeof(disk));
		char waits[32] = "-";
		if (sig.cpu_wait >= 0.0 && sig.io_wait >= 0.0) {
			snprintf(waits, sizeof(waits), "%.1f%%/%.1f%%", sig.cpu_wait * 100.0, sig.io_wait * 100.0);
		}
		printf("%scpu %3.0f%% [%s]  mem stall %5s  net %6.0f pkt/s  disk %3.0f%% %6s/s  "
		       "procs %4.0f/s  ctxsw %6.0f/s  wait cpu/io %s%s",
		       tty ? "\r\033[K" : "", sig.cpu * 100.0, bars, stall,
		       (double)sig.packets / sig.seconds, sig.disk_busy * 100.0, disk,
		       (double)sig.spawns / sig.seconds, (double)sig.switches / sig.seconds, waits, tty ? "" : "\n");
		fflush(stdout);

		/* Fell more than a second behind (a stopped process, a slow
		 * /proc): start the beat again from now rather than catch up */
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec > next.tv_sec + 1) {
			next = now;
		}
	}
	if (tty) {
		putchar('\n');
	}
	syn_hum_sampler_close(s);
	return 0;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	const char *command = argv[1];

	if (!strcmp(command, "--help") || !strcmp(command, "-h")) {
		print_usage(argv[0]);
		return 0;
	}

	if (!strcmp(command, "toggle")) return running_pid() ? cmd_stop() : cmd_start();
	if (!strcmp(command, "start")) return cmd_start();
	if (!strcmp(command, "stop")) return cmd_stop();
	if (!strcmp(command, "status")) return cmd_status(argc > 2 && !strcmp(argv[2], "--json"));
	if (!strcmp(command, "styles")) return cmd_styles();
	if (!strcmp(command, "set")) return cmd_set(argc, argv);
	if (!strcmp(command, "test")) return cmd_test(argc > 2 ? argv[2] : NULL);
	if (!strcmp(command, "watch")) return watch();

	fprintf(stderr, "syn-hum: unknown command '%s'\n", command);
	print_usage(argv[0]);
	return 1;
}

/* ------------------------------------------------------------------------
 *                            S Y N - N O D E
 *
 *   The command line. Everything but join goes through syn-noded's
 *   local control socket; join runs here, because the joining machine
 *   has no mesh yet for its daemon to act on. It writes the roster,
 *   secret and first address it's given, then tells the daemon to
 *   reload.
 *
 *   Built so far (README.md, build step 1): pair, join, nodes, status,
 *   revoke, leave. The rest say they aren't built yet.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (Network)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _DEFAULT_SOURCE

#include "sn_identity.h"
#include "sn_mesh.h"
#include "sn_pair.h"
#include "sn_roster.h"
#include "sn_util.h"
#include "sn_wire.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DIALPAD "/usr/lib/syn-os/syn-uplink-dialpad"

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s [command] [args]\n"
		"\n"
		"  pair                         show a pairing code (lets one machine in)\n"
		"  join [code] [host[:port]]    join the mesh with a member's code\n"
		"                               (no code: asks in the dialpad; no host: finds\n"
		"                               the machine showing a code on this network)\n"
		"  nodes [--raw]                every node, online or not\n"
		"  status                       this node: name, key, mesh\n"
		"  revoke <node>                remove a node from the whole mesh\n"
		"  leave [--yes]                forget this node's mesh (identity stays)\n"
		"\n"
		"  Not built yet: the station (no command), files, shell, screen, apps,\n"
		"  launch, send, get, borderless, share, stat, menu.\n"
		"  See SYN-SOFTWARE/syn-node-wip/README.md.\n",
		argv0);
}

/* ---- control socket ---- */

static int ctl_connect(bool quiet) {
	char path[512];
	sn_ctl_path(path, sizeof(path));
	struct sockaddr_un sa = {0};
	if (strlen(path) >= sizeof(sa.sun_path)) return -1;
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path, path, strlen(path) + 1);
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		close(fd);
		if (!quiet) fprintf(stderr, "syn-node: syn-noded isn't running (no %s)\n", path);
		return -1;
	}
	return fd;
}

/* Reads one line (without the newline). timeout_ms < 0: wait forever. */
static bool ctl_line(int fd, char *out, size_t out_size, int timeout_ms) {
	size_t n = 0;
	for (;;) {
		struct pollfd p = {.fd = fd, .events = POLLIN};
		int rc = poll(&p, 1, timeout_ms);
		if (rc < 0 && errno == EINTR) continue;
		if (rc <= 0) return false;
		char c;
		ssize_t r = read(fd, &c, 1);
		if (r <= 0) return false;
		if (c == '\n') {
			out[n] = '\0';
			return true;
		}
		if (n + 1 < out_size) out[n++] = c;
	}
}

static bool ctl_send(int fd, const char *line) {
	return sn_write_all(fd, line, strlen(line), 2000);
}

/* Sends a one-line command and prints a one-line OK/FAIL answer. */
static int ctl_simple(const char *cmd, const char *ok_fmt) {
	int fd = ctl_connect(false);
	if (fd < 0) return 1;
	char line[1024];
	if (!ctl_send(fd, cmd) || !ctl_line(fd, line, sizeof(line), 5000)) {
		fprintf(stderr, "syn-node: no answer from syn-noded\n");
		close(fd);
		return 1;
	}
	close(fd);
	if (strncmp(line, "OK", 2) == 0) {
		const char *rest = line[2] == ' ' ? line + 3 : "";
		printf(ok_fmt, rest);
		putchar('\n');
		return 0;
	}
	fprintf(stderr, "syn-node: %s\n", strncmp(line, "FAIL ", 5) == 0 ? line + 5 : line);
	return 1;
}

static void notify(const char *title, const char *body) {
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
		}
		execlp("notify-send", "notify-send", "-t", "300000", title, body, (char *)NULL);
		_exit(127);
	}
}

/* ---- commands ---- */

static int cmd_status(void) {
	int fd = ctl_connect(false);
	if (fd < 0) return 1;
	ctl_send(fd, "STATUS\n");
	char line[512];
	while (ctl_line(fd, line, sizeof(line), 5000) && strcmp(line, ".") != 0) {
		char *sp = strchr(line, ' ');
		if (!sp) continue;
		*sp = '\0';
		printf("%-8s %s\n", line, sp + 1);
	}
	close(fd);
	return 0;
}

static int cmd_nodes(bool raw) {
	int fd = ctl_connect(false);
	if (fd < 0) return 1;
	ctl_send(fd, "NODES\n");
	char line[512];
	if (!raw) printf("%-20s %-8s %-22s %-6s %s\n", "NODE", "STATE", "ADDRESS", "RTT", "KEY");
	while (ctl_line(fd, line, sizeof(line), 5000) && strcmp(line, ".") != 0) {
		if (raw) {
			puts(line);
			continue;
		}
		char *f[5] = {0};
		int nf = 0;
		for (char *tok = strtok(line, "|"); tok && nf < 5; tok = strtok(NULL, "|")) f[nf++] = tok;
		if (nf < 5) continue;
		char rtt[16];
		snprintf(rtt, sizeof(rtt), strcmp(f[4], "-") ? "%sms" : "%s", f[4]);
		printf("%-20s %-8s %-22s %-6s %.16s\n", f[0], f[2], f[3], rtt, f[1]);
	}
	close(fd);
	return 0;
}

static int cmd_pair(void) {
	int fd = ctl_connect(false);
	if (fd < 0) return 1;
	char line[256];
	if (!ctl_send(fd, "PAIR\n") || !ctl_line(fd, line, sizeof(line), 5000)) {
		fprintf(stderr, "syn-node: no answer from syn-noded\n");
		return 1;
	}
	if (strncmp(line, "CODE ", 5) != 0) {
		fprintf(stderr, "syn-node: %s\n", strncmp(line, "FAIL ", 5) == 0 ? line + 5 : line);
		return 1;
	}
	const char *code = line + 5;
	printf("Pairing code: %s\n", code);
	printf("On the other machine: syn-node join %s\n", code);
	printf("Good for 5 minutes and one try. Ctrl+C cancels.\n");
	fflush(stdout);
	if (!isatty(STDOUT_FILENO)) {
		char body[320];
		snprintf(body, sizeof(body), "%s\nOn the other machine: syn-node join", code);
		notify("SYN-NODE pairing code", body);
	}
	char result[256];
	if (!ctl_line(fd, result, sizeof(result), -1)) {
		fprintf(stderr, "syn-node: syn-noded went away\n");
		return 1;
	}
	close(fd);
	if (strncmp(result, "OK ", 3) == 0) {
		printf("Paired: %s joined the mesh\n", result + 3);
		if (!isatty(STDOUT_FILENO)) notify("SYN-NODE", "Paired");
		return 0;
	}
	fprintf(stderr, "syn-node: pairing failed: %s\n", strncmp(result, "FAIL ", 5) == 0 ? result + 5 : result);
	return 1;
}

/* ---- join ---- */

static bool ask_code(char out[SN_PAIR_CODE_DIGITS + 1]) {
	char typed[64] = "";
	if (isatty(STDIN_FILENO)) {
		printf("Pairing code: ");
		fflush(stdout);
		if (!fgets(typed, sizeof(typed), stdin)) return false;
	} else if (access(DIALPAD, X_OK) == 0) {
		FILE *p = popen(DIALPAD, "r");
		if (!p) return false;
		if (!fgets(typed, sizeof(typed), p)) typed[0] = '\0';
		pclose(p);
	} else {
		return false;
	}
	typed[strcspn(typed, "\r\n")] = '\0';
	return sn_pair_normalize(typed, out);
}

/* Broadcasts "who is showing a code?" and collects answers for 2s. */
static int find_pairing_nodes(struct sockaddr_in *out, int max) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) return 0;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

	struct sockaddr_in dst[17];
	int nd = 0;
	/* this machine's own addresses: a daemon here answers once per
	 * interface, each time from a different one of them */
	in_addr_t own[32];
	int nown = 0;
	struct ifaddrs *ifs, *it;
	if (getifaddrs(&ifs) == 0) {
		for (it = ifs; it; it = it->ifa_next) {
			if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
			if (nown < 32) own[nown++] = ((struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr;
			if (nd >= 16) continue;
			if (!(it->ifa_flags & IFF_UP) || !(it->ifa_flags & IFF_BROADCAST) || !it->ifa_broadaddr) continue;
			dst[nd] = *(struct sockaddr_in *)it->ifa_broadaddr;
			dst[nd++].sin_port = htons(SN_UDP_PORT);
		}
		freeifaddrs(ifs);
	}
	memset(&dst[nd], 0, sizeof(dst[nd]));
	dst[nd].sin_family = AF_INET;
	dst[nd].sin_port = htons(SN_UDP_PORT);
	dst[nd++].sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	int n = 0;
	uint64_t until = sn_now_ms() + 2000, next_send = 0;
	while (sn_now_ms() < until) {
		if (sn_now_ms() >= next_send) {
			for (int i = 0; i < nd; i++) sendto(fd, "SNPQ", 4, 0, (struct sockaddr *)&dst[i], sizeof(dst[i]));
			next_send = sn_now_ms() + 500;
		}
		struct pollfd p = {.fd = fd, .events = POLLIN};
		if (poll(&p, 1, 100) <= 0) continue;
		uint8_t pkt[16];
		struct sockaddr_in from;
		socklen_t flen = sizeof(from);
		ssize_t r = recvfrom(fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &flen);
		if (r != 6 || memcmp(pkt, "SNPA", 4) != 0) continue;
		from.sin_port = htons(sn_get16(pkt + 4));
		bool dup = false, from_own = ntohl(from.sin_addr.s_addr) >> 24 == 127;
		for (int k = 0; k < nown; k++) if (own[k] == from.sin_addr.s_addr) from_own = true;
		for (int i = 0; i < n; i++) {
			if (out[i].sin_port == from.sin_port) {
				/* same daemon answering on several interfaces: keep one,
				 * preferring a non-loopback address. Two answers from this
				 * machine's own addresses on one port are one daemon, however
				 * many interfaces it has (LAN, VM bridges). */
				bool out_own = ntohl(out[i].sin_addr.s_addr) >> 24 == 127;
				for (int k = 0; k < nown; k++) if (own[k] == out[i].sin_addr.s_addr) out_own = true;
				if (out[i].sin_addr.s_addr == from.sin_addr.s_addr || (out_own && from_own)) {
					if (ntohl(out[i].sin_addr.s_addr) >> 24 == 127) out[i] = from;
					dup = true;
				}
			}
		}
		if (!dup && n < max) out[n++] = from;
	}
	close(fd);
	return n;
}

static int tcp_connect(const struct sockaddr_in *sa) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	int rc = connect(fd, (const struct sockaddr *)sa, sizeof(*sa));
	if (rc != 0 && errno != EINPROGRESS) {
		close(fd);
		return -1;
	}
	struct pollfd p = {.fd = fd, .events = POLLOUT};
	int err = 0;
	socklen_t len = sizeof(err);
	if (poll(&p, 1, 3000) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool resolve_host(const char *spec, struct sockaddr_in *out) {
	char host[256];
	snprintf(host, sizeof(host), "%s", spec);
	uint16_t port = SN_DEFAULT_PORT;
	char *colon = strrchr(host, ':');
	if (colon) {
		*colon = '\0';
		long p = strtol(colon + 1, NULL, 10);
		if (p <= 0 || p > 65535) return false;
		port = (uint16_t)p;
	}
	struct addrinfo hints = {0}, *res;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, NULL, &hints, &res) != 0) return false;
	*out = *(struct sockaddr_in *)res->ai_addr;
	out->sin_port = htons(port);
	freeaddrinfo(res);
	return true;
}

static int cmd_join(const char *code_arg, const char *host_arg) {
	if (sodium_init() < 0) return 1;
	sn_identity id;
	if (!sn_identity_load(&id)) return 1;

	static sn_roster current;
	if (!sn_roster_load(&current)) {
		fprintf(stderr, "syn-node: the roster file is damaged; 'syn-node leave --yes' clears it\n");
		return 1;
	}
	if (current.has_mesh) {
		fprintf(stderr, "syn-node: this node is already in a mesh (%d member%s). "
			"Run 'syn-node leave' first to join another.\n",
			current.member_count, current.member_count == 1 ? "" : "s");
		return 1;
	}

	char code[SN_PAIR_CODE_DIGITS + 1];
	if (code_arg) {
		if (!sn_pair_normalize(code_arg, code)) {
			fprintf(stderr, "syn-node: a pairing code is 8 digits\n");
			return 1;
		}
	} else if (!ask_code(code)) {
		fprintf(stderr, "syn-node: no code given (a pairing code is 8 digits)\n");
		return 1;
	}

	struct sockaddr_in member;
	if (host_arg) {
		if (!resolve_host(host_arg, &member)) {
			fprintf(stderr, "syn-node: can't resolve %s\n", host_arg);
			return 1;
		}
	} else {
		struct sockaddr_in found[8];
		int n = find_pairing_nodes(found, 8);
		if (n == 0) {
			fprintf(stderr, "syn-node: no machine on this network is showing a pairing code.\n"
				"Run 'syn-node pair' on a member, or give its address: syn-node join <code> <host>\n");
			return 1;
		}
		if (n > 1) {
			fprintf(stderr, "syn-node: %d machines are showing pairing codes; say which:\n", n);
			for (int i = 0; i < n; i++) {
				fprintf(stderr, "  syn-node join %s %s:%u\n", code, inet_ntoa(found[i].sin_addr), ntohs(found[i].sin_port));
			}
			return 1;
		}
		member = found[0];
	}

	char where[64];
	snprintf(where, sizeof(where), "%s:%u", inet_ntoa(member.sin_addr), ntohs(member.sin_port));
	int fd = tcp_connect(&member);
	if (fd < 0) {
		fprintf(stderr, "syn-node: can't reach %s\n", where);
		return 1;
	}

	const int T = 5000;
	uint8_t *sid = NULL, *Ym = NULL, *sealed = NULL;
	size_t len;
	int rc = 1;
	sn_cpace cp;
	uint8_t k_j2m[32], k_m2j[32];

	if (!sn_write_all(fd, SN_MAGIC_PAIR, 4, T) || !sn_recv_frame(fd, &sid, &len, 64, T) || len != SN_PAIR_SID_LEN) {
		fprintf(stderr, "syn-node: %s isn't showing a pairing code\n", where);
		goto out;
	}
	if (!sn_cpace_start(&cp, true, code, sid) || !sn_send_frame(fd, cp.Y, 32, T) ||
			!sn_recv_frame(fd, &Ym, &len, 64, T) || len != 32 || !sn_cpace_finish(&cp, Ym, k_j2m, k_m2j)) {
		fprintf(stderr, "syn-node: pairing with %s broke off\n", where);
		goto out;
	}

	uint8_t idmsg[SN_PAIR_ID_LEN], idsealed[SN_PAIR_ID_LEN + SN_PAIR_SEAL_EXTRA];
	memcpy(idmsg, id.sign_pk, 32);
	sn_put16(idmsg + 32, sn_port());
	memcpy(idmsg + 34, id.name, 32);
	sn_pair_seal(k_j2m, idmsg, sizeof(idmsg), idsealed);
	if (!sn_send_frame(fd, idsealed, sizeof(idsealed), T)) {
		fprintf(stderr, "syn-node: pairing with %s broke off\n", where);
		goto out;
	}
	if (!sn_recv_frame(fd, &sealed, &len, SN_MAX_FRAME, T) || len < SN_PAIR_SEAL_EXTRA + 98) {
		fprintf(stderr, "syn-node: %s refused: wrong code, or the code was already used or expired\n", where);
		goto out;
	}
	size_t plen = len - SN_PAIR_SEAL_EXTRA;
	uint8_t *plain = malloc(plen);
	if (!plain || !sn_pair_open(k_m2j, sealed, len, plain)) {
		fprintf(stderr, "syn-node: the reply from %s didn't open (wrong code?)\n", where);
		free(plain);
		goto out;
	}

	uint8_t secret[32], mpk[32];
	char mname[32];
	memcpy(secret, plain, 32);
	uint16_t mport = sn_get16(plain + 32);
	memcpy(mname, plain + 34, 32);
	mname[31] = '\0';
	memcpy(mpk, plain + 66, 32);
	static sn_roster r;
	bool ok = sn_roster_unpack(&r, plain + 98, plen - 98);
	sodium_memzero(plain, plen);
	free(plain);
	if (!ok || !sn_roster_is_active(&r, id.sign_pk) || !sn_roster_is_active(&r, mpk)) {
		fprintf(stderr, "syn-node: the roster from %s doesn't hold up; not joining\n", where);
		goto out;
	}

	sn_addr a;
	int na = sn_addrs_set(&a, 0, 1, mpk, inet_ntoa(member.sin_addr), mport, sn_unix_time());
	if (!sn_roster_save(&r) || !sn_secret_save(secret) || !sn_addrs_save(&a, na)) {
		fprintf(stderr, "syn-node: couldn't save the mesh state\n");
		sn_mesh_forget();
		goto out;
	}
	sodium_memzero(secret, sizeof(secret));
	printf("Joined: paired with %s, %d node%s in the mesh\n", mname, r.member_count, r.member_count == 1 ? "" : "s");

	int cfd = ctl_connect(true);
	if (cfd >= 0) {
		char line[64];
		ctl_send(cfd, "RELOAD\n");
		ctl_line(cfd, line, sizeof(line), 3000);
		close(cfd);
	} else {
		printf("syn-noded isn't running here; start it and it will connect.\n");
	}
	rc = 0;

out:
	free(sid);
	free(Ym);
	free(sealed);
	sodium_memzero(k_j2m, sizeof(k_j2m));
	sodium_memzero(k_m2j, sizeof(k_m2j));
	close(fd);
	return rc;
}

static int cmd_leave(bool yes) {
	if (!yes) {
		if (!isatty(STDIN_FILENO)) {
			fprintf(stderr, "syn-node: leave forgets the mesh; add --yes to confirm\n");
			return 1;
		}
		printf("Forget this node's mesh? Other nodes keep it as a member until one revokes it. [y/N] ");
		fflush(stdout);
		char a[8] = "";
		if (!fgets(a, sizeof(a), stdin) || (a[0] != 'y' && a[0] != 'Y')) return 1;
	}
	int fd = ctl_connect(true);
	if (fd < 0) {
		/* no daemon: do it on disk directly */
		sn_mesh_forget();
		printf("Left the mesh\n");
		return 0;
	}
	close(fd);
	return ctl_simple("LEAVE\n", "Left the mesh%s");
}

static int not_built(const char *what) {
	fprintf(stderr, "syn-node: %s isn't built yet, see SYN-SOFTWARE/syn-node-wip/README.md\n", what);
	return 1;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		return not_built("the station (try: syn-node nodes)");
	}
	const char *cmd = argv[1];

	if (!strcmp(cmd, "--help") || !strcmp(cmd, "-h") || !strcmp(cmd, "help")) {
		print_usage(argv[0]);
		return 0;
	}
	if (!strcmp(cmd, "pair")) return cmd_pair();
	if (!strcmp(cmd, "join")) {
		const char *code = NULL, *host = NULL;
		for (int i = 2; i < argc; i++) {
			char tmp[SN_PAIR_CODE_DIGITS + 1];
			if (!code && sn_pair_normalize(argv[i], tmp)) code = argv[i];
			else if (!host) host = argv[i];
		}
		/* "join 4821 9930 host": two halves of a code */
		if (!code && argc >= 4) {
			char both[32], tmp[SN_PAIR_CODE_DIGITS + 1];
			snprintf(both, sizeof(both), "%s%s", argv[2], argv[3]);
			if (sn_pair_normalize(both, tmp)) {
				static char joined[SN_PAIR_CODE_DIGITS + 1];
				memcpy(joined, tmp, sizeof(joined));
				code = joined;
				host = argc >= 5 ? argv[4] : NULL;
			}
		}
		return cmd_join(code, host);
	}
	if (!strcmp(cmd, "nodes")) return cmd_nodes(argc >= 3 && !strcmp(argv[2], "--raw"));
	if (!strcmp(cmd, "status")) return cmd_status();
	if (!strcmp(cmd, "revoke")) {
		if (argc < 3) {
			fprintf(stderr, "syn-node: revoke which node? (name or key prefix)\n");
			return 1;
		}
		char line[256];
		snprintf(line, sizeof(line), "REVOKE %s\n", argv[2]);
		return ctl_simple(line, "Revoked %s");
	}
	if (!strcmp(cmd, "leave")) return cmd_leave(argc >= 3 && !strcmp(argv[2], "--yes"));

	static const char *const later[] = {
		"files", "shell", "screen", "apps", "launch", "send", "get",
		"borderless", "share", "stat", "menu",
	};
	for (size_t i = 0; i < sizeof(later) / sizeof(later[0]); i++) {
		if (!strcmp(cmd, later[i])) return not_built(cmd);
	}
	fprintf(stderr, "syn-node: unknown command '%s'\n", cmd);
	print_usage(argv[0]);
	return 1;
}

/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-CLIENT (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_relay_conn.h"
#include "syn_relay_protocol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* Short deadline: a node that isn't there should fail fast, not hang the
 * bar's 5s poll tick or the menu's pipe-menu render. */
#define SYN_AGENT_TIMEOUT_SEC 2

bool syn_relay_request(const char *host, const char *command, char *reply, size_t reply_size) {
	reply[0] = '\0';

	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	char port_str[8];
	snprintf(port_str, sizeof(port_str), "%d", SYN_RELAY_PORT);

	struct addrinfo *res;
	if (getaddrinfo(host, port_str, &hints, &res) != 0) {
		return false;
	}

	int fd = -1;
	for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0) {
			continue;
		}

		struct timeval tv = {.tv_sec = SYN_AGENT_TIMEOUT_SEC, .tv_usec = 0};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	if (fd < 0) {
		return false;
	}

	char line[SYN_RELAY_MAX_LINE];
	snprintf(line, sizeof(line), "%s\n", command);
	if (write(fd, line, strlen(line)) < 0) {
		close(fd);
		return false;
	}

	ssize_t n = read(fd, reply, reply_size - 1);
	close(fd);
	if (n <= 0) {
		return false;
	}
	reply[n] = '\0';

	size_t len = strlen(reply);
	while (len > 0 && (reply[len - 1] == '\n' || reply[len - 1] == '\r')) {
		reply[--len] = '\0';
	}
	return true;
}

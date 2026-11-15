/* ------------------------------------------------------------------------
 *   One link handshake, for tests/test_mesh.sh: dials host:port as the
 *   identity in $SN_HOME, using the roster there for the mesh, aiming at
 *   the node whose key is given. Exits 0 if the other side completes the
 *   handshake (we were accepted), 1 if it refuses or hangs up.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (tests)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _DEFAULT_SOURCE

#include "sn_identity.h"
#include "sn_noise.h"
#include "sn_roster.h"
#include "sn_util.h"
#include "sn_wire.h"

#include <arpa/inet.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static sn_roster r;

int main(int argc, char **argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: %s <ip> <port> <target key hex>\n", argv[0]);
		return 2;
	}
	if (sodium_init() < 0) return 2;
	sn_identity id;
	uint8_t target[32], rs[32];
	if (!sn_identity_load(&id) || !sn_roster_load(&r) || !r.has_mesh ||
			!sn_unhex(argv[3], target, 32) || !sn_identity_dh_of(target, rs)) {
		fprintf(stderr, "test_dial: bad setup\n");
		return 2;
	}

	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)atoi(argv[2]));
	inet_pton(AF_INET, argv[1], &sa.sin_addr);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		fprintf(stderr, "test_dial: can't connect\n");
		return 2;
	}

	uint8_t prologue[sizeof(SN_LINK_PROLOGUE) - 1 + 32];
	memcpy(prologue, SN_LINK_PROLOGUE, sizeof(SN_LINK_PROLOGUE) - 1);
	memcpy(prologue + sizeof(SN_LINK_PROLOGUE) - 1, r.mesh_id, 32);
	sn_handshake hs;
	sn_noise_init(&hs, true, prologue, sizeof(prologue), id.dh_priv, id.dh_pub, rs);
	uint8_t payload[SN_HELLO_PAYLOAD] = {0}, msg[256];
	memcpy(payload, id.sign_pk, 32);
	sn_put16(payload + 32, 1);
	memcpy(payload + 34, id.name, 32);
	long n = sn_noise_write_msg1(&hs, payload, sizeof(payload), msg);
	if (!sn_write_all(fd, SN_MAGIC_LINK, 4, 3000) || !sn_send_frame(fd, msg, (size_t)n, 3000)) return 1;

	uint8_t *reply;
	size_t len;
	if (!sn_recv_frame(fd, &reply, &len, SN_MAX_LINK_FRAME, 3000)) {
		printf("refused\n");
		return 1;
	}
	uint8_t out[SN_MAX_LINK_FRAME];
	long p = sn_noise_read_msg2(&hs, reply, len, out);
	free(reply);
	close(fd);
	if (p != SN_HELLO_PAYLOAD) {
		printf("refused\n");
		return 1;
	}
	printf("accepted\n");
	return 0;
}

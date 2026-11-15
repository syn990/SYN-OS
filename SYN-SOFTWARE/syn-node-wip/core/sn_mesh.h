/* ------------------------------------------------------------------------
 *   The rest of a node's mesh state on disk, beside the roster:
 *
 *   mesh   the mesh secret (32 bytes, 0600). Made by the founder, handed
 *          to each joiner inside the sealed pairing reply. It only seals
 *          LAN beacons, so outsiders can't read who is where; it opens
 *          no link (links need a roster key). A revoked node still has
 *          it: it can read beacons, nothing more.
 *   peers  last known address of each node, one line each:
 *          <sign_pk hex> <ipv4> <tcp port> <unix time seen>
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SN_MESH_H
#define SN_MESH_H

#include <stdbool.h>
#include <stdint.h>

#define SN_ADDR_MAX 1024

typedef struct {
	uint8_t pk[32];
	char ip[48];
	uint16_t port;
	uint64_t seen;
} sn_addr;

bool sn_secret_load(uint8_t out[32]);
bool sn_secret_save(const uint8_t secret[32]);

int sn_addrs_load(sn_addr *out, int max);
bool sn_addrs_save(const sn_addr *a, int n);
/* Updates or adds; returns the new count. */
int sn_addrs_set(sn_addr *a, int n, int max, const uint8_t pk[32], const char *ip, uint16_t port, uint64_t seen);

/* Forget the mesh: roster, secret and addresses. Identity stays. */
bool sn_mesh_forget(void);

#endif

/* ------------------------------------------------------------------------
 *   This node's identity: one Ed25519 key pair, made on first start and
 *   kept in the state directory (mode 0600). Ed25519 signs roster
 *   entries; the same key converted to X25519 is the node's Noise static
 *   key, so one key is the whole identity.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SN_IDENTITY_H
#define SN_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint8_t sign_pk[32];
	uint8_t sign_sk[64];
	uint8_t dh_pub[32];   /* X25519, from sign_pk */
	uint8_t dh_priv[32];  /* X25519, from sign_sk */
	char name[32];        /* hostname unless the "name" file says otherwise */
} sn_identity;

bool sn_identity_load(sn_identity *id); /* creates it if missing */

/* Ed25519 public key -> X25519, for matching a Noise static to a member. */
bool sn_identity_dh_of(const uint8_t sign_pk[32], uint8_t dh_pub_out[32]);

#endif

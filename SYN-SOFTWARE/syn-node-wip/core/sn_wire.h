/* ------------------------------------------------------------------------
 *   The wire, in one place. docs/protocol.md says the same in words.
 *
 *   TCP (default 47900): every connection opens with a 4-byte magic,
 *   then length-prefixed frames (u32 little-endian length, then bytes).
 *     "SNH1"  a link: Noise_IK handshake, then encrypted messages
 *     "SNP1"  pairing by code (only while a member shows a code)
 *
 *   UDP 47900 (always 47900, shared by every syn-noded on the machine):
 *     "SNPQ"                      who is showing a pairing code? (broadcast)
 *     "SNPA" port:u16             I am; pair with me on this TCP port
 *     "SNB1" nonce:24 box         beacon, sealed with the mesh secret:
 *                                 sign_pk:32 port:u16 time:u64
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SN_WIRE_H
#define SN_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sn_util.h"

#define SN_MAGIC_LINK "SNH1"
#define SN_MAGIC_PAIR "SNP1"
#define SN_UDP_PORT 47900

#define SN_MAX_FRAME (1024 * 1024)  /* pairing carries the whole roster */
#define SN_MAX_LINK_FRAME 65535     /* one Noise message */

/* Link handshake payloads, both directions: who I am. */
#define SN_HELLO_PAYLOAD 66         /* sign_pk:32 port:u16 name:32 */
#define SN_LINK_PROLOGUE "syn-node link 1"

/* Encrypted link messages: type:u8, then: */
enum {
	SN_MSG_HELLO = 1,    /* version:u16 roster_digest:32 */
	SN_MSG_ENTRIES = 2,  /* count:u16 entries[count] (169 bytes each) */
	SN_MSG_PING = 3,     /* stamp:u64 */
	SN_MSG_PONG = 4,     /* stamp:u64 (echoed) */
};
#define SN_ENTRIES_PER_MSG 300

/* Pairing, after "SNP1":
 *   member -> joiner   sid:16
 *   joiner -> member   Yj:32
 *   member -> joiner   Ym:32
 *   joiner -> member   seal(k_j2m, sign_pk:32 port:u16 name:32)
 *   member -> joiner   seal(k_m2j, secret:32 port:u16 name:32 sign_pk:32 roster)
 * where roster is the roster file's bytes ("SNR1" ...). A wrong code
 * fails at the joiner's sealed message: the member closes and burns the
 * code. */
#define SN_PAIR_ID_LEN 66

/* Blocking helpers for short-lived clients (the CLI's join). */
bool sn_write_all(int fd, const void *data, size_t len, int timeout_ms);
bool sn_read_exact(int fd, void *out, size_t len, int timeout_ms);
bool sn_send_frame(int fd, const void *data, size_t len, int timeout_ms);
/* Allocates *out (caller frees). */
bool sn_recv_frame(int fd, uint8_t **out, size_t *len, size_t max, int timeout_ms);

/* For the daemon's buffered reads: if buf holds a whole frame, points
 * *frame at it and returns its total size including the length prefix;
 * 0 if incomplete; -1 if the length is over max. */
long sn_frame_peek(const sn_buf *buf, size_t max, const uint8_t **frame, size_t *frame_len);
bool sn_frame_append(sn_buf *buf, const void *data, size_t len);

#endif

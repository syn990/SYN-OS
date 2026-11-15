/* ------------------------------------------------------------------------
 *   The mesh roster: who is in, signed by who let them in.
 *
 *   A mesh is named by its founder's key (mesh_id). Every entry is signed
 *   by a member; the founder signs its own. Who is a member is never
 *   stored, it is worked out from the entries every time:
 *
 *     members  = the founder, then anyone whose MEMBER entry is signed
 *                by a member (repeat until nothing changes)
 *     revoked  = anyone with a REVOKE entry signed by a member who is not
 *                revoked themselves (same loop)
 *     a revoked member's MEMBER entries dated at or after their
 *     revocation don't count
 *     active   = members - revoked
 *
 *   So entries are self-proving: nodes swap them freely (gossip) and
 *   merge whatever verifies, and every node reaches the same answer. A
 *   tampered byte breaks a signature and the entry stops counting.
 *   Revocation is permanent; a revoked key never comes back.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SN_ROSTER_H
#define SN_ROSTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SN_ROSTER_MAX 1024
#define SN_ENTRY_SIZE 169 /* kind 1, pk 32, name 32, signer 32, time 8, sig 64 */

enum { SN_ENTRY_FOUNDER = 1, SN_ENTRY_MEMBER = 2, SN_ENTRY_REVOKE = 3 };

typedef struct {
	uint8_t kind;
	uint8_t pk[32];
	char name[32];
	uint8_t signer[32];
	uint64_t time;
	uint8_t sig[64];
} sn_entry;

typedef struct {
	uint8_t pk[32];
	char name[32];
	bool revoked;
} sn_member;

typedef struct {
	bool has_mesh;
	uint8_t mesh_id[32];
	int count;
	sn_entry entries[SN_ROSTER_MAX];
	/* worked out by sn_roster_eval() */
	int member_count;
	sn_member members[SN_ROSTER_MAX];
} sn_roster;

void sn_entry_pack(const sn_entry *e, uint8_t out[SN_ENTRY_SIZE]);
void sn_entry_unpack(const uint8_t in[SN_ENTRY_SIZE], sn_entry *e);
bool sn_entry_verify(const uint8_t mesh_id[32], const sn_entry *e);
void sn_entry_sign(const uint8_t mesh_id[32], sn_entry *e, const uint8_t signer_sk[64]);

/* Starts a new mesh with this node as founder. */
void sn_roster_found(sn_roster *r, const uint8_t pk[32], const uint8_t sk[64], const char name[32]);
/* Adds an entry if it verifies and isn't already there. Returns true if
 * it was new. Re-evaluates. */
bool sn_roster_add(sn_roster *r, const sn_entry *e);
void sn_roster_eval(sn_roster *r);

/* NULL if not a member at all. */
const sn_member *sn_roster_find(const sn_roster *r, const uint8_t pk[32]);
bool sn_roster_is_active(const sn_roster *r, const uint8_t pk[32]);
/* By exact name or a hex prefix of at least 4 characters; NULL if none
 * or ambiguous. */
const sn_member *sn_roster_lookup(const sn_roster *r, const char *name_or_hex);

/* A digest over every entry's signature, sorted, so two nodes can tell
 * whether they hold the same entries without swapping them. */
void sn_roster_digest(const sn_roster *r, uint8_t out[32]);

/* Bytes (pairing) and disk: "SNR1" count:u32 mesh_id:32 entries. */
uint8_t *sn_roster_pack(const sn_roster *r, size_t *len_out); /* caller frees */
bool sn_roster_unpack(sn_roster *r, const uint8_t *data, size_t len);
bool sn_roster_load(sn_roster *r);
bool sn_roster_save(const sn_roster *r);
bool sn_roster_clear_disk(void);

#endif

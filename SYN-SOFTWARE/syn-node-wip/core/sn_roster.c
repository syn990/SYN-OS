/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "sn_roster.h"
#include "sn_util.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char SIGN_CONTEXT[] = "syn-node roster 1";

void sn_entry_pack(const sn_entry *e, uint8_t out[SN_ENTRY_SIZE]) {
	out[0] = e->kind;
	memcpy(out + 1, e->pk, 32);
	memcpy(out + 33, e->name, 32);
	memcpy(out + 65, e->signer, 32);
	sn_put64(out + 97, e->time);
	memcpy(out + 105, e->sig, 64);
}

void sn_entry_unpack(const uint8_t in[SN_ENTRY_SIZE], sn_entry *e) {
	e->kind = in[0];
	memcpy(e->pk, in + 1, 32);
	memcpy(e->name, in + 33, 32);
	e->name[31] = '\0';
	memcpy(e->signer, in + 65, 32);
	e->time = sn_get64(in + 97);
	memcpy(e->sig, in + 105, 64);
}

/* What gets signed: context, mesh, then every field but the signature. */
static void signed_bytes(const uint8_t mesh_id[32], const sn_entry *e, uint8_t out[sizeof(SIGN_CONTEXT) - 1 + 32 + 105]) {
	size_t c = sizeof(SIGN_CONTEXT) - 1;
	uint8_t packed[SN_ENTRY_SIZE];
	sn_entry_pack(e, packed);
	memcpy(out, SIGN_CONTEXT, c);
	memcpy(out + c, mesh_id, 32);
	memcpy(out + c + 32, packed, 105);
}

void sn_entry_sign(const uint8_t mesh_id[32], sn_entry *e, const uint8_t signer_sk[64]) {
	uint8_t msg[sizeof(SIGN_CONTEXT) - 1 + 32 + 105];
	signed_bytes(mesh_id, e, msg);
	crypto_sign_detached(e->sig, NULL, msg, sizeof(msg), signer_sk);
}

bool sn_entry_verify(const uint8_t mesh_id[32], const sn_entry *e) {
	if (e->kind < SN_ENTRY_FOUNDER || e->kind > SN_ENTRY_REVOKE) {
		return false;
	}
	if (e->kind == SN_ENTRY_FOUNDER &&
			(memcmp(e->pk, mesh_id, 32) != 0 || memcmp(e->signer, mesh_id, 32) != 0)) {
		return false;
	}
	uint8_t msg[sizeof(SIGN_CONTEXT) - 1 + 32 + 105];
	signed_bytes(mesh_id, e, msg);
	return crypto_sign_verify_detached(e->sig, msg, sizeof(msg), e->signer) == 0;
}

void sn_roster_found(sn_roster *r, const uint8_t pk[32], const uint8_t sk[64], const char name[32]) {
	memset(r, 0, sizeof(*r));
	r->has_mesh = true;
	memcpy(r->mesh_id, pk, 32);
	sn_entry e = {0};
	e.kind = SN_ENTRY_FOUNDER;
	memcpy(e.pk, pk, 32);
	memcpy(e.name, name, 32);
	memcpy(e.signer, pk, 32);
	e.time = sn_unix_time();
	sn_entry_sign(r->mesh_id, &e, sk);
	r->entries[r->count++] = e;
	sn_roster_eval(r);
}

bool sn_roster_add(sn_roster *r, const sn_entry *e) {
	if (!r->has_mesh || r->count >= SN_ROSTER_MAX) {
		return false;
	}
	for (int i = 0; i < r->count; i++) {
		if (memcmp(r->entries[i].sig, e->sig, 64) == 0) {
			return false;
		}
	}
	if (!sn_entry_verify(r->mesh_id, e)) {
		return false;
	}
	r->entries[r->count++] = *e;
	sn_roster_eval(r);
	return true;
}

/* ---- evaluation ---- */

typedef struct {
	uint8_t pk[32];
	uint64_t revoked_at; /* 0 = not revoked */
} key_state;

static int key_index(const key_state *keys, int n, const uint8_t pk[32]) {
	for (int i = 0; i < n; i++) {
		if (memcmp(keys[i].pk, pk, 32) == 0) return i;
	}
	return -1;
}

static const sn_entry *g_sort_entries;

static int cmp_entry_age(const void *a, const void *b) {
	const sn_entry *x = &g_sort_entries[*(const int *)a];
	const sn_entry *y = &g_sort_entries[*(const int *)b];
	if (x->time != y->time) return x->time < y->time ? -1 : 1;
	return memcmp(x->sig, y->sig, 64);
}

void sn_roster_eval(sn_roster *r) {
	r->member_count = 0;
	if (!r->has_mesh) {
		return;
	}

	static bool valid[SN_ROSTER_MAX];
	for (int i = 0; i < r->count; i++) {
		valid[i] = sn_entry_verify(r->mesh_id, &r->entries[i]);
	}

	static key_state members[SN_ROSTER_MAX], prev_members[SN_ROSTER_MAX];
	int n = 0, prev_n = 0;

	/* Members and revocations depend on each other, so loop until the
	 * pair stops changing. Each pass works members out from scratch,
	 * using the revocations the previous pass found. */
	static uint64_t revoked_at[SN_ROSTER_MAX], prev_rev_at[SN_ROSTER_MAX];
	static uint8_t revoked_pk[SN_ROSTER_MAX][32], prev_rev_pk[SN_ROSTER_MAX][32];
	int nrev = 0, prev_nrev = 0;

	static int revoke_order[SN_ROSTER_MAX];
	int nrevokes = 0;
	for (int i = 0; i < r->count; i++) {
		if (r->entries[i].kind == SN_ENTRY_REVOKE) revoke_order[nrevokes++] = i;
	}
	g_sort_entries = r->entries;
	qsort(revoke_order, (size_t)nrevokes, sizeof(int), cmp_entry_age);

	for (int pass = 0; pass < 32; pass++) {
		n = 0;
		/* founder */
		for (int i = 0; i < r->count; i++) {
			if (valid[i] && r->entries[i].kind == SN_ENTRY_FOUNDER) {
				memcpy(members[n].pk, r->mesh_id, 32);
				members[n].revoked_at = 0;
				n = 1;
				break;
			}
		}
		/* admissions */
		bool grew = true;
		while (grew) {
			grew = false;
			for (int i = 0; i < r->count; i++) {
				const sn_entry *e = &r->entries[i];
				if (!valid[i] || e->kind != SN_ENTRY_MEMBER || key_index(members, n, e->pk) >= 0) continue;
				if (key_index(members, n, e->signer) < 0) continue;
				/* a revoked signer's admissions from its revocation on don't count */
				bool late = false;
				for (int k = 0; k < nrev; k++) {
					if (memcmp(revoked_pk[k], e->signer, 32) == 0 && e->time >= revoked_at[k]) late = true;
				}
				if (late) continue;
				memcpy(members[n].pk, e->pk, 32);
				members[n].revoked_at = 0;
				n++;
				grew = true;
			}
		}
		/* revocations, signed by a member not itself revoked. Taken
		 * oldest first (ties by signature), never in arrival order: if A
		 * and B revoke each other, the earlier revocation wins on every
		 * node, whatever order the entries reached it in. */
		nrev = 0;
		grew = true;
		while (grew) {
			grew = false;
			for (int oi = 0; oi < nrevokes; oi++) {
				int i = revoke_order[oi];
				const sn_entry *e = &r->entries[i];
				if (!valid[i]) continue;
				if (key_index(members, n, e->signer) < 0) continue;
				bool signer_revoked = false, already = false;
				for (int k = 0; k < nrev; k++) {
					if (memcmp(revoked_pk[k], e->signer, 32) == 0) signer_revoked = true;
					if (memcmp(revoked_pk[k], e->pk, 32) == 0) {
						already = true;
						if (e->time < revoked_at[k]) revoked_at[k] = e->time;
					}
				}
				if (signer_revoked || already) continue;
				memcpy(revoked_pk[nrev], e->pk, 32);
				revoked_at[nrev] = e->time;
				nrev++;
				grew = true;
			}
		}
		/* stop when nothing moved: same members, same revocations, same
		 * times (the passes run in entry order, so equal sets come out in
		 * equal order) */
		bool same = pass > 0 && n == prev_n && nrev == prev_nrev &&
			memcmp(prev_members, members, sizeof(key_state) * (size_t)n) == 0 &&
			memcmp(prev_rev_pk, revoked_pk, 32 * (size_t)nrev) == 0 &&
			memcmp(prev_rev_at, revoked_at, sizeof(uint64_t) * (size_t)nrev) == 0;
		if (same) break;
		prev_n = n;
		prev_nrev = nrev;
		memcpy(prev_members, members, sizeof(key_state) * (size_t)n);
		memcpy(prev_rev_pk, revoked_pk, 32 * (size_t)nrev);
		memcpy(prev_rev_at, revoked_at, sizeof(uint64_t) * (size_t)nrev);
	}

	for (int i = 0; i < n; i++) {
		sn_member *m = &r->members[r->member_count++];
		memcpy(m->pk, members[i].pk, 32);
		m->revoked = false;
		for (int k = 0; k < nrev; k++) {
			if (memcmp(revoked_pk[k], m->pk, 32) == 0) m->revoked = true;
		}
		/* name: from the newest valid entry admitting this key */
		uint64_t best = 0;
		m->name[0] = '\0';
		for (int j = 0; j < r->count; j++) {
			const sn_entry *e = &r->entries[j];
			if (!valid[j] || e->kind == SN_ENTRY_REVOKE || memcmp(e->pk, m->pk, 32) != 0) continue;
			if (!m->name[0] || e->time >= best) {
				best = e->time;
				memcpy(m->name, e->name, 32);
				m->name[31] = '\0';
			}
		}
	}
}

const sn_member *sn_roster_find(const sn_roster *r, const uint8_t pk[32]) {
	for (int i = 0; i < r->member_count; i++) {
		if (memcmp(r->members[i].pk, pk, 32) == 0) return &r->members[i];
	}
	return NULL;
}

bool sn_roster_is_active(const sn_roster *r, const uint8_t pk[32]) {
	const sn_member *m = sn_roster_find(r, pk);
	return m && !m->revoked;
}

const sn_member *sn_roster_lookup(const sn_roster *r, const char *q) {
	const sn_member *hit = NULL;
	int hits = 0;
	for (int i = 0; i < r->member_count; i++) {
		if (strcmp(r->members[i].name, q) == 0) {
			hit = &r->members[i];
			hits++;
		}
	}
	if (hits == 1) return hit;
	if (hits > 1) return NULL;
	size_t qlen = strlen(q);
	if (qlen < 4 || qlen > 64) return NULL;
	for (int i = 0; i < r->member_count; i++) {
		char hex[65];
		sn_hex(r->members[i].pk, 32, hex);
		if (strncmp(hex, q, qlen) == 0) {
			hit = &r->members[i];
			hits++;
		}
	}
	return hits == 1 ? hit : NULL;
}

static int cmp_sig(const void *a, const void *b) {
	return memcmp(a, b, 64);
}

void sn_roster_digest(const sn_roster *r, uint8_t out[32]) {
	static uint8_t sigs[SN_ROSTER_MAX][64];
	for (int i = 0; i < r->count; i++) memcpy(sigs[i], r->entries[i].sig, 64);
	qsort(sigs, (size_t)r->count, 64, cmp_sig);
	crypto_generichash_blake2b_state st;
	crypto_generichash_blake2b_init(&st, NULL, 0, 32);
	crypto_generichash_blake2b_update(&st, r->mesh_id, 32);
	for (int i = 0; i < r->count; i++) crypto_generichash_blake2b_update(&st, sigs[i], 64);
	crypto_generichash_blake2b_final(&st, out, 32);
}

/* ---- bytes and disk ---- */

uint8_t *sn_roster_pack(const sn_roster *r, size_t *len_out) {
	size_t len = 40 + (size_t)r->count * SN_ENTRY_SIZE;
	uint8_t *data = malloc(len);
	if (!data) return NULL;
	memcpy(data, "SNR1", 4);
	sn_put32(data + 4, (uint32_t)r->count);
	memcpy(data + 8, r->mesh_id, 32);
	for (int i = 0; i < r->count; i++) sn_entry_pack(&r->entries[i], data + 40 + i * SN_ENTRY_SIZE);
	*len_out = len;
	return data;
}

bool sn_roster_unpack(sn_roster *r, const uint8_t *data, size_t len) {
	memset(r, 0, sizeof(*r));
	if (len < 40 || memcmp(data, "SNR1", 4) != 0) return false;
	uint32_t count = sn_get32(data + 4);
	if (count > SN_ROSTER_MAX || len != 40 + (size_t)count * SN_ENTRY_SIZE) return false;
	r->has_mesh = true;
	memcpy(r->mesh_id, data + 8, 32);
	for (uint32_t i = 0; i < count; i++) {
		sn_entry_unpack(data + 40 + i * SN_ENTRY_SIZE, &r->entries[r->count++]);
	}
	sn_roster_eval(r);
	return true;
}

bool sn_roster_load(sn_roster *r) {
	memset(r, 0, sizeof(*r));
	char path[512];
	if (!sn_state_path("roster", path, sizeof(path))) return false;
	uint8_t *data;
	size_t len;
	if (!sn_read_file(path, &data, &len, 40 + SN_ROSTER_MAX * SN_ENTRY_SIZE)) {
		return true; /* no mesh yet */
	}
	bool ok = sn_roster_unpack(r, data, len);
	free(data);
	if (!ok) sn_log("roster file %s is damaged", path);
	return ok;
}

bool sn_roster_save(const sn_roster *r) {
	char path[512];
	if (!sn_state_path("roster", path, sizeof(path))) return false;
	size_t len;
	uint8_t *data = sn_roster_pack(r, &len);
	if (!data) return false;
	bool ok = sn_write_file(path, data, len, 0600);
	free(data);
	return ok;
}

bool sn_roster_clear_disk(void) {
	char path[512];
	if (!sn_state_path("roster", path, sizeof(path))) return false;
	return unlink(path) == 0 || access(path, F_OK) != 0;
}

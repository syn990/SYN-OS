/* ------------------------------------------------------------------------
 *   Roster rules: admission chains, revocation, tampering, entries from
 *   another mesh, a revoked member's late admissions, and two nodes that
 *   receive the same entries in different orders reaching one answer.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (tests)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "sn_roster.h"
#include "sn_util.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

typedef struct {
	uint8_t pk[32], sk[64];
	char name[32];
} key;

static void mk(key *k, const char *name) {
	crypto_sign_keypair(k->pk, k->sk);
	sn_name_field(name, k->name);
}

static sn_entry entry(const uint8_t mesh[32], int kind, const key *subject, const key *signer, uint64_t t) {
	sn_entry e = {0};
	e.kind = (uint8_t)kind;
	memcpy(e.pk, subject->pk, 32);
	memcpy(e.name, subject->name, 32);
	memcpy(e.signer, signer->pk, 32);
	e.time = t;
	sn_entry_sign(mesh, &e, signer->sk);
	return e;
}

static sn_roster ra, rb;

int main(void) {
	if (sodium_init() < 0) return 1;
	key a, b, c, d, x;
	mk(&a, "alpha"); mk(&b, "bravo"); mk(&c, "charlie"); mk(&d, "delta"); mk(&x, "xray");

	sn_roster_found(&ra, a.pk, a.sk, a.name);
	CHECK(sn_roster_is_active(&ra, a.pk), "founder active");

	sn_entry eb = entry(ra.mesh_id, SN_ENTRY_MEMBER, &b, &a, 100);
	sn_entry ec = entry(ra.mesh_id, SN_ENTRY_MEMBER, &c, &b, 200);
	CHECK(sn_roster_add(&ra, &ec), "entry stored before its signer is a member");
	CHECK(!sn_roster_is_active(&ra, c.pk), "c not active until b is a member");
	CHECK(sn_roster_add(&ra, &eb), "add b");
	CHECK(sn_roster_is_active(&ra, b.pk) && sn_roster_is_active(&ra, c.pk), "chain a->b->c active");
	CHECK(!sn_roster_add(&ra, &eb), "duplicate refused");

	/* an outsider's admission doesn't count */
	sn_entry ed_by_x = entry(ra.mesh_id, SN_ENTRY_MEMBER, &d, &x, 300);
	sn_roster_add(&ra, &ed_by_x);
	CHECK(!sn_roster_is_active(&ra, d.pk), "admission by non-member ignored");

	/* entry from another mesh doesn't verify here */
	uint8_t other_mesh[32];
	memcpy(other_mesh, x.pk, 32);
	sn_entry foreign = entry(other_mesh, SN_ENTRY_MEMBER, &d, &a, 300);
	CHECK(!sn_roster_add(&ra, &foreign), "entry signed for another mesh refused");

	/* tampering */
	sn_entry tampered = entry(ra.mesh_id, SN_ENTRY_MEMBER, &d, &a, 400);
	tampered.name[0] ^= 1;
	CHECK(!sn_roster_add(&ra, &tampered), "tampered entry refused");

	/* revoke b: c (admitted by b before) stays; b's later admission of d doesn't count */
	sn_entry rev_b = entry(ra.mesh_id, SN_ENTRY_REVOKE, &b, &a, 500);
	sn_entry ed_late = entry(ra.mesh_id, SN_ENTRY_MEMBER, &d, &b, 600);
	sn_roster_add(&ra, &rev_b);
	sn_roster_add(&ra, &ed_late);
	CHECK(!sn_roster_is_active(&ra, b.pk), "b revoked");
	CHECK(sn_roster_is_active(&ra, c.pk), "c admitted before b's revocation stays");
	CHECK(!sn_roster_is_active(&ra, d.pk), "d admitted by b after revocation not active");
	/* revoked b can't revoke anyone */
	sn_entry rev_c_by_b = entry(ra.mesh_id, SN_ENTRY_REVOKE, &c, &b, 700);
	sn_roster_add(&ra, &rev_c_by_b);
	CHECK(sn_roster_is_active(&ra, c.pk), "revoked member's revocation ignored");

	/* mutual revocation, two arrival orders, one answer */
	sn_roster_found(&rb, a.pk, a.sk, a.name);
	sn_roster r2 = rb;
	key e1, e2;
	mk(&e1, "echo1"); mk(&e2, "echo2");
	sn_entry m1 = entry(rb.mesh_id, SN_ENTRY_MEMBER, &e1, &a, 10);
	sn_entry m2 = entry(rb.mesh_id, SN_ENTRY_MEMBER, &e2, &a, 10);
	sn_entry r12 = entry(rb.mesh_id, SN_ENTRY_REVOKE, &e2, &e1, 20); /* earlier: wins */
	sn_entry r21 = entry(rb.mesh_id, SN_ENTRY_REVOKE, &e1, &e2, 30);
	sn_roster_add(&rb, &m1); sn_roster_add(&rb, &m2); sn_roster_add(&rb, &r12); sn_roster_add(&rb, &r21);
	sn_roster_add(&r2, &r21); sn_roster_add(&r2, &r12); sn_roster_add(&r2, &m2); sn_roster_add(&r2, &m1);
	CHECK(sn_roster_is_active(&rb, e1.pk) && !sn_roster_is_active(&rb, e2.pk), "earlier revocation wins");
	CHECK(sn_roster_is_active(&r2, e1.pk) == sn_roster_is_active(&rb, e1.pk) &&
	      sn_roster_is_active(&r2, e2.pk) == sn_roster_is_active(&rb, e2.pk), "order independent");
	uint8_t d1[32], d2[32];
	sn_roster_digest(&rb, d1);
	sn_roster_digest(&r2, d2);
	CHECK(memcmp(d1, d2, 32) == 0, "digest order independent");

	/* lookup */
	CHECK(sn_roster_lookup(&ra, "charlie") != NULL, "lookup by name");
	char hex[65];
	sn_hex(c.pk, 32, hex);
	hex[8] = '\0';
	CHECK(sn_roster_lookup(&ra, hex) != NULL, "lookup by hex prefix");

	if (failures) {
		fprintf(stderr, "test_roster: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_roster: ok\n");
	return 0;
}

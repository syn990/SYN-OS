# SYN-NODE protocol, version 1

The wire contract between nodes. `core/sn_wire.h` is the same thing in
code; if they ever disagree, fix whichever is wrong, don't let them drift.
What's here is what's built (build step 1). Later channels get added
here when they're built.

All integers are little-endian. "Frame" means a u32 length followed by
that many bytes.

## Keys

- Each node: one Ed25519 key pair (`identity`, 64-byte secret key, 0600).
  The same key converted to X25519 is its Noise static key.
- Each mesh: named by its founder's Ed25519 public key, `mesh_id`.
- Each mesh: a 32-byte secret for sealing LAN beacons, made by the
  founder, handed to joiners in the pairing reply. It opens no links.

## Roster entry (169 bytes)

| field  | bytes | |
|--------|-------|-|
| kind   | 1  | 1 founder, 2 member, 3 revoke |
| pk     | 32 | the node the entry is about |
| name   | 32 | NUL-padded, printable ASCII |
| signer | 32 | the member who signed it (founder: itself) |
| time   | 8  | unix seconds, signer's clock |
| sig    | 64 | Ed25519 over `"syn-node roster 1" ‖ mesh_id ‖ the first 105 bytes` |

Membership rules are in `core/sn_roster.h`. In short: the founder, plus
anyone admitted by a member; minus anyone revoked by a member who isn't
revoked; revocations taken oldest first; a revoked member's admissions
from its revocation time on don't count.

Roster bytes (disk and pairing): `"SNR1" count:u32 mesh_id:32 entries`.

## TCP, port 47900 by default

Each connection starts with 4 bytes saying what it is.

### `SNH1`: a link

Noise_IK_25519_ChaChaPoly_BLAKE2b. Prologue: `"syn-node link 1" ‖ mesh_id`,
so nodes of different meshes can't complete a handshake.

| frame | from | contents |
|-------|------|----------|
| 1 | dialler | Noise message 1, payload `sign_pk:32 port:u16 name:32` |
| 2 | answerer | Noise message 2, same payload |

The answerer checks that the payload's `sign_pk` converts to the Noise
static key the handshake proved, and that it's an active member (not
itself). The dialler checks the answerer is the key it meant to reach.
Anything else: close, no reply.

After that, every frame is one Noise transport message (at most 65535
bytes) whose plaintext is `type:u8 body`:

| type | name | body |
|------|------|------|
| 1 | HELLO | `version:u16 roster_digest:32` |
| 2 | ENTRIES | `count:u16` then `count` roster entries (at most 300) |
| 3 | PING | `stamp:u64` |
| 4 | PONG | the PING's stamp, echoed |

Unknown types are ignored, so later versions can add channels.

- Both sides send HELLO first. `roster_digest` is BLAKE2b-256 over
  `mesh_id` and every entry's signature, sorted. If the other side's
  digest differs from yours, send all your entries; it merges what
  verifies. Entries that turn out new are passed on to every other link.
- PING every 15 s; a link with nothing received for 45 s is dropped.
- Two links between the same pair (both dialled at once): the one dialled
  by the lower `sign_pk` (bytewise) is kept; the other is closed after
  its handshake finishes.
- Dropped links are redialled with backoff, 2 s doubling to 60 s.

### `SNP1`: pairing

Only accepted while the member is showing a code (`syn-node pair`, 5
minutes), and only one at a time.

| frame | from | contents |
|-------|------|----------|
| 1 | member | `sid:16` random |
| 2 | joiner | `Yj:32` |
| 3 | member | `Ym:32` |
| 4 | joiner | `seal(k_j2m, sign_pk:32 port:u16 name:32)` |
| 5 | member | `seal(k_m2j, secret:32 port:u16 name:32 sign_pk:32 roster)` |

CPace on ristretto255:

- `g = ristretto255_from_hash(SHA-512("syn-node cpace 1" ‖ len(code):u8 ‖ code ‖ sid))`
- each side: random scalar `y`, sends `y·g`
- `K = y·Y_other` (identity point refused)
- `ISK = BLAKE2b-512("syn-node isk 1" ‖ sid ‖ K ‖ Yj ‖ Ym)`
- `k_j2m = BLAKE2b-256(key=ISK, "joiner to member")`,
  `k_m2j = BLAKE2b-256(key=ISK, "member to joiner")`
- `seal` is XChaCha20-Poly1305 with a zero nonce (each key seals one message)

If frame 4 doesn't open, the code was wrong: the member closes and the
code is spent. If it opens, the member admits the joiner (founding a mesh
first if it has none), sends frame 5 and closes. The joiner checks the
roster lists both itself and the member as active before keeping it.

The code is 8 decimal digits (the dialpad can type them). One guess per
code and a 5-minute life make that enough; CPace means watching the
exchange gives nothing to guess against offline.

## UDP, port 47900 always

Every syn-noded on a machine binds 47900 with `SO_REUSEADDR`, so all of
them hear broadcasts.

| packet | meaning |
|--------|---------|
| `"SNPQ"` | broadcast by a joiner: who is showing a code? |
| `"SNPA" port:u16` | a member showing a code answers with its TCP port |
| `"SNB1" nonce:24 box` | beacon every 10 s, `box = secretbox(mesh secret, sign_pk:32 port:u16 time:u64)` |

A beacon from a member a node has no link to makes it dial now, at the
beacon's source address and the port inside.

## Local control socket

`$XDG_RUNTIME_DIR/syn-node.sock`, mode 0600, one line per command, for
the CLI, station, menu and bar. Not a network protocol; it may change
freely between versions.

| command | answer |
|---------|--------|
| `STATUS` | `key value` lines, then `.` |
| `NODES` | `name|key|state|address|rtt_ms` lines (state: self, online, offline, revoked), then `.` |
| `PAIR` | `CODE dddd dddd`, later `OK <name>` or `FAIL <reason>`; closing the socket cancels |
| `REVOKE <name or key prefix>` | `OK <name>` or `FAIL <reason>` |
| `RELOAD` | `OK` (re-reads roster, secret, addresses after a join) |
| `LEAVE` | `OK` |

/* ------------------------------------------------------------------------
 *   SYN-CRYPTER shared types and helpers used by every algorithm module.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CRYPT_H
#define SYN_CRYPT_H

#include <stddef.h>

/* Every format starts with a magic so decrypt can identify which
 * algorithm/layout produced a file, and encrypt can refuse to re-wrap
 * one. AES/Blowfish/RSA use a 4-byte magic + 1-byte version. Redshirt
 * also accepts the two marker strings from the original Uplink game's
 * RedShirt library (lib/redshirt/redshirt.cpp, vb6mmorpg/uplink-source-code):
 * "REDSHIRT\0" v1 with no integrity check, "REDSHRT2\0" v2 with a SHA-1
 * of the ciphertext — plus a third, SYNX, this tool's own SHA-256-of-
 * plaintext variant. */
#define SYN_CRYPT_MAGIC_AES      "SYNA"
#define SYN_CRYPT_MAGIC_BLOWFISH "SYNB"
#define SYN_CRYPT_MAGIC_RSA      "SYNR"
#define SYN_CRYPT_MAGIC_LEN 4
#define SYN_CRYPT_VERSION 1

/* 9 bytes (string + embedded NUL), matching redshirt.cpp exactly — not
 * 4 bytes like the magics above, so sniffing/dispatch checks length first. */
#define SYN_CRYPT_MARKER_REDSHIRT  "REDSHIRT"
#define SYN_CRYPT_MARKER_REDSHRT2  "REDSHRT2"
#define SYN_CRYPT_MARKER_LEN 9
#define SYN_CRYPT_MAGIC_REDSHIRT_SYNX "SYNX"

typedef enum {
	SYN_REDSHIRT_V1_REDSHIRT = 0, /* no integrity check */
	SYN_REDSHIRT_V2_REDSHRT2,     /* SHA-1 of ciphertext */
	SYN_REDSHIRT_V3_SYNX,         /* SHA-256 of plaintext */
} syn_redshirt_variant;

typedef enum {
	SYN_CRYPT_OK = 0,
	SYN_CRYPT_ERR_IO,
	SYN_CRYPT_ERR_CRYPTO,
	SYN_CRYPT_ERR_BAD_PASSWORD,
	SYN_CRYPT_ERR_CORRUPT,
	SYN_CRYPT_ERR_FORMAT,
	SYN_CRYPT_ERR_ALREADY_ENCRYPTED, /* encrypt refused: input already looks encrypted */
	SYN_CRYPT_ERR_NOT_ENCRYPTED,     /* decrypt refused: input has no recognized magic/marker */
} syn_crypt_status;

/* Reads one line from stdin (the password/passphrase), stripping the
 * trailing newline, into a caller-owned buffer. Never touches argv, so
 * the secret never appears in `ps`. Returns 0 on success. */
int syn_crypt_read_secret(char *buf, size_t buflen);

/* Best-effort secure erase, used for password buffers and derived keys
 * before they go out of scope. */
void syn_crypt_wipe(void *buf, size_t len);

/* True if `path`'s leading bytes match any recognized syn-crypter magic
 * or Redshirt marker. Every encrypt_* function checks this on its input
 * first and refuses rather than double-wrapping an already-encrypted file. */
int syn_crypt_looks_encrypted(const char *path);

int syn_crypt_aes_encrypt(const char *in_path, const char *out_path, const char *password);
int syn_crypt_aes_decrypt(const char *in_path, const char *out_path, const char *password);

int syn_crypt_blowfish_encrypt(const char *in_path, const char *out_path, const char *password);
int syn_crypt_blowfish_decrypt(const char *in_path, const char *out_path, const char *password);

int syn_crypt_rsa_encrypt(const char *in_path, const char *out_path, const char *key_path);
int syn_crypt_rsa_decrypt(const char *in_path, const char *out_path, const char *key_path);

/* `variant` selects the on-disk format to write; decrypt detects the
 * variant from the file's own marker instead. */
int syn_crypt_redshirt_encrypt(const char *in_path, const char *out_path, syn_redshirt_variant variant);
int syn_crypt_redshirt_decrypt(const char *in_path, const char *out_path);

#endif

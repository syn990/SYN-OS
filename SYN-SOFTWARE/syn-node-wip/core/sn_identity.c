/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "sn_identity.h"
#include "sn_util.h"

#include <sodium.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool sn_identity_dh_of(const uint8_t sign_pk[32], uint8_t dh_pub_out[32]) {
	return crypto_sign_ed25519_pk_to_curve25519(dh_pub_out, sign_pk) == 0;
}

bool sn_identity_load(sn_identity *id) {
	memset(id, 0, sizeof(*id));
	char path[512];
	if (!sn_state_path("identity", path, sizeof(path))) {
		return false;
	}

	uint8_t *data;
	size_t len;
	if (sn_read_file(path, &data, &len, 1024) && len == 64) {
		memcpy(id->sign_sk, data, 64);
		sodium_memzero(data, len);
		free(data);
		crypto_sign_ed25519_sk_to_pk(id->sign_pk, id->sign_sk);
	} else {
		if (data) free(data);
		if (access(path, F_OK) == 0) {
			sn_log("identity file %s is damaged; refusing to replace it", path);
			return false;
		}
		crypto_sign_keypair(id->sign_pk, id->sign_sk);
		if (!sn_write_file(path, id->sign_sk, 64, 0600)) {
			sn_log("cannot write %s", path);
			return false;
		}
		sn_log("made a new identity in %s", path);
	}

	if (crypto_sign_ed25519_sk_to_curve25519(id->dh_priv, id->sign_sk) != 0 ||
			crypto_sign_ed25519_pk_to_curve25519(id->dh_pub, id->sign_pk) != 0) {
		return false;
	}

	char name[64] = "";
	if (sn_state_path("name", path, sizeof(path)) && sn_read_file(path, &data, &len, 63)) {
		memcpy(name, data, len);
		name[len] = '\0';
		free(data);
		name[strcspn(name, "\r\n")] = '\0';
	}
	if (!name[0]) {
		gethostname(name, sizeof(name) - 1);
	}
	if (!name[0]) {
		snprintf(name, sizeof(name), "node");
	}
	sn_name_field(name, id->name);
	return true;
}

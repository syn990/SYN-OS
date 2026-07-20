/* ------------------------------------------------------------------------
 *                        S Y N - C R Y P T E R
 *
 *   Encrypts/decrypts files with AES-256-GCM, Blowfish-CBC+HMAC, RSA
 *   (OAEP, hybrid via AES-256-GCM), or Redshirt (Uplink-style XOR
 *   obfuscation — a joke, not real security; see syn_crypt_redshirt.c
 *   for its three on-disk variants). Links libcrypto directly rather
 *   than shelling out to openssl(1).
 *
 *   Run with no arguments for the interactive ncurses dashboard (see
 *   syn_tui.h), themed from the live SYN-OS theme (see syn_theme.h).
 *   Flag form below is for scripting/menu.xml.
 *
 *   Passwords are read as a line from stdin, never argv — argv is
 *   visible to any other process via /proc or `ps aux` for as long as
 *   this process runs; stdin is not. RSA takes a key file path on argv
 *   instead, since a public/private key path isn't a secret itself.
 *
 *   Usage:
 *     syn-crypter                                          (interactive)
 *     syn-crypter --encrypt|--decrypt --aes|--blowfish <file>   (password on stdin)
 *     syn-crypter --encrypt|--decrypt --rsa <key.pem> <file>
 *     syn-crypter --encrypt --redshirt --redshirt|--redshrt2|--synx <file>
 *     syn-crypter --decrypt --redshirt <file>   (variant auto-detected)
 *
 *   Every encrypt refuses to run if the input already looks like a
 *   recognized syn-crypter/Redshirt file (see syn_crypt_looks_encrypted)
 *   instead of double-wrapping it.
 *
 *   Every mode encrypts/decrypts in place: writes to "<file>.tmp", then
 *   renames over the original.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_crypt.h"
#include "syn_tui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_PASSWORD_LEN 4096

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s                                                    (interactive)\n"
		"       %s --encrypt|--decrypt --aes|--blowfish <file>       (password on stdin)\n"
		"       %s --encrypt|--decrypt --rsa <key.pem> <file>\n"
		"       %s --encrypt --redshirt --redshirt|--redshrt2|--synx <file>\n"
		"       %s --decrypt --redshirt <file>                       (variant auto-detected)\n",
		argv0, argv0, argv0, argv0, argv0);
}

static const char *status_message(int status) {
	switch (status) {
	case SYN_CRYPT_OK: return "ok";
	case SYN_CRYPT_ERR_IO: return "I/O error (missing file, permissions, or disk full)";
	case SYN_CRYPT_ERR_CRYPTO: return "internal crypto operation failed";
	case SYN_CRYPT_ERR_BAD_PASSWORD: return "wrong password/key, or file is corrupted";
	case SYN_CRYPT_ERR_CORRUPT: return "file is corrupted or truncated";
	case SYN_CRYPT_ERR_FORMAT: return "not a recognized syn-crypter file for this algorithm";
	case SYN_CRYPT_ERR_ALREADY_ENCRYPTED: return "file is already encrypted (refusing to double-encrypt)";
	case SYN_CRYPT_ERR_NOT_ENCRYPTED: return "file is not a recognized encrypted format";
	default: return "unknown error";
	}
}

/* Shared by the CLI and the TUI: one encrypt/decrypt operation plus the
 * write-to-.tmp-then-rename dance. `key_or_password` is a password for
 * aes/blowfish, a PEM path for rsa, unused for redshirt. `redshirt_variant`
 * only matters for encrypt --redshirt. On failure, fills `err_out` (if
 * non-NULL) instead of printing — the TUI still owns the terminal when
 * this runs, and stderr output mid-ncurses corrupts the display until
 * the next redraw. */
static int run_operation(int encrypt, const char *algo, const char *file,
		const char *key_or_password, syn_redshirt_variant redshirt_variant,
		const char **err_out) {
	char tmp_path[4096];
	if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file) >= (int)sizeof(tmp_path)) {
		if (err_out) *err_out = "path too long";
		return 1;
	}

	int status;
	if (strcmp(algo, "aes") == 0) {
		status = encrypt
			? syn_crypt_aes_encrypt(file, tmp_path, key_or_password)
			: syn_crypt_aes_decrypt(file, tmp_path, key_or_password);
	} else if (strcmp(algo, "blowfish") == 0) {
		status = encrypt
			? syn_crypt_blowfish_encrypt(file, tmp_path, key_or_password)
			: syn_crypt_blowfish_decrypt(file, tmp_path, key_or_password);
	} else if (strcmp(algo, "rsa") == 0) {
		status = encrypt
			? syn_crypt_rsa_encrypt(file, tmp_path, key_or_password)
			: syn_crypt_rsa_decrypt(file, tmp_path, key_or_password);
	} else {
		status = encrypt
			? syn_crypt_redshirt_encrypt(file, tmp_path, redshirt_variant)
			: syn_crypt_redshirt_decrypt(file, tmp_path);
	}

	if (status != SYN_CRYPT_OK) {
		if (err_out) *err_out = status_message(status);
		remove(tmp_path);
		return 1;
	}

	if (rename(tmp_path, file) != 0) {
		if (err_out) *err_out = "succeeded but failed to replace original file";
		return 1;
	}

	return 0;
}

static int run_cli(int argc, char **argv) {
	if (argc < 4) {
		print_usage(argv[0]);
		return 1;
	}

	int encrypt;
	if (strcmp(argv[1], "--encrypt") == 0) {
		encrypt = 1;
	} else if (strcmp(argv[1], "--decrypt") == 0) {
		encrypt = 0;
	} else {
		print_usage(argv[0]);
		return 1;
	}

	const char *algo_flag = argv[2];
	const char *err = NULL;
	int rc;

	if (strcmp(algo_flag, "--aes") == 0 || strcmp(algo_flag, "--blowfish") == 0) {
		if (argc != 4) {
			print_usage(argv[0]);
			return 1;
		}
		char password[MAX_PASSWORD_LEN];
		if (syn_crypt_read_secret(password, sizeof(password)) != 0) {
			fprintf(stderr, "syn-crypter: failed to read password from stdin\n");
			return 1;
		}
		const char *algo = (strcmp(algo_flag, "--aes") == 0) ? "aes" : "blowfish";
		rc = run_operation(encrypt, algo, argv[3], password, SYN_REDSHIRT_V3_SYNX, &err);
		syn_crypt_wipe(password, sizeof(password));

	} else if (strcmp(algo_flag, "--rsa") == 0) {
		if (argc != 5) {
			print_usage(argv[0]);
			return 1;
		}
		rc = run_operation(encrypt, "rsa", argv[4], argv[3], SYN_REDSHIRT_V3_SYNX, &err);

	} else if (strcmp(algo_flag, "--redshirt") == 0) {
		if (encrypt) {
			if (argc != 5) {
				print_usage(argv[0]);
				return 1;
			}
			syn_redshirt_variant variant;
			if (strcmp(argv[3], "--redshirt") == 0) {
				variant = SYN_REDSHIRT_V1_REDSHIRT;
			} else if (strcmp(argv[3], "--redshrt2") == 0) {
				variant = SYN_REDSHIRT_V2_REDSHRT2;
			} else if (strcmp(argv[3], "--synx") == 0) {
				variant = SYN_REDSHIRT_V3_SYNX;
			} else {
				print_usage(argv[0]);
				return 1;
			}
			rc = run_operation(encrypt, "redshirt", argv[4], NULL, variant, &err);
		} else {
			if (argc != 4) {
				print_usage(argv[0]);
				return 1;
			}
			rc = run_operation(encrypt, "redshirt", argv[3], NULL, SYN_REDSHIRT_V3_SYNX, &err);
		}
	} else {
		print_usage(argv[0]);
		return 1;
	}

	if (rc != 0) {
		fprintf(stderr, "syn-crypter: %s: %s\n", encrypt ? "encrypt" : "decrypt", err ? err : "unknown error");
		return 1;
	}
	fprintf(stderr, "syn-crypter: %s complete\n", encrypt ? "encryption" : "decryption");
	return 0;
}

static int run_interactive(void) {
	syn_tui_init();

	const char *home = getenv("HOME");
	syn_tui_dashboard_result d = syn_tui_dashboard(home ? home : "/");
	if (!d.ok) {
		syn_tui_end();
		return 0;
	}

	static const syn_redshirt_variant variant_values[] = {
		SYN_REDSHIRT_V1_REDSHIRT, SYN_REDSHIRT_V2_REDSHRT2, SYN_REDSHIRT_V3_SYNX,
	};
	syn_redshirt_variant redshirt_variant = variant_values[d.redshirt_variant_index];

	const char *err = NULL;
	int rc = run_operation(d.encrypt, d.algo, d.file,
		d.key_or_password[0] ? d.key_or_password : NULL, redshirt_variant, &err);
	syn_crypt_wipe(d.key_or_password, sizeof(d.key_or_password));

	char body[512];
	if (rc == 0) {
		snprintf(body, sizeof(body), "%s %s: succeeded", d.encrypt ? "Encrypt" : "Decrypt", d.file);
	} else {
		snprintf(body, sizeof(body), "%s %s: FAILED — %s",
			d.encrypt ? "Encrypt" : "Decrypt", d.file, err ? err : "unknown error");
	}
	/* Not fprintf(stderr, ...) — ncurses still owns the terminal here. */
	syn_tui_message("Result", body);
	syn_tui_end();

	return rc;
}

int main(int argc, char **argv) {
	if (argc == 1) {
		return run_interactive();
	}
	return run_cli(argc, argv);
}

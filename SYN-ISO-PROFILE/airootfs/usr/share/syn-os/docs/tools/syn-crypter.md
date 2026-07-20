# SYN-CRYPTER

SYN-CRYPTER is a unified command-line interface for encrypting and
decrypting a single file at a time, dispatching to one of four algorithms:
**AES-256**, **Blowfish**, **RSA**, or **Redshirt** (a lightweight XOR
obfuscation — not real encryption; see [SYN-REDSHIRT](./syn-redshirt.md)
for why it's included here at all). AES-256, Blowfish, and RSA are real
cryptography, implemented entirely as thin wrappers around `openssl`; the
script itself holds no cryptographic code beyond argument parsing and
temp-file handling.

## Files

| File | Role |
|---|---|
| `/usr/lib/syn-os/syn-crypter.zsh` | The CLI itself — parses `--encrypt`/`--decrypt` plus an algorithm flag, does the work |
| `/usr/lib/syn-os/syn-crypter-prompt.zsh` | rofi front-end: collects action, algorithm, file, and password/key, then runs `syn-crypter.zsh` |

## CLI usage

```
syn-crypter.zsh --encrypt --aes <password> <file>
syn-crypter.zsh --decrypt --aes <password> <file>
syn-crypter.zsh --encrypt --blowfish <password> <file>
syn-crypter.zsh --decrypt --blowfish <password> <file>
syn-crypter.zsh --encrypt --rsa <public_key.pem> <file>
syn-crypter.zsh --decrypt --rsa <private_key.pem> <file>
syn-crypter.zsh --encrypt --redshirt <file>
syn-crypter.zsh --decrypt --redshirt <file>
```

Every mode operates **in place** — output overwrites the input file (via a
`.tmp`/`.aes_encrypted`/etc. intermediate that's `mv`'d over the original
once the operation succeeds), there is no separate output-path argument.

## Algorithms

### AES-256

`aes_encrypt`/`aes_decrypt` call `openssl enc -aes-256-cbc` with PBKDF2 key
derivation. A random 16-byte salt and 16-byte IV are generated with
`openssl rand -hex 16` each, passed explicitly via `-S`/`-iv`, and then
**prepended to the ciphertext file itself** with `dd ... seek=0
conv=notrunc` — the salt and IV travel with the file rather than being
derived implicitly by OpenSSL's own salting, so decryption reads the first
32 bytes back out (`head -c 32`) to recover them before calling `openssl
enc -d` with the same explicit `-S`/`-iv`.

### Blowfish

`blowfish_encrypt`/`blowfish_decrypt` call `openssl enc -bf` with
OpenSSL's own `-salt` handling (no manual salt/IV management, unlike AES
above). Blowfish is a legacy 64-bit-block cipher; SYN-CRYPTER offers it as
a supported algorithm but does not claim it as a modern recommendation over
AES-256.

### RSA

`rsa_encrypt`/`rsa_decrypt` call `openssl rsautl -encrypt`/`-decrypt`
directly against a PEM key file — a public key (`-pubin`) to encrypt, the
matching private key to decrypt. RSA here encrypts the file's raw bytes
directly rather than wrapping a symmetric key (no hybrid envelope scheme),
which is standard `rsautl` behavior but means the plaintext must fit within
the key's block-size limit — SYN-CRYPTER does not chunk or pre-compress
large files for RSA mode.

### Redshirt

`redshirt_encrypt`/`redshirt_decrypt` shell out to `syn-redshirt.zsh`'s
same XOR transform via a compiled C helper (`cc -O2`, cached at
`${TMPDIR:-/tmp}/syn-redshirt-core`) — see
[SYN-REDSHIRT](./syn-redshirt.md) for the actual byte-level algorithm and
why it's explicitly not real encryption. Selecting Redshirt through
SYN-CRYPTER's `--redshirt` flag is a thinner path than running
`syn-redshirt.zsh` directly: it takes no marker/header/checksum framing (no
`REDSHIRT`/`REDSHRT2` magic bytes, no SHA1 integrity check) — it's a raw
XOR pass with the mode (`e`/`d`) fixed by which `syn-crypter.zsh` flag was
used, not auto-detected from the file's own contents the way
`syn-redshirt.zsh` run standalone is.

## Desktop integration

**Applications > SYN-OS Tools > SYN-CRYPTER (AES/Blowfish/RSA file
encryption)** in the [labwc](../labwc.md) root menu runs
`syn-crypter-prompt.zsh` directly (no `foot` wrapper — the rofi picker is
already its own centered popup). The prompt flow:

1. Action: **Encrypt** or **Decrypt** (rofi list)
2. Algorithm: **AES-256**, **Blowfish**, **RSA**, or **Redshirt** (rofi list)
3. File path (rofi text input, defaults to `$HOME/`, `~` expanded manually)
4. Algorithm-specific: a password (AES/Blowfish, via a masked rofi password
   prompt), a `.pem` key path (RSA — public key for encrypt, private key
   for decrypt), or nothing extra (Redshirt)

The real work then runs inside `syn_popup::run`, the same popup-terminal
wrapper `syn-share-prompt.zsh` uses (see [SYN-SHARE](./syn-share.md)) — a
small terminal window that closes itself on completion rather than sitting
open. On exit, it fires a `notify-send` toast: `"<Action> <Algorithm>
succeeded: <filename>"` on success, or a critical-urgency `"... failed:
<filename>"` toast on nonzero exit. See
[Notifications](./notifications.md) for how the toast pipeline itself
works.

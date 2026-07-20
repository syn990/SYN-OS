# SYN-REDSHIRT

SYN-REDSHIRT is a single-file byte obfuscator. **It is explicitly not real
encryption.** The transform is a fixed, keyless XOR-equivalent byte shift —
there is no password, no key, no cryptographic algorithm of any kind
involved. Its own header comment states the concept is inspired by
Uplink's in-game "RedShirt" tool, a piece of hacking-game flavor rather
than a security claim. Anyone who has the file and knows (or guesses) that
it went through SYN-REDSHIRT can reverse it trivially — there is no secret
to attack because there is no secret at all.

## The actual algorithm

The byte transform is a tiny embedded C program, written out and compiled
on first use:

```c
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int encrypt = (argv[1][0] == 'e');
    int c;
    while ((c = getchar()) != EOF) {
        if (encrypt) putchar((c + 128) & 0xFF);
        else putchar((c - 128) & 0xFF);
    }
    return 0;
}
```

Every byte is transformed independently by adding (encrypt) or subtracting
(decrypt) 128 modulo 256 — arithmetically equivalent to flipping the
high bit, i.e. XOR against `0x80`. There is no key material, no per-byte
state, no diffusion between bytes: byte *N* of the output depends only on
byte *N* of the input. This is a **fixed, publicly-known, self-inverse
transform** — running it twice on the same file restores the original.
Compare this to [SYN-CRYPTER](./syn-crypter.md)'s AES-256/Blowfish/RSA
modes, which are real `openssl`-backed cryptography with actual key
material.

The C helper is compiled once with `cc -O2` and cached at
`${TMPDIR:-/tmp}/syn-redshirt-core`, reused on subsequent runs rather than
recompiled every invocation.

## File format and self-detection

Unlike [SYN-CRYPTER](./syn-crypter.md)'s Redshirt mode (which takes an
explicit `--encrypt`/`--decrypt` flag), `syn-redshirt.zsh` run directly
**auto-detects** which direction to run by reading the file's own header:

- **`REDSHRT2\0`** (9-byte marker) → the file is v2 output: decrypt it,
  verifying a stored SHA1 checksum first.
- **`REDSHIRT\0`** (9-byte marker) → the file is v1 output (no checksum):
  decrypt it without verification.
- Neither marker present → the file is untransformed: encrypt it.

Encrypting always writes the current (v2) format: a 9-byte `REDSHRT2\0`
marker, a 20-byte SHA1 hash of the transformed data, then the transformed
bytes themselves. The hash is computed *after* the XOR transform (over the
ciphertext, not the plaintext) and written back into the header with `dd
... seek=$markersize conv=notrunc` once known. Decrypting v2 recomputes
that SHA1 over the stored ciphertext and compares it against the header
value, reporting `checksum mismatch` (via `syn_ui::error`, non-fatal — it
still proceeds to decrypt) if the file was altered or truncated after
encryption. v1 files (bare `REDSHIRT\0` marker, no hash) are decrypted with
no such check, kept only for backward compatibility with files produced
before the v2 format existed.

Because direction is inferred from file content, `syn-redshirt.zsh
somefile` is the single command for both directions — there is no
`--encrypt`/`--decrypt` flag on the standalone tool.

## Usage

```
syn-redshirt.zsh <file>
```

Operates in place: writes to `<file>.tmp`, then `mv`s it over the original
once the transform completes.

## Desktop integration

**Applications > SYN-OS Tools > SYN-REDSHIRT (Uplink-style XOR
obfuscation)** in the [labwc](../labwc.md) root menu runs
`syn-redshirt-prompt.zsh` directly. The prompt asks only for a file path
(rofi text input, defaults to `$HOME/`) — no action selection, since
`syn-redshirt.zsh` itself determines encrypt-vs-decrypt from the file's
header. The real work runs inside `syn_popup::run` (the same
self-closing popup terminal `syn-crypter-prompt.zsh` and
`syn-share-prompt.zsh` use), and fires a `notify-send` toast on completion
— `"Succeeded: <filename>"` normally, or a critical-urgency `"Failed:
<filename>"` toast on nonzero exit. See
[Notifications](./notifications.md) for the toast pipeline itself.

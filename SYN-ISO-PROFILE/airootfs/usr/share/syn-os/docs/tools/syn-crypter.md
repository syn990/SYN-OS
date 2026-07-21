# Encryption

A simple tool for locking a single file with a password, or unlocking it
again. Find it in the main menu under SYN-OS Tools.

You'll be asked, in order: whether you're encrypting or decrypting, which
method to use, the file, and then a password or key depending on the
method. It runs, then pops up a notification saying whether it worked.

The file is changed in place, there's no separate output file to look
for.

## Which method to pick

- **AES-256.** The one to use by default. Strong, modern, needs a
  password.
- **Blowfish.** An older method, kept for compatibility. AES-256 is the
  better choice unless you have a specific reason to want this one.
- **RSA.** Uses a key file instead of a password, a public key to lock
  the file and the matching private key to unlock it. Useful if you
  already work with RSA keys for something else. Not meant for very
  large files.
- **Redshirt.** Not real encryption, just a bit of file scrambling for
  fun, named after the hacking tool from the game Uplink. Don't use it to
  protect anything you actually care about keeping private. It has no
  password: the same command reverses it again.

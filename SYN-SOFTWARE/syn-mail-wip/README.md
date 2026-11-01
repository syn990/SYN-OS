# syn-mail

A mail client for SYN-OS, built by the crew in the SYN-CAST family: a
native Linux program and, later, an Android APK, sharing one design and
one stand-in test server. It reads and sends for both of the operator's
domains: a Google Workspace mailbox (OAuth) and an IONOS mailbox
(password). Ticket G223; the plan is in the crew workspace,
`research/2026-11-01-syn-mail-plan.md`.

**Slices 1 and 2 are built: the command-line core and the window.** The
APK (slice 3) is not.

## Commands

```
syn-mail [-v] add NAME ADDRESS --preset google|ionos [--display NAME]
syn-mail password NAME            the mailbox password from stdin (no echo on a terminal) into the keyring
syn-mail login NAME [--no-browser] Google: one sign-in in the browser; the refresh token goes to the keyring
syn-mail list
syn-mail check [NAME] [--last N]  sign in, count the inbox and the unread, list the newest N (10)
syn-mail read NAME N              one message as text
syn-mail send NAME TO SUBJECT     the text on stdin, from NAME to TO
syn-mail folders NAME             the account's folders (modified UTF-7 names decoded)
syn-mail headers NAME [FOLDER] [--skip N] [--last N]   a page of a folder by UID, newest first
syn-mail show NAME FOLDER UID     one message by UID as text
syn-mail window [--shot FILE.png] [--size WxH] [--open N]   the window
```

Reading never marks mail read: folders are opened with EXAMINE and bodies
fetched with BODY.PEEK.

## The window

A native Wayland client (xdg-shell, server-side decorations where the
compositor offers them), drawn with pixman and fcft in the live SYN-OS
theme (`syn_theme_load`, shared with syn-crypter). One worker thread per
account keeps its own IMAP connection; the window never waits on the
network. Panes: every account with its folders, the open folder's
messages (unread marked, 50 to a page), the reader, and compose or reply
in place of the reader. The header shows each account's unread count; it
refreshes every two minutes and on F5.

Keys: arrows or h j k l move, Tab or left and right change pane, Enter
opens, PgUp and PgDn page, c new message, r reply, F5 refresh, q or
Ctrl+Q quit. In compose, Tab moves between To, Subject and the text,
Ctrl+V pastes, Ctrl+Enter sends, Esc drops it. The mouse clicks, double
clicks and scrolls.

`--shot` draws one frame offscreen once the first folder and message have
loaded and saves it as a PNG; no compositor is needed. `--open N` opens the
Nth message first.

`-v` traces the conversation with the server; sign-in lines are never shown.

## Accounts and secrets

- `~/.config/syn-mail/config.json` (folder 700, file 600) holds the
  accounts: address, From name, servers and ports, and how they sign in.
  It never holds a secret.
- Secrets live in the system keyring through libsecret, schema
  `org.syn-os.syn-mail`, attributes `service=syn-mail`, `account=NAME`,
  `kind=password|client_id|client_secret|refresh_token`.
- Presets: `google` is imap.gmail.com 993 and smtp.gmail.com 465 with
  XOAUTH2 (Google has refused plain passwords for IMAP and SMTP since
  14 March 2025); `ionos` is imap.ionos.co.uk 993 and smtp.ionos.co.uk
  465 with the mailbox password. Port 143 or 587 means STARTTLS.
- Google sign-in: a Desktop OAuth client (Internal consent screen, no
  verification needed), loopback redirect on 127.0.0.1 with PKCE S256,
  `access_type=offline`, scope `https://mail.google.com/`. The client id
  and secret are put in the keyring first (kinds `client_id`,
  `client_secret`); `login` adds the refresh token, and every `check` or
  `send` swaps it for a fresh access token.

## Files

| File | What it does |
|---|---|
| `src/net.c` | TCP (lifted from syn-cast's ws.c) and TLS: 1.2 at least, the peer and its name verified, 60 s timeouts |
| `src/imap.c` | Tagged commands, literals (refused past 64 MiB a reply, before any of it is read), LOGIN and AUTHENTICATE XOAUTH2, STARTTLS on 143 |
| `src/box.c` | Folders (LIST, modified UTF-7), a page of headers by UID, one body by UID, all read-only |
| `src/worker.c` | One thread per account: reads and sends off the window's thread, results back over a pipe |
| `src/ui.c` | The window: Wayland, pixman, fcft, xkbcommon, the theme, and `--shot` through libpng |
| `src/smtp.c` | EHLO, STARTTLS on 587, AUTH PLAIN or XOAUTH2, one recipient, dot-stuffing |
| `src/msg.c` | The outgoing message (from SYN-CASTER's fn_gmail.c): RFC 2047 headers, quoted names, base64 body, address and header-injection checks |
| `src/mime.c` | The incoming message: unfolded and decoded headers (B and Q words, any charset by iconv), multipart walked for text/plain, HTML reduced to text, base64 and quoted-printable |
| `src/oauth.c` | The Google loopback sign-in and the refresh grant (libcurl, HTTPS only) |
| `src/secret.c` | libsecret; a mode-600 test file instead when `SYN_MAIL_SECRETS_FILE` is set |
| `src/config.c` | The accounts file, written atomically |
| `tests/standin.py` | A stand-in IMAP, SMTP and token server on localhost with a throwaway certificate |
| `tests/run.sh` | The tests: never the real keyring, config or mail |

## Build and test

Needs openssl, jansson, libsecret, curl, cmake; for the window
wayland (and wayland-protocols, wayland-scanner), xkbcommon, fcft,
pixman, libpng and ncursesw (syn_theme.c); python3 and the openssl
command for the tests. The theme reader is built from
`../syn-crypter-src/src/syn_theme.c`.

```
make          # build/syn-mail
make test     # 80 checks against the stand-in
```

Test hooks (tests only): `SYN_MAIL_CONFIG`, `SYN_MAIL_SECRETS_FILE`,
`SYN_MAIL_CAFILE`, `SYN_MAIL_TOKEN_URL`, `SYN_MAIL_AUTH_URL`,
`SYN_MAIL_DRAWLOG` (every string the window draws, one a line) and
`SYN_MAIL_FONT`.

## Next

3. The APK: the core in Java, background checks with notifications, the
   Google token carried from the PC over SYN-Link.
4. IMAP IDLE, search, attachments, an offline cache, threads, a labwc
   menu entry and a PKGBUILD.

Not tested against a server: STARTTLS (ports 143 and 587 need a
privileged port in the stand-in). Both presets use implicit TLS.

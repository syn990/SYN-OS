# syn-mail

A mail client for SYN-OS, built by the crew in the SYN-CAST family: a
native Linux program and an Android APK, sharing one design and one
stand-in test server. It reads and sends for both of the operator's
domains: a Google Workspace mailbox (OAuth) and an IONOS mailbox
(password). Ticket G223; the plan is in the crew workspace,
`research/2026-11-01-syn-mail-plan.md`.

**Slices 1 to 3 are built: the command-line core, the window and the
phone app (`apk/`).**

## Commands

```
syn-mail [-v] add NAME ADDRESS --preset google|ionos [--display NAME] [--title TITLE]
syn-mail title NAME [TITLE]       the title under the name in the account's signature (none: clear it)
syn-mail password NAME            the mailbox password from stdin (no echo on a terminal) into the keyring
syn-mail login NAME [--no-browser] Google: one sign-in in the browser; the refresh token goes to the keyring
syn-mail list
syn-mail check [NAME] [--last N]  sign in, count the inbox and the unread, list the newest N (10)
syn-mail read NAME N              one message as text
syn-mail send NAME TO SUBJECT [--attach FILE]... [--plain]   the text on stdin, from NAME to TO, as a
                                  signed company letter: multipart/alternative, the text twin then the
                                  rendered HTML (signature block, Syntax Corporation footer and notice, in
                                  the pinned SYN-OS-BLUE look); each --attach adds one file (16 at most,
                                  18 MiB in all), the letter first inside multipart/mixed; --plain sends
                                  the text alone, unsigned, as before
syn-mail folders NAME             the account's folders (modified UTF-7 names decoded)
syn-mail headers NAME [FOLDER] [--skip N] [--last N]   a page of a folder by UID, newest first
syn-mail show NAME FOLDER UID     one message by UID as text
syn-mail html NAME FOLDER UID [--remote] [--open] [--png FILE] [--width N]
                                  one message's HTML as it was made, in a private page with its
                                  inline images; remote images held back unless --remote; --open
                                  puts it in a Chrome tab, --png has headless Chrome draw it
syn-mail window [--shot FILE.png] [--size WxH] [--open N] [--account NAME] [--folder FOLDER] [--images]
                                  the window
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
opens, PgUp and PgDn page, c new message, r reply, o open the message in
Chrome, i load its remote images, t text or HTML, F5 refresh, q or
Ctrl+Q quit. In compose, Tab moves between To, Subject and the text,
Ctrl+V pastes, Ctrl+Enter sends, Esc drops it. The mouse clicks, double
clicks and scrolls.

HTML mail (G265) is shown as it was made. The HTML part (chosen over the
plain one) goes into a private page, `$XDG_RUNTIME_DIR/syn-mail/` (folder
700, files 600), with every part it names by `cid:` written beside it, and a
headless Chrome (chromium, else google-chrome) draws it at the pane's width,
off the window's thread; a pane under 640 pixels gets the page laid out at
640 and drawn smaller, so nothing is cut off. The network is locked twice:
the page's Content-Security-Policy lets no script run and no remote image,
style or font load, and the drawing goes through a proxy to nowhere. DNS
prefetch and meta refresh are off. A line above the page says how many remote
images were held back; `i` loads them for that message only. `o` opens the
same page (remote images as the reader has them) in a new Chrome tab. With no
headless Chrome, or when the drawing fails, the reader shows the text, and
`t` switches to the text at any time.

`--shot` draws one frame offscreen once the first folder and message have
loaded and saves it as a PNG; no compositor is needed. `--open N` opens the
Nth message first, `--account` and `--folder` choose where, and `--images`
reads every message with its remote images, as `i` does.

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
| `src/letter.c` | The company letter (G265): the text twin and the HTML, tables and inline styles only, no images, scripts or links, the theme pinned to SYN-OS-BLUE; its notice is SYN-CASTER's fn_mail_disclaimer word for word |
| `src/mime.c` | The incoming message: unfolded and decoded headers (B and Q words, any charset by iconv), multipart walked for text/plain, HTML reduced to text, base64 and quoted-printable; the HTML part and its Content-ID parts for the HTML view |
| `src/html.c` | The HTML view: the guarded private page, cid: references resolved, the headless Chrome drawing, Chrome for `o`, the PNG read back |
| `src/oauth.c` | The Google loopback sign-in and the refresh grant (libcurl, HTTPS only) |
| `src/secret.c` | libsecret; a mode-600 test file instead when `SYN_MAIL_SECRETS_FILE` is set |
| `src/config.c` | The accounts file, written atomically |
| `tests/standin.py` | A stand-in IMAP, SMTP and token server on localhost with a throwaway certificate |
| `tests/run.sh` | The tests: never the real keyring, config or mail |
| `tests/pngcount.py` | Counts one colour's pixels in a PNG, for the HTML view's drawing checks |
| `apk/` | The phone app: `build.sh`, `test.sh`, the core and the screens (above) |

## The phone app (apk/)

A plain APK, no Gradle, built like syn-cast's: `apk/build.sh` runs aapt2,
javac (`--release 17 -Werror`), d8, zipalign and apksigner, and puts
`SYN-MAIL-APK-0.2.0.apk` in `~/Apps/SYN-Mail/`. The key is
`~/.config/syn-mail/apk-signing.jks` (made on the first build). Android 8
(API 26) and up.

- `apk/java/com/syntax990/synmail/core/` is the mail core in plain Java,
  no Android code: TLS (system trust, or a given CA for the stand-in;
  the host name checked), IMAP (LOGIN or XOAUTH2, EXAMINE and BODY.PEEK
  only, so reading never marks mail read; literals refused past 64 MiB;
  folder names with CR, LF or NUL refused), SMTP (PLAIN or XOAUTH2,
  dot-stuffing, the same header-injection checks as `msg.c`), the
  Google refresh grant, MIME and modified UTF-7. The desktop JVM tests it.
- The screens: the accounts with their unread count, folders, a page of
  25 messages ("Older" for the next), the reader with Reply, Write, and
  Add account (Gmail by app password, IONOS by mailbox password). Colours
  follow the SYN-OS theme through the launcher, as in syn-cast.
- HTML mail is shown as made, the phone's twin of the desktop's `html.c`:
  a WebView with script, files and network off; pictures sent with the
  message (cid:) put inline as data; every sender http-equiv renamed and
  our own Content-Security-Policy first; remote pictures held back and
  counted, shown only on Show pictures for that one message. Text shows
  the plain body instead.
- Secrets are sealed with AES-256-GCM under a key held in the Android
  Keystore (`syn-mail-secrets`); only the sealed text is stored. No
  backup (allowBackup false, no_backup.xml). No secret is in the APK.
- Bringing an account from the PC: `syn-mail export NAME FILE` (mode 600;
  for Google the refresh token and its client), `syn-link send FILE`, then
  Import from PC on the phone: it signs in once to prove the file, seals
  it, and offers to delete the file from Downloads.
- A background check every 15 minutes (JobScheduler, network required,
  kept over a reboot): STATUS UNSEEN on each inbox, and a notification
  when the count rises.
- `make apk-standin STANDIN=DIR` builds `com.syntax990.synmail.standin`,
  a separate package that trusts only the stand-in's certificate and
  carries its test account; it stays in `apk/build/`, is never copied to
  `~/Apps`, and reaches the PC with
  `adb reverse tcp:PORT tcp:PORT` for the three ports `build.sh` prints.

## Build and test

Needs openssl, jansson, libsecret, curl, cmake; for the window
wayland (and wayland-protocols, wayland-scanner), xkbcommon, fcft,
pixman, libpng and ncursesw (syn_theme.c); python3 and the openssl
command for the tests. To draw HTML mail: chromium or google-chrome (the
reader falls back to the text without one; the drawing tests are skipped). The theme reader is built from
`../syn-crypter-src/src/syn_theme.c`.

```
make          # build/syn-mail
make test     # 177 checks of the desktop client against the stand-in,
              # then 82 of the phone app's core (apk/test.sh, needs a JDK)
make apk      # the phone app (needs the Android SDK: platform 36, build-tools 36.1.0)
```

Test hooks (tests only): `SYN_MAIL_CONFIG`, `SYN_MAIL_SECRETS_FILE`,
`SYN_MAIL_CAFILE`, `SYN_MAIL_TOKEN_URL`, `SYN_MAIL_AUTH_URL`,
`SYN_MAIL_DRAWLOG` (every string the window draws, one a line),
`SYN_MAIL_FONT`, and `SYN_MAIL_CHROME` and `SYN_MAIL_BROWSER` (the headless
drawer and the browser `o` opens; `SYN_MAIL_CHROME=none` turns drawing off).

## Next

4. IMAP IDLE, search, attachments, an offline cache, threads, a labwc
   menu entry and a PKGBUILD.

Not tested against a server: STARTTLS (ports 143 and 587 need a
privileged port in the stand-in). Both presets use implicit TLS.

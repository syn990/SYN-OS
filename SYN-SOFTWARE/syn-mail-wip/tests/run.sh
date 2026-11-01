#!/bin/sh
# ------------------------------------------------------------------------
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-MAIL
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
# The stand-in tests: syn-mail against tests/standin.py, in a temp folder, never the real keyring, config or mail.
# Usage: tests/run.sh path/to/syn-mail
BIN=$(realpath "${1:-build/syn-mail}")
HERE=$(dirname "$(realpath "$0")")
T=$(mktemp -d "${TMPDIR:-/tmp}/syn-mail-test.XXXXXX")
PASS=0 FAIL=0
cleanup() { [ -n "$SP" ] && kill "$SP" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT INT TERM

ok() { PASS=$((PASS + 1)); echo "ok   $1"; }
no() { FAIL=$((FAIL + 1)); echo "FAIL $1"; [ -n "$2" ] && printf '%s\n' "$2" | sed 's/^/     /'; }
# has NAME TEXT PATTERN: TEXT contains PATTERN (fixed string)
has() { if printf '%s' "$2" | grep -qF -- "$3"; then ok "$1"; else no "$1" "$2"; fi; }
hasnt() { if printf '%s' "$2" | grep -qF -- "$3"; then no "$1" "$2"; else ok "$1"; fi; }

openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=localhost -addext subjectAltName=DNS:localhost \
	-keyout "$T/key.pem" -out "$T/cert.pem" >/dev/null 2>&1 || { echo "FAIL openssl could not make the test certificate"; exit 1; }
python3 "$HERE/standin.py" "$T" >"$T/standin.out" 2>&1 &
SP=$!
i=0
while [ ! -s "$T/ports" ] && [ $i -lt 100 ]; do i=$((i + 1)); python3 -c 'import time; time.sleep(0.05)'; done
[ -s "$T/ports" ] || { echo "FAIL the stand-in did not start"; cat "$T/standin.out"; exit 1; }
read -r PI PS PH <"$T/ports"

export SYN_MAIL_CONFIG="$T/conf/config.json" SYN_MAIL_SECRETS_FILE="$T/secrets.json" SYN_MAIL_CAFILE="$T/cert.pem"
export SYN_MAIL_TOKEN_URL="https://localhost:$PH/token"
unset SYN_MAIL_AUTH_URL

# 1. accounts: presets, the config's modes, no secret in it
"$BIN" add pw pw@example.test --preset ionos --display 'Pat "the" Tester' >/dev/null &&
"$BIN" add oa oauth@example.test --preset google >/dev/null; r=$?
[ $r -eq 0 ] && ok "add two accounts" || no "add two accounts" "exit $r"
L=$("$BIN" list)
has "list shows the IONOS preset" "$L" "imap imap.ionos.co.uk:993  smtp smtp.ionos.co.uk:465  password"
has "list shows the Google preset with OAuth" "$L" "imap imap.gmail.com:993  smtp smtp.gmail.com:465  oauth"
[ "$(stat -c %a "$T/conf")" = 700 ] && [ "$(stat -c %a "$SYN_MAIL_CONFIG")" = 600 ] && ok "config folder 700, file 600" ||
	no "config folder 700, file 600" "$(stat -c '%a %n' "$T/conf" "$SYN_MAIL_CONFIG")"
O=$("$BIN" add bad 'x@y.z>' --preset ionos 2>&1); r=$?
[ $r -ne 0 ] && ok "add refuses a malformed address" || no "add refuses a malformed address" "$O"

# point both accounts at the stand-in
python3 - "$SYN_MAIL_CONFIG" "$PI" "$PS" <<'PY'
import json, sys
p = sys.argv[1]; c = json.load(open(p))
for a in c["accounts"]:
    a.update(imap_host="localhost", imap_port=int(sys.argv[2]), smtp_host="localhost", smtp_port=int(sys.argv[3]))
json.dump(c, open(p, "w"))
PY

# 2. secrets: stdin password, the file's mode, a world-readable file refused
printf '%s\n' 's3cret pass"word' | "$BIN" password pw >/dev/null; r=$?
[ $r -eq 0 ] && [ "$(stat -c %a "$SYN_MAIL_SECRETS_FILE")" = 600 ] && ok "password stored, secrets file 600" ||
	no "password stored, secrets file 600" "exit $r $(stat -c %a "$SYN_MAIL_SECRETS_FILE" 2>&1)"
hasnt "the config holds no secret" "$(cat "$SYN_MAIL_CONFIG")" "s3cret"
chmod 644 "$SYN_MAIL_SECRETS_FILE"
O=$("$BIN" check pw 2>&1); r=$?
[ $r -ne 0 ] && has "a world-readable secrets file is refused" "$O" "readable by others" || no "a world-readable secrets file is refused" "$O"
chmod 600 "$SYN_MAIL_SECRETS_FILE"

# 3. IMAP with a password
O=$("$BIN" check pw --last 5 2>&1); r=$?
[ $r -eq 0 ] && ok "check signs in with LOGIN" || no "check signs in with LOGIN" "$O"
has "check counts the inbox and the unread" "$O" "pw <pw@example.test>: 3 in the inbox, 2 unread"
has "check decodes a folded UTF-8 B subject" "$O" "Café réunion"
has "check decodes an encoded From name" "$O" "Café Club <club@example.test>"
has "check decodes an ISO-8859-1 Q subject" "$O" "Grüße aus Berlin"
has "check lists newest first, unread marked" "$(printf '%s\n' "$O" | sed -n 2p)" "     3 * Sun, 01 Nov 2026 09:00:00 +0000"
has "check leaves read mail unmarked" "$O" "     1   Sat, 31 Oct 2026 10:00:00 +0000"

# 4. reading: multipart prefers text/plain, QP and soft breaks, attachment skipped; HTML-only reduced to text
O=$("$BIN" read pw 2 2>&1)
has "read decodes quoted-printable UTF-8" "$O" "Your café order is on its way, and this line is long enough to be wrapped by a soft break."
hasnt "read prefers text/plain over HTML" "$O" "HTML VERSION"
hasnt "read leaves attachments out" "$O" "QVRUQUNITUVOVA"
O=$("$BIN" read pw 3 2>&1)
has "read reduces HTML to text, entities decoded" "$O" "Fish & chips"
has "read converts ISO-8859-1 to UTF-8" "$O" "à bientôt"
hasnt "read drops tags and style" "$O" "<p>"
hasnt "read drops the style block" "$O" "p{x}"
O=$("$BIN" read pw 9 2>&1); r=$?
[ $r -ne 0 ] && ok "read of a missing message fails" || no "read of a missing message fails" "$O"

# 5. IMAP with OAuth (refresh grant, then XOAUTH2)
O=$("$BIN" check oa 2>&1)
has "OAuth: no client yet is named" "$O" "no OAuth client in the keyring for oa"
printf '{"oa/client_id":"cid","oa/client_secret":"csec","pw/password":"s3cret pass\\"word"}' >"$SYN_MAIL_SECRETS_FILE"
O=$("$BIN" check oa 2>&1)
has "OAuth: no token yet says how to sign in" "$O" "syn-mail login oa"
printf '{"oa/client_id":"cid","oa/client_secret":"csec","oa/refresh_token":"rt-good","pw/password":"s3cret pass\\"word"}' >"$SYN_MAIL_SECRETS_FILE"
O=$("$BIN" check oa --last 1 2>&1); r=$?
[ $r -eq 0 ] && has "OAuth: check signs in with XOAUTH2" "$O" "oa <oauth@example.test>: 3 in the inbox" ||
	no "OAuth: check signs in with XOAUTH2" "$O"
has "the stand-in saw XOAUTH2 for the right mailbox" "$(cat "$T/log")" "imap XOAUTH2 oauth@example.test"
O=$("$BIN" -v check oa --last 1 2>&1)
hasnt "the trace hides the OAuth line" "$O" "AUTHENTICATE XOAUTH2 "
O=$("$BIN" -v check pw --last 1 2>&1)
hasnt "the trace hides the password" "$O" "s3cret"
python3 - "$SYN_MAIL_SECRETS_FILE" <<'PY'
import json, sys; p = sys.argv[1]; s = json.load(open(p)); s["oa/refresh_token"] = "rt-revoked"; json.dump(s, open(p, "w"))
PY
O=$("$BIN" check oa 2>&1); r=$?
[ $r -ne 0 ] && has "OAuth: a revoked token is reported" "$O" "invalid_grant" || no "OAuth: a revoked token is reported" "$O"
python3 - "$SYN_MAIL_SECRETS_FILE" <<'PY'
import json, sys; p = sys.argv[1]; s = json.load(open(p)); s["oa/refresh_token"] = "rt-good"; s["pw/password"] = "wrong"; json.dump(s, open(p, "w"))
PY
O=$("$BIN" check pw 2>&1); r=$?
[ $r -ne 0 ] && has "a wrong password is reported as refused" "$O" "refused the sign-in for pw" || no "a wrong password is reported as refused" "$O"
python3 - "$SYN_MAIL_SECRETS_FILE" <<'PY'
import json, sys; p = sys.argv[1]; s = json.load(open(p)); s["pw/password"] = 's3cret pass"word'; json.dump(s, open(p, "w"))
PY

# 5b. the Google sign-in: loopback, state, PKCE, the refresh token stored
export SYN_MAIL_AUTH_URL="https://localhost:$PH/auth"
"$BIN" login oa --no-browser 2>"$T/login.err" >"$T/login.out" &
LP=$!
i=0
while ! grep -q "^https://" "$T/login.err" 2>/dev/null && [ $i -lt 100 ]; do i=$((i + 1)); python3 -c 'import time; time.sleep(0.05)'; done
URL=$(grep "^https://" "$T/login.err")
python3 - "$URL" "$T" <<'PY'
import sys, urllib.parse
u = urllib.parse.urlparse(sys.argv[1]); q = urllib.parse.parse_qs(u.query)
open(sys.argv[2] + "/challenge", "w").write(q["code_challenge"][0])
open(sys.argv[2] + "/redir", "w").write(q["redirect_uri"][0] + " " + q["state"][0] + " " + q["scope"][0] + " " +
    q["code_challenge_method"][0] + " " + q["access_type"][0] + " " + q["login_hint"][0])
PY
read -r REDIR STATE SCOPE METHOD ACCESS HINT <"$T/redir"
[ "$SCOPE $METHOD $ACCESS $HINT" = "https://mail.google.com/ S256 offline oauth@example.test" ] &&
	ok "login asks for full mail, PKCE S256, offline, the right account" || no "login asks for full mail, PKCE S256, offline, the right account" "$SCOPE $METHOD $ACCESS $HINT"
W=$(curl -s "$REDIR/?state=wrong&code=code-1")
has "login turns away a reply with the wrong state" "$W" "Not a SYN-Mail sign-in."
W=$(curl -s "$REDIR/?state=$STATE&code=code-1&scope=x")
has "login thanks the browser" "$W" "SYN-Mail is signed in."
wait $LP; r=$?
[ $r -eq 0 ] && has "login exchanges the code and stores the refresh token" "$(cat "$SYN_MAIL_SECRETS_FILE")" '"oa/refresh_token": "rt-from-login"' ||
	no "login exchanges the code and stores the refresh token" "exit $r $(cat "$T/login.err")"
O=$("$BIN" check oa --last 0 2>&1)
has "the new token signs in" "$O" "oa <oauth@example.test>: 3 in the inbox"
unset SYN_MAIL_AUTH_URL

# 6. TLS: the stand-in's certificate is refused without its CA, and for the wrong name
O=$(SYN_MAIL_CAFILE= "$BIN" check pw 2>&1); r=$?
[ $r -ne 0 ] && has "TLS: an unknown certificate is refused" "$O" "TLS to localhost failed" || no "TLS: an unknown certificate is refused" "$O"
sed -i 's/"localhost"/"127.0.0.1"/g' "$SYN_MAIL_CONFIG"
O=$("$BIN" check pw 2>&1); r=$?
[ $r -ne 0 ] && ok "TLS: a certificate for another name is refused" || no "TLS: a certificate for another name is refused" "$O"
sed -i 's/"127.0.0.1"/"localhost"/g' "$SYN_MAIL_CONFIG"

# 6b. folders, pages and the window (slice 2): read-only, nothing marked read
O=$("$BIN" folders pw 2>&1); r=$?
[ $r -eq 0 ] && has "folders lists the Inbox first" "$(printf '%s\n' "$O" | sed -n 1p)" "Inbox	INBOX" || no "folders lists the Inbox first" "$O"
has "folders decodes modified UTF-7" "$O" "Été	&AMk-t&AOk-"
has "folders marks a folder that holds no messages" "$O" "[Gmail] (no messages)"
has "folders reads a name sent as a literal" "$O" "Literal/1"
O=$("$BIN" headers pw 2>&1); r=$?
[ $r -eq 0 ] && has "headers counts the folder" "$O" "pw INBOX: 3 messages, 2 unread" || no "headers counts the folder" "$O"
has "headers lists by UID, newest first, unread marked" "$(printf '%s\n' "$O" | sed -n 2p)" "     103 * "
has "headers splits the sender's name from the address" "$O" "Café Club                       Café réunion"
has "headers decodes the subject" "$O" "Grüße aus Berlin"
O=$("$BIN" headers pw INBOX --skip 1 --last 1 2>&1)
has "headers pages back with --skip" "$O" "     102 * "
hasnt "headers --last 1 shows one row" "$O" "     103"
O=$("$BIN" headers pw Sent 2>&1)
has "headers reads another folder" "$O" "Where is my order"
O=$("$BIN" show pw INBOX 102 2>&1); r=$?
[ $r -eq 0 ] && has "show reads a message by UID" "$O" "Your café order is on its way" || no "show reads a message by UID" "$O"
O=$("$BIN" show pw INBOX 999 2>&1); r=$?
[ $r -ne 0 ] && ok "show of a missing UID fails" || no "show of a missing UID fails" "$O"
S0=$(date +%s)
O=$(timeout 20 "$BIN" show pw INBOX 666 2>&1); r=$?
S1=$(date +%s)
[ $r -ne 0 ] && [ $r -ne 124 ] && has "show refuses a reply too large to read" "$O" "too large to read" || no "show refuses a reply too large to read" "$O (exit $r)"
[ $((S1 - S0)) -lt 10 ] && ok "the refusal is at once, nothing of it read" || no "the refusal is at once, nothing of it read" "$((S1 - S0)) s"
has "the server did announce the huge reply" "$(cat "$T/log")" "imap UID FETCH 666 huge literal announced"
O=$("$BIN" headers pw '[Gmail]' 2>&1); r=$?
[ $r -ne 0 ] && ok "headers of a folder that holds no messages fails" || no "headers of a folder that holds no messages fails" "$O"
# a folder name with a line break would end the command and smuggle in a second one (SELECT marks mail read)
BADNAME=$(printf 'INBOX\r\nX1 SELECT INBOX')
O=$("$BIN" headers pw "$BADNAME" 2>&1); r=$?
[ $r -ne 0 ] && has "headers refuses a folder name with a line break" "$O" "line break" || no "headers refuses a folder name with a line break" "$O (exit $r)"
O=$("$BIN" show pw "$(printf 'INBOX\nX2 SELECT INBOX')" 102 2>&1); r=$?
[ $r -ne 0 ] && has "show refuses a folder name with a line break" "$O" "line break" || no "show refuses a folder name with a line break" "$O (exit $r)"
hasnt "no smuggled command reached the server" "$(cat "$T/log")" "imap SELECT"
: >"$T/draw"
O=$(SYN_MAIL_DRAWLOG="$T/draw" WAYLAND_DISPLAY= "$BIN" window --shot "$T/shot.png" --size 1280x800 2>&1); r=$?
[ $r -eq 0 ] && ok "window --shot draws without a compositor" || no "window --shot draws without a compositor" "$O"
P=$(python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read(24); print(d[:8]==b"\x89PNG\r\n\x1a\n", *struct.unpack(">II", d[16:24]))' "$T/shot.png" 2>&1)
has "the shot is a 1280x800 PNG" "$P" "True 1280 800"
[ -n "$SYN_MAIL_SHOT_OUT" ] && cp "$T/shot.png" "$T/shot2.png" "$SYN_MAIL_SHOT_OUT"/ 2>/dev/null
D=$(cat "$T/draw")
has "the window shows the folders" "$D" "Été"
has "the window lists the inbox" "$D" "Your order"
has "the window shows the sender's name" "$D" "Chip Shop"
has "the window opens the newest message" "$D" "Grüße aus Berlin"
has "the window shows its text" "$D" "Fish & chips"
has "the window counts the folder" "$D" "3 messages, 2 unread"
has "the window counts the other account's inbox" "$D" "oa 2 unread"
O=$(SYN_MAIL_DRAWLOG="$T/draw2" WAYLAND_DISPLAY= "$BIN" window --shot "$T/shot2.png" --open 2 2>&1)
has "window --open 2 reads the second message" "$(cat "$T/draw2" 2>/dev/null)" "Your café order is on its way"
O=$(WAYLAND_DISPLAY=nowhere-$$ "$BIN" window 2>&1); r=$?
[ $r -ne 0 ] && has "window without a desktop says so" "$O" "no Wayland display" || no "window without a desktop says so" "$O"
hasnt "nothing was ever marked read" "$(cat "$T/log")" "marks read"
has "bodies were fetched with PEEK" "$(cat "$T/log")" "imap UID FETCH 103 PEEK"

# 6b. export for the phone app: one file, mode 600, the account and its keyring secrets, never through a link
O=$("$BIN" export pw "$T/pw.syn-mail" 2>&1); r=$?
[ $r -eq 0 ] && [ "$(stat -c %a "$T/pw.syn-mail")" = 600 ] && ok "export writes the account file mode 600" ||
	no "export writes the account file mode 600" "$O"
X=$(python3 -c 'import json,sys; j=json.load(open(sys.argv[1])); print(j["syn_mail_account"], j["address"], j["imap_host"], j["auth"], j["secrets"]["password"])' "$T/pw.syn-mail" 2>&1)
has "the export carries the account and its password" "$X" '1 pw@example.test localhost password s3cret pass"word'
O=$("$BIN" export oa "$T/oa.syn-mail" 2>&1); r=$?
X=$(python3 -c 'import json,sys; j=json.load(open(sys.argv[1])); s=j["secrets"]; print(j["auth"], sorted(s), s["client_id"])' "$T/oa.syn-mail" 2>&1)
[ $r -eq 0 ] && has "the Google export carries the refresh token and the client" "$X" "oauth ['client_id', 'client_secret', 'refresh_token'] cid" ||
	no "the Google export carries the refresh token and the client" "$O $X"
ln -s "$T/elsewhere" "$T/link.syn-mail"
O=$("$BIN" export pw "$T/link.syn-mail" 2>&1); r=$?
[ $r -ne 0 ] && [ ! -e "$T/elsewhere" ] && ok "export will not write through a link" || no "export will not write through a link" "$O"
O=$("$BIN" export nobody "$T/x.syn-mail" 2>&1); r=$?
[ $r -ne 0 ] && [ ! -e "$T/x.syn-mail" ] && ok "export of an unknown account writes nothing" || no "export of an unknown account writes nothing" "$O"
[ -n "$SYN_MAIL_EXPORT_OUT" ] && cp "$T/pw.syn-mail" "$T/oa.syn-mail" "$SYN_MAIL_EXPORT_OUT"/ 2>/dev/null

# 7. sending: AUTH PLAIN and XOAUTH2, the message as built, header injection, bad recipients
printf 'Hello,\n.leading dot line\nCafé.\n' | "$BIN" send pw to@example.test 'Résumé
Bcc: evil@example.test' >"$T/o" 2>&1; r=$?
[ $r -eq 0 ] && ok "send with AUTH PLAIN" || no "send with AUTH PLAIN" "$(cat "$T/o")"
E=$(cat "$T/sent/1.eml" 2>/dev/null)
has "the stand-in saw AUTH PLAIN for the right mailbox" "$(cat "$T/log")" "smtp PLAIN pw@example.test"
has "sent: one recipient, the one asked" "$E" "X-Standin-Rcpt: <to@example.test>"
has "sent: From carries the quoted display name" "$E" 'From: "Pat \"the\" Tester" <pw@example.test>'
has "sent: non-ASCII subject is RFC 2047 encoded" "$E" "Subject: =?UTF-8?B?"
hasnt "sent: a newline in the subject cannot add a header" "$(printf '%s' "$E" | grep -i '^Bcc:')" "Bcc"
has "sent: Message-ID at the sender's domain" "$E" "@example.test>"
B=$(python3 -c 'import email,sys; m=email.message_from_bytes(open(sys.argv[1],"rb").read()); print(m.get_payload(decode=True).decode()); print(m["Subject"] and str(email.header.make_header(email.header.decode_header(m["Subject"]))))' "$T/sent/1.eml" 2>&1)
has "sent: the body arrives intact" "$B" "Hello,
.leading dot line
Café."
has "sent: the subject decodes, newline flattened" "$B" "Résumé Bcc: evil@example.test"
printf 'Via OAuth\n' | "$BIN" send oa to@example.test 'Plain subject' >"$T/o" 2>&1; r=$?
[ $r -eq 0 ] && has "send with XOAUTH2" "$(cat "$T/log")" "smtp XOAUTH2 oauth@example.test" || no "send with XOAUTH2" "$(cat "$T/o")"
O=$(printf 'x' | "$BIN" send pw 'to@example.test>
RCPT TO:<x@y.z' 'S' 2>&1); r=$?
[ $r -ne 0 ] && [ ! -e "$T/sent/3.eml" ] && ok "send refuses an address that would inject a command" || no "send refuses an address that would inject a command" "$O"
python3 - "$SYN_MAIL_SECRETS_FILE" <<'PY'
import json, sys; p = sys.argv[1]; s = json.load(open(p)); s["pw/password"] = "wrong"; json.dump(s, open(p, "w"))
PY
O=$(printf 'x' | "$BIN" send pw to@example.test 'S' 2>&1); r=$?
[ $r -ne 0 ] && has "send reports a refused sign-in with the server's words" "$O" "535 5.7.8 bad credentials" || no "send reports a refused sign-in with the server's words" "$O"

echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]

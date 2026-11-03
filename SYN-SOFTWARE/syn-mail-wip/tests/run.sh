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
# these read the text view: SYN_MAIL_CHROME=none is the window with no headless Chrome to draw HTML (the fallback)
O=$(SYN_MAIL_CHROME=none SYN_MAIL_DRAWLOG="$T/draw" WAYLAND_DISPLAY= "$BIN" window --shot "$T/shot.png" --size 1280x800 2>&1); r=$?
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
O=$(SYN_MAIL_CHROME=none SYN_MAIL_DRAWLOG="$T/draw2" WAYLAND_DISPLAY= "$BIN" window --shot "$T/shot2.png" --open 2 2>&1)
has "window --open 2 reads the second message" "$(cat "$T/draw2" 2>/dev/null)" "Your café order is on its way"
has "with no headless Chrome the HTML falls back to the text, and says so" "$D" "HTML not drawn: no headless Chrome here"
O=$(WAYLAND_DISPLAY=nowhere-$$ "$BIN" window 2>&1); r=$?
[ $r -ne 0 ] && has "window without a desktop says so" "$O" "no Wayland display" || no "window without a desktop says so" "$O"
hasnt "nothing was ever marked read" "$(cat "$T/log")" "marks read"
has "bodies were fetched with PEEK" "$(cat "$T/log")" "imap UID FETCH 103 PEEK"

# 6c. the HTML reader (G265): the HTML part over the plain one, cid: parts in place, remote images held back, the
# page private; with a headless Chrome on the machine, the drawing is checked for the inline image and the stand-in's
# web server for any request
read -r PX <"$T/pixel_port"
R="$T/run"; mkdir -m 700 "$R"
O=$(XDG_RUNTIME_DIR="$R" "$BIN" html pw Promo 701 2>&1); r=$?
PAGE=$(printf '%s\n' "$O" | sed -n 1p)
[ $r -eq 0 ] && [ -f "$PAGE" ] && ok "html writes the message's page" || no "html writes the message's page" "$O"
has "html counts the inline part and holds the remote ones back" "$O" "1 inline part, 2 remote references held back"
H=$(cat "$PAGE" 2>/dev/null)
has "html prefers the HTML part over the plain twin" "$H" "The November letter"
hasnt "html leaves the plain twin out" "$H" "PLAIN TWIN"
has "html points cid: at the inline image beside the page" "$H" 'src="cid-1.png"'
hasnt "html leaves no cid: reference behind" "$H" "cid:logo"
cmp -s "$(dirname "$PAGE")/cid-1.png" "$T/logo.png" && ok "the inline image is written byte for byte" || no "the inline image is written byte for byte"
has "html turns ISO-8859-1 into UTF-8" "$H" "Café news"
CSP=$(printf '%s' "$H" | grep -o 'Content-Security-Policy" content="[^"]*"')
has "the page's guard lets no script run" "$CSP" "default-src 'none'"
hasnt "the page's guard lets no remote image in" "$CSP" "http"
has "the page's guard keeps the inline image" "$CSP" "img-src data: file: blob:;"
has "html switches a meta refresh off: the sender's http-equiv is renamed" "$H" 'x-http-equiv-off="refresh"'
has "and so is the sender's charset meta, which the page's own utf-8 replaces" "$H" 'x-http-equiv-off="Content-Type"'
has "html switches DNS prefetch off" "$H" 'x-dns-prefetch-control" content="off"'
[ "$(stat -c %a "$(dirname "$PAGE")")" = 700 ] && [ "$(stat -c %a "$PAGE")" = 600 ] && [ "$(stat -c %a "$(dirname "$PAGE")/cid-1.png")" = 600 ] &&
	ok "the page's folder is 700, its files 600" || no "the page's folder is 700, its files 600" "$(stat -c '%a %n' "$(dirname "$PAGE")" "$PAGE")"
O=$(XDG_RUNTIME_DIR="$R" "$BIN" html pw Promo 701 --remote 2>&1)
has "html --remote lets the remote images in" "$(cat "$(printf '%s\n' "$O" | sed -n 1p)")" "img-src data: file: blob: http: https:"
has "html --remote says so" "$O" "2 remote references allowed"
O=$(XDG_RUNTIME_DIR="$R" "$BIN" html pw INBOX 102 2>&1); r=$?
[ $r -eq 0 ] && has "html takes the HTML of multipart/alternative inside mixed" "$(cat "$(printf '%s\n' "$O" | sed -n 1p)")" "HTML VERSION" ||
	no "html takes the HTML of multipart/alternative inside mixed" "$O"
has "html leaves the attachment out" "$O" "0 inline parts"
O=$(XDG_RUNTIME_DIR="$R" "$BIN" html pw Promo 702 2>&1); r=$?
[ $r -ne 0 ] && has "html of a text-only message says there is none" "$O" "no HTML part" || no "html of a text-only message says there is none" "$O"
# a meta refresh no word match catches (the exec's refusal of C251): every http-equiv the sender wrote is renamed,
# whatever its value or the space before its =; only the page's own two guard metas keep the name
O=$(XDG_RUNTIME_DIR="$R" "$BIN" html pw Promo 703 2>&1); r=$?
H3=$(cat "$(printf '%s\n' "$O" | sed -n 1p)" 2>/dev/null)
EQ=$(printf '%s' "$H3" | python3 -c 'import re,sys; print(len(re.findall(r"(?<!x-)http-equiv", sys.stdin.read(), re.I)))')
[ $r -eq 0 ] && [ "$EQ" = 2 ] && ok "no http-equiv of the sender's survives, only the page's two guards" ||
	no "no http-equiv of the sender's survives, only the page's two guards" "exit $r, $EQ left: $H3"
has "an entity-encoded refresh (&#114;efresh) is renamed" "$H3" 'x-http-equiv-off="&#114;efresh"'
has "a hex-encoded one in capitals (&#x72;efresh) is renamed" "$H3" 'x-http-equiv-off="&#x72;efresh"'
has "one with a tab before the = is renamed" "$H3" "$(printf 'x-http-equiv-off\t="refresh"')"
printf '%s' "$H3" | python3 -c 'import re,sys; sys.exit(0 if re.search(r"x-http-equiv-off\r?\n=refresh", sys.stdin.read()) else 1)' &&
	ok "one with a newline before the = is renamed" || no "one with a newline before the = is renamed" "$H3"
has "a link's href, a srcset's remote candidate and a video poster count as remote; a plain link does not" "$O" \
	"0 inline parts, 3 remote references held back"
# the sweep: a drawing's folder left past ten minutes goes, a fresh one and an open page under the hour stay,
# and nothing else in the folder is touched
mkdir -p "$R/syn-mail/render-stale" "$R/syn-mail/render-fresh" "$R/syn-mail/open-recent" "$R/syn-mail/keep-stale"
touch -d '20 minutes ago' "$R/syn-mail/render-stale" "$R/syn-mail/keep-stale"
touch -d '30 minutes ago' "$R/syn-mail/open-recent"
printf '#!/bin/sh\nprintf "%%s\\n" "$@" >>"%s/browser.log"\n' "$T" >"$T/fake-browser"; chmod 700 "$T/fake-browser"
O=$(XDG_RUNTIME_DIR="$R" SYN_MAIL_BROWSER="$T/fake-browser" "$BIN" html pw Promo 701 --open 2>&1); r=$?
i=0; while [ ! -s "$T/browser.log" ] && [ $i -lt 100 ]; do i=$((i + 1)); python3 -c 'import time; time.sleep(0.05)'; done
U=$(cat "$T/browser.log" 2>/dev/null)
[ $r -eq 0 ] && has "html --open hands Chrome the private page" "$U" "file://$R/syn-mail/open-" || no "html --open hands Chrome the private page" "$O $U"
F=$(printf '%s' "$U" | sed -n 's|^file://||p' | head -1)
[ -f "$F" ] && [ "$(stat -c %a "$F")" = 600 ] && ok "the page Chrome opens is mode 600" || no "the page Chrome opens is mode 600" "$F"
[ ! -e "$R/syn-mail/render-stale" ] && ok "the sweep clears a drawing's folder a crash left" || no "the sweep clears a drawing's folder a crash left" "$(ls "$R/syn-mail")"
[ -d "$R/syn-mail/render-fresh" ] && [ -d "$R/syn-mail/open-recent" ] && [ -d "$R/syn-mail/keep-stale" ] &&
	ok "the sweep leaves a fresh drawing, an open page under the hour and folders it did not make" ||
	no "the sweep leaves a fresh drawing, an open page under the hour and folders it did not make" "$(ls "$R/syn-mail")"
rmdir "$R/syn-mail/render-fresh" "$R/syn-mail/open-recent" "$R/syn-mail/keep-stale"
O=$(SYN_MAIL_CHROME=none SYN_MAIL_DRAWLOG="$T/draw3" WAYLAND_DISPLAY= XDG_RUNTIME_DIR="$R" "$BIN" window --shot "$T/shot3.png" --folder Promo --open 3 2>&1)
has "with no Chrome the window reads the HTML letter's plain twin" "$(cat "$T/draw3" 2>/dev/null)" "PLAIN TWIN of the letter."
if CH=$(command -v chromium || command -v google-chrome-stable || command -v google-chrome); then
	O=$(XDG_RUNTIME_DIR="$R" SYN_MAIL_CHROME="$CH" "$BIN" html pw Promo 701 --png "$T/blocked.png" --width 800 2>&1); r=$?
	[ $r -eq 0 ] && [ -s "$T/blocked.png" ] && ok "headless Chrome draws the page" || no "headless Chrome draws the page" "$O"
	G=$(python3 "$HERE/pngcount.py" "$T/blocked.png" 12ab34 2>&1)
	case "$G" in "800x"*" 7200") ok "the drawing shows the inline image, every pixel of it" ;;
	*) no "the drawing shows the inline image, every pixel of it" "$G (want 800 wide, 7200 green pixels)" ;; esac
	O=$(XDG_RUNTIME_DIR="$R" SYN_MAIL_CHROME="$CH" "$BIN" html pw Promo 701 --png "$T/narrow.png" --width 320 2>&1)
	G=$(python3 "$HERE/pngcount.py" "$T/narrow.png" 12ab34 2>&1)
	case "$G" in "320x"*" 0") no "a narrow drawing is the page scaled down, the image whole" "$G" ;;
	"320x"*) n=${G##* }; [ "$n" -ge 1500 ] && [ "$n" -le 2100 ] && ok "a narrow drawing is the page scaled down, the image whole" ||
		no "a narrow drawing is the page scaled down, the image whole" "$G (want about 1800 green pixels: 7200 at half scale)" ;;
	*) no "a narrow drawing is the page scaled down, the image whole" "$G" ;; esac
	G=$(python3 "$HERE/pngcount.py" "$T/blocked.png" ff0000 2>&1)
	has "no script ran in the drawing" "$G" " 0"
	[ ! -s "$T/pixel.log" ] && ok "the remote image block holds: the web server saw nothing" || no "the remote image block holds: the web server saw nothing" "$(cat "$T/pixel.log")"
	[ -z "$(ls "$R/syn-mail" | grep '^render-')" ] && ok "the drawing leaves no folder behind" || no "the drawing leaves no folder behind" "$(ls "$R/syn-mail")"
	O=$(SYN_MAIL_CHROME="$CH" SYN_MAIL_DRAWLOG="$T/draw4" WAYLAND_DISPLAY= XDG_RUNTIME_DIR="$R" "$BIN" window --shot "$T/shot4.png" --folder Promo --open 3 --size 1280x800 2>&1)
	D4=$(cat "$T/draw4" 2>/dev/null)
	has "the window shows the HTML as drawn" "$D4" "[html "
	has "the window says the remote images are held back and how to load them" "$D4" "2 remote images held back: i loads them"
	G=$(python3 "$HERE/pngcount.py" "$T/shot4.png" 12ab34 700 0 1280 800 2>&1)
	case "$G" in *" 0") no "the window's reader shows the inline image" "$G" ;; *) ok "the window's reader shows the inline image" ;; esac
	[ ! -s "$T/pixel.log" ] && ok "the window fetched no remote image" || no "the window fetched no remote image" "$(cat "$T/pixel.log")"
	[ -n "$SYN_MAIL_SHOT_OUT" ] && cp "$T/shot4.png" "$SYN_MAIL_SHOT_OUT"/ 2>/dev/null
	O=$(XDG_RUNTIME_DIR="$R" SYN_MAIL_CHROME="$CH" "$BIN" html pw Promo 701 --remote --png "$T/remote.png" --width 600 2>&1)
	has "with --remote the remote image is fetched (the test can see a fetch)" "$(cat "$T/pixel.log" 2>/dev/null)" "/pixel.gif?u=1"
	hasnt "a meta refresh moves nothing, even with remote images on" "$(cat "$T/pixel.log" 2>/dev/null)" "/refresh"
	O=$(XDG_RUNTIME_DIR="$R" SYN_MAIL_CHROME="$CH" "$BIN" html pw Promo 703 --remote --png "$T/tricks.png" --width 600 2>&1); r=$?
	[ $r -eq 0 ] && [ -s "$T/tricks.png" ] && ok "headless Chrome draws the refresh tricks" || no "headless Chrome draws the refresh tricks" "$O"
	has "with --remote the tricks letter's stylesheet is fetched (Chrome read the page)" "$(cat "$T/pixel.log" 2>/dev/null)" "/style.css"
	hasnt "no encoded or spaced meta refresh moved the page, remote images on" "$(cat "$T/pixel.log" 2>/dev/null)" "/refresh"
	: >"$T/pixel.log"
	O=$(SYN_MAIL_CHROME="$CH" SYN_MAIL_DRAWLOG="$T/draw5" WAYLAND_DISPLAY= XDG_RUNTIME_DIR="$R" "$BIN" window --shot "$T/shot5.png" --folder Promo --open 3 --images 2>&1)
	has "window --images loads the remote images, as i does" "$(cat "$T/draw5" 2>/dev/null)" "HTML, remote images loaded"
	has "and the window's drawing fetched them" "$(cat "$T/pixel.log" 2>/dev/null)" "/pixel.gif?u=1"
else
	echo "skip the drawing checks: no chromium or google-chrome on this machine"
fi

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
B=$(python3 -c 'import email,email.policy,sys; m=email.message_from_bytes(open(sys.argv[1],"rb").read(),policy=email.policy.default); print(m.get_body(("plain",)).get_content()); print(m["Subject"])' "$T/sent/1.eml" 2>&1)
has "sent: the body arrives intact" "$B" "Hello,
.leading dot line
Café."
has "sent: the subject decodes, newline flattened" "$B" "Résumé Bcc: evil@example.test"
printf 'Via OAuth\n' | "$BIN" send oa to@example.test 'Plain subject' >"$T/o" 2>&1; r=$?
[ $r -eq 0 ] && has "send with XOAUTH2" "$(cat "$T/log")" "smtp XOAUTH2 oauth@example.test" || no "send with XOAUTH2" "$(cat "$T/o")"
O=$(printf 'x' | "$BIN" send pw 'to@example.test>
RCPT TO:<x@y.z' 'S' 2>&1); r=$?
[ $r -ne 0 ] && [ ! -e "$T/sent/3.eml" ] && ok "send refuses an address that would inject a command" || no "send refuses an address that would inject a command" "$O"
# 8. attachments (G261): repeatable --attach, multipart/mixed, every byte back, names that cannot forge a header
python3 -c 'import sys; open(sys.argv[1],"wb").write(b"%PDF-1.4\n" + bytes(range(256)) * 300 + b"\n%%EOF\n")' "$T/report.pdf"
printf 'a,b\n1,2\n' >"$T/Relevé \"x\".csv"
NL="$T/$(printf 'n\nBcc: evil@example.test\nX: y').txt"; printf 'nl\n' >"$NL"
printf 'See attached.\n' | "$BIN" send pw to@example.test 'With files' --attach "$T/report.pdf" --attach "$T/Relevé \"x\".csv" --attach "$NL" >"$T/o" 2>&1; r=$?
[ $r -eq 0 ] && has "send with three attachments" "$(cat "$T/o")" "3 attachments" || no "send with three attachments" "$(cat "$T/o")"
A=$(python3 - "$T/sent/3.eml" "$T/report.pdf" "$T/Relevé \"x\".csv" "$NL" <<'PY'
import email, email.policy, sys
m = email.message_from_bytes(open(sys.argv[1], "rb").read(), policy=email.policy.default)
print("type", m.get_content_type())
parts = list(m.iter_parts())
print("parts", len(parts))
print("text", parts[0].get_content_type(), m.get_body(("plain",)).get_content().split("\n")[0])
print("alt", ",".join(p.get_content_type() for p in parts[0].iter_parts()))
for p, f in zip(parts[1:], sys.argv[2:]):
    same = p.get_payload(decode=True) == open(f, "rb").read()
    print("file", p.get_content_type(), p.get_content_disposition(), p.get_filename(), "same" if same else "DIFFERENT")
print("headers", ",".join(k for k in m.keys()), "|", ",".join(k for p in parts for k in p.keys()))
PY
)
has "attached: the message is multipart/mixed" "$A" "type multipart/mixed"
has "attached: text and three files" "$A" "parts 4"
has "attached: the letter comes first, its text intact" "$A" "text multipart/alternative See attached."
has "attached: the letter inside is the text twin then the HTML" "$A" "alt text/plain,text/html"
has "attached: the PDF arrives byte for byte, typed and named" "$A" "file application/pdf attachment report.pdf same"
has "attached: a non-ASCII name with a quote decodes, the bytes intact" "$A" "file text/csv attachment Relevé \"x\".csv same"
hasnt "attached: a newline in a file name adds no header" "$(printf '%s' "$A" | grep '^headers')" "Bcc"
has "attached: the name with newlines still round-trips" "$A" "attachment n
Bcc: evil@example.test
X: y.txt same"
O=$(printf 'x' | "$BIN" send pw to@example.test 'S' --attach "$T/no-such.pdf" 2>&1); r=$?
[ $r -ne 0 ] && [ ! -e "$T/sent/4.eml" ] && has "send refuses a missing attachment, sending nothing" "$O" "cannot open" || no "send refuses a missing attachment, sending nothing" "$O"
O=$(printf 'x' | "$BIN" send pw to@example.test 'S' --attach "$T" 2>&1); r=$?
[ $r -ne 0 ] && [ ! -e "$T/sent/4.eml" ] && has "send refuses a folder as an attachment" "$O" "not a plain file" || no "send refuses a folder as an attachment" "$O"
O=$(printf 'x' | "$BIN" send pw to@example.test 'S' --attach 2>&1); r=$?
[ $r -eq 2 ] && [ ! -e "$T/sent/4.eml" ] && ok "send --attach with no file is a usage error" || no "send --attach with no file is a usage error" "$O"
# 9. the company letter (G265): text twin and rendered HTML, signature, footer and notice, the pinned theme
printf 'Plain only.\n' | "$BIN" send pw to@example.test 'Plain' --plain >"$T/o" 2>&1; r=$?
E4=$(cat "$T/sent/4.eml" 2>/dev/null)
[ $r -eq 0 ] && hasnt "send --plain stays single-part text" "$E4" "multipart" || no "send --plain stays single-part text" "$(cat "$T/o")"
P4=$(python3 -c 'import email,sys; print(email.message_from_bytes(open(sys.argv[1],"rb").read()).get_payload(decode=True).decode())' "$T/sent/4.eml" 2>&1)
hasnt "send --plain carries no signature" "$P4" "Syntax Corporation"
O=$("$BIN" title pw 'Chief Engineer' 2>&1); r=$?
[ $r -eq 0 ] && has "title sets the signature's title" "$O" 'Pat "the" Tester, Chief Engineer' || no "title sets the signature's title" "$O"
has "the title is kept in the config" "$(cat "$SYN_MAIL_CONFIG")" '"title": "Chief Engineer"'
O=$("$BIN" title pw "$(printf 'a\nb')" 2>&1); r=$?
[ $r -eq 2 ] && ok "a title with a newline is refused" || no "a title with a newline is refused" "$O"
printf 'Dear <Captain> & crew,\n\nSecond "para".\nLine two </p><script>x()</script>\n' |
	"$BIN" send pw to@example.test 'A <b> test' >"$T/o" 2>&1; r=$?
[ $r -eq 0 ] && ok "send a company letter" || no "send a company letter" "$(cat "$T/o")"
python3 - "$T/sent/5.eml" "$T/plain5" "$T/html5" "$T/type5" <<'PY'
import email, email.policy, sys
m = email.message_from_bytes(open(sys.argv[1], "rb").read(), policy=email.policy.default)
open(sys.argv[4], "w").write(m.get_content_type() + " " + ",".join(p.get_content_type() for p in m.iter_parts()))
open(sys.argv[2], "w").write(m.get_body(("plain",)).get_content())
open(sys.argv[3], "w").write(m.get_body(("html",)).get_content())
PY
TY=$(cat "$T/type5" 2>/dev/null); PL=$(cat "$T/plain5" 2>/dev/null); HT=$(cat "$T/html5" 2>/dev/null)
has "letter: multipart/alternative, the text twin first, the HTML last" "$TY" "multipart/alternative text/plain,text/html"
has "letter: the text twin carries the body" "$PL" "Dear <Captain> & crew,"
has "letter: the text twin signs below the standard delimiter" "$PL" '-- 
Pat "the" Tester
Chief Engineer
Syntax Corporation
pw@example.test'
has "letter: the text twin ends with the notice" "$PL" "NOTICE OF GENERATED WORKS AND DISCLAIMER OF WARRANTY."
has "letter: the HTML escapes the body" "$HT" "Dear &lt;Captain&gt; &amp; crew,"
hasnt "letter: no tag in the body reaches the HTML" "$HT" "<script"
has "letter: a blank line starts a new paragraph, a newline is a break" "$HT" "Second &quot;para&quot;.<br>"
has "letter: the subject is escaped in the title" "$HT" "<title>A &lt;b&gt; test</title>"
has "letter: the signature names the sender" "$HT" "Pat &quot;the&quot; Tester</div>"
has "letter: the signature carries the title" "$HT" "Chief Engineer</div>"
has "letter: signed for Syntax Corporation" "$HT" "Syntax Corporation</div>"
has "letter: the footer carries the notice" "$HT" "NOTICE OF GENERATED WORKS AND DISCLAIMER OF WARRANTY."
has "letter: the pinned SYN-OS-M141 accent" "$HT" "#e00000"
hasnt "letter: no SYN-OS-BLUE left" "$HT" "#0986d3"
has "letter: the pinned SYN-OS-M141 panel" "$HT" "background-color:#1a0000"
has "letter: laid out in tables" "$HT" '<table role="presentation"'
hasnt "letter: no style sheet (inline styles only)" "$HT" "<style"
hasnt "letter: no images" "$HT" "<img"
hasnt "letter: nothing fetched from outside" "$HT" "src="
hasnt "letter: no links" "$HT" "href="
SYN_THEME_NAME=SYN-OS-HALO-CRIMSON printf 'x\n' | SYN_THEME_NAME=SYN-OS-HALO-CRIMSON "$BIN" send pw to@example.test 'T' >/dev/null 2>&1
H6=$(python3 -c 'import email,email.policy,sys; m=email.message_from_bytes(open(sys.argv[1],"rb").read(),policy=email.policy.default); print(m.get_body(("html",)).get_content())' "$T/sent/6.eml" 2>&1)
has "letter: the theme stays pinned whatever the desktop wears" "$H6" "#e00000"
O=$("$BIN" title pw 2>&1); r=$?
[ $r -eq 0 ] && has "title with none clears it" "$O" "signs with no title" || no "title with none clears it" "$O"
hasnt "a cleared title leaves the config" "$(cat "$SYN_MAIL_CONFIG")" '"title"'

python3 - "$SYN_MAIL_SECRETS_FILE" <<'PY'
import json, sys; p = sys.argv[1]; s = json.load(open(p)); s["pw/password"] = "wrong"; json.dump(s, open(p, "w"))
PY
O=$(printf 'x' | "$BIN" send pw to@example.test 'S' 2>&1); r=$?
[ $r -ne 0 ] && has "send reports a refused sign-in with the server's words" "$O" "535 5.7.8 bad credentials" || no "send reports a refused sign-in with the server's words" "$O"

echo "$PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]

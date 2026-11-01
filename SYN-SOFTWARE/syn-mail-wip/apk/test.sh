#!/bin/sh
# ------------------------------------------------------------------------
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-MAIL-APK
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
# The phone app's mail core (apk/java/.../core, no Android code in it) on the desktop JVM, against tests/standin.py.
# Usage: apk/test.sh [EXPORT_DIR]   EXPORT_DIR: the files `syn-mail export` wrote in tests/run.sh (make test gives it)
HERE=$(dirname "$(realpath "$0")")
EXP=${1:+$(realpath "$1")}
command -v javac >/dev/null || { echo "skip the APK core tests: no javac"; exit 0; }
T=$(mktemp -d "${TMPDIR:-/tmp}/syn-mail-apk-test.XXXXXX")
cleanup() { [ -n "$SP" ] && kill "$SP" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT INT TERM
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=localhost -addext subjectAltName=DNS:localhost \
	-keyout "$T/key.pem" -out "$T/cert.pem" >/dev/null 2>&1 || { echo "FAIL openssl could not make the test certificate"; exit 1; }
python3 "$HERE/../tests/standin.py" "$T" >"$T/standin.out" 2>&1 &
SP=$!
i=0
while [ ! -s "$T/ports" ] && [ $i -lt 100 ]; do i=$((i + 1)); python3 -c 'import time; time.sleep(0.05)'; done
[ -s "$T/ports" ] || { echo "FAIL the stand-in did not start"; cat "$T/standin.out"; exit 1; }
mkdir -p "$T/classes"
javac --release 17 -Xlint:all -Werror -d "$T/classes" "$HERE"/java/com/syntax990/synmail/core/*.java \
	"$HERE"/test/com/syntax990/synmail/CoreTest.java || { echo "FAIL the core does not compile"; exit 1; }
java -cp "$T/classes" com.syntax990.synmail.CoreTest "$T" $EXP

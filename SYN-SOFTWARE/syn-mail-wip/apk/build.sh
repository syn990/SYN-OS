#!/usr/bin/env bash
# ------------------------------------------------------------------------
#   Builds SYN-MAIL-APK with the SDK's own tools, no Gradle:
#   aapt2 -> javac -> d8 -> zipalign -> apksigner.
#
#     SYN-MAIL-APK-<ver>.apk  no secret inside: an account comes from the
#                             PC (`syn-mail export NAME FILE`, then
#                             `syn-link send FILE`) or is typed on the
#                             phone with an app password, and is sealed
#                             there under an Android Keystore key
#
#     --standin DIR           the test build, for screenshots: its own
#                             package (com.syntax990.synmail.standin), the
#                             running stand-in's certificate (DIR/cert.pem)
#                             and ports (DIR/ports) and its test account in
#                             assets, and it trusts nothing else. Reached
#                             through `adb reverse`. Kept in apk/build only,
#                             never copied to ~/Apps.
#
#   Signed with ~/.config/syn-mail/apk-signing.jks (made on the first run),
#   so a new build installs over an old one. A copy lands in ~/Apps/SYN-Mail/.
#
#   usage: apk/build.sh [--standin DIR]
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-MAIL-APK
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
SDK=${ANDROID_HOME:-$HOME/Android/Sdk}
BT=$SDK/build-tools/36.1.0
JAR=$SDK/platforms/android-36/android.jar
OUT=$HERE/build
DEST=$HOME/Apps/SYN-Mail
VERSION=0.2.0
CODE=2
THEMES=$REPO/SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/DotfileOverlay/etc/skel/.config/syn-os/themes
KS=$HOME/.config/syn-mail/apk-signing.jks
KSPASS=$HOME/.config/syn-mail/apk-signing.pass
STANDIN=
if [ "${1:-}" = --standin ]; then
  STANDIN=$(cd "${2:?usage: build.sh --standin DIR (a running tests/standin.py folder)}" && pwd)
  [ -s "$STANDIN/ports" ] && [ -s "$STANDIN/cert.pem" ] || { echo "no stand-in running in $STANDIN"; exit 1; }
fi

say() { printf '\033[1;31m[SYN-MAIL-APK]\033[0m %s\n' "$*"; }
for t in "$BT/aapt2" "$BT/d8" "$BT/zipalign" "$BT/apksigner" "$JAR"; do
  [ -e "$t" ] || { say "missing $t (sdkmanager \"platforms;android-36\" \"build-tools;36.1.0\")"; exit 1; }
done

# ---- assets: the SYN-OS themes, the live one as default; the stand-in's test pieces in its build only ----
rm -rf "$OUT"
mkdir -p "$OUT/assets/themes" "$OUT/gen" "$OUT/classes" "$OUT/dex"
cp "$THEMES"/*.theme "$OUT/assets/themes/"
cat "$HOME/.config/syn-os/current-theme" 2>/dev/null > "$OUT/assets/default-theme" || true
[ -s "$OUT/assets/default-theme" ] || echo SYN-OS-M141 > "$OUT/assets/default-theme"
RENAME=()
NAME=SYN-MAIL-APK-$VERSION
if [ -n "$STANDIN" ]; then
  read -r PI PS PH < "$STANDIN/ports"
  cp "$STANDIN/cert.pem" "$OUT/assets/standin-ca.pem"
  echo "https://localhost:$PH/token" > "$OUT/assets/standin-token-url"
  # the stand-in's own test account (tests/standin.py USERS): test data, not a secret
  python3 -c 'import json,sys; print(json.dumps({"syn_mail_account": 1, "name": "standin", "address": "pw@example.test",
    "display_name": "Pat Tester", "imap_host": "localhost", "imap_port": int(sys.argv[1]), "smtp_host": "localhost",
    "smtp_port": int(sys.argv[2]), "auth": "password", "secrets": {"password": "s3cret pass\"word"}}))' "$PI" "$PS" \
    > "$OUT/assets/standin-account.json"
  RENAME=(--rename-manifest-package com.syntax990.synmail.standin)
  NAME=SYN-MAIL-APK-$VERSION-standin
fi

# ---- resources, Java, dex ----
say "aapt2, javac, d8${STANDIN:+ (stand-in build)}"
"$BT/aapt2" compile --dir "$HERE/res" -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$JAR" --manifest "$HERE/AndroidManifest.xml" --min-sdk-version 26 \
  --target-sdk-version 34 --version-code $CODE --version-name $VERSION -A "$OUT/assets" "$OUT/res.zip" \
  --java "$OUT/gen" "${RENAME[@]}"
javac --release 17 -Xlint:all,-options,-deprecation -Werror -cp "$JAR" -d "$OUT/classes" \
  $(find "$HERE/java" "$OUT/gen" -name '*.java')
"$BT/d8" --release --min-api 26 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')

# ---- signing key, made once and kept ----
if [ ! -f "$KS" ]; then
  say "making the signing key $KS"
  (umask 077; mkdir -p "$(dirname "$KS")"; head -c 24 /dev/urandom | base64 > "$KSPASS")
  keytool -genkeypair -keystore "$KS" -storepass:file "$KSPASS" -keypass:file "$KSPASS" -alias syn-mail \
    -keyalg RSA -keysize 3072 -validity 10000 -dname "CN=SYN-MAIL, O=SYN-OS" >/dev/null 2>&1
  chmod 600 "$KS"
fi

u=$OUT/unsigned.apk a=$OUT/aligned.apk apk=$OUT/$NAME.apk
cp "$OUT/base.apk" "$u"
(cd "$OUT/dex" && zip -q -X "$u" classes.dex)
"$BT/zipalign" -f -P 16 4 "$u" "$a"
# (JDK 27 warns about apksigner's native crypto; nothing to act on)
"$BT/apksigner" sign --ks "$KS" --ks-pass "file:$KSPASS" --ks-key-alias syn-mail --out "$apk" "$a" 2>&1 | grep -v '^WARNING\|^$' || true
"$BT/apksigner" verify "$apk" >/dev/null 2>&1 || { say "signature check failed for $apk"; exit 1; }
rm -f "$u" "$a" "$apk.idsig"
if [ -z "$STANDIN" ]; then
  mkdir -p "$DEST"
  cp "$apk" "$DEST/"
  say "$DEST/$NAME.apk"
else
  say "$apk (stand-in: adb reverse tcp:$PI tcp:$PI; adb reverse tcp:$PS tcp:$PS; adb reverse tcp:$PH tcp:$PH)"
fi

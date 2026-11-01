#!/usr/bin/env bash
# ------------------------------------------------------------------------
#   Builds SYN-CAST-APK with the SDK's own tools, no Gradle:
#   libopus + JNI glue (NDK, per ABI) -> aapt2 -> javac -> d8 -> zipalign
#   -> apksigner.
#
#     SYN-CAST-APK-<ver>-personal.apk  the one to install: carries the
#                                      developer issuer and key from
#                                      ~/.config/syn-caster/crew (revoke the key at
#                                      developers.zello.com if it ever leaks)
#     SYN-CAST-APK-<ver>.apk           with --shareable too: no secrets, sign
#                                      in with a pasted or loaded key
#
#   Both are signed with ~/.config/syn-cast/apk-signing.jks (made on the
#   first run), so a new build installs over an old one. Copies land in
#   ~/Apps/SYN-CAST/.
#
#   usage: apk/build.sh [--shareable]
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-CAST-APK
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
SDK=${ANDROID_HOME:-$HOME/Android/Sdk}
BT=$SDK/build-tools/36.1.0
JAR=$SDK/platforms/android-36/android.jar
NDK=$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/syn-cast-apk
OUT=$HERE/out
DEST=$HOME/Apps/SYN-CAST
VERSION=0.1.0
CODE=1
ABIS="arm64-v8a armeabi-v7a x86_64"
OPUS_VER=1.6.1
OPUS_SHA=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
THEMES=$REPO/SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/DotfileOverlay/etc/skel/.config/syn-os/themes
ZCFG=$HOME/.config/syn-caster/crew/config.json
ZKEY=$HOME/.config/syn-caster/crew/zello-key.pem
KS=$HOME/.config/syn-cast/apk-signing.jks
KSPASS=$HOME/.config/syn-cast/apk-signing.pass

say() { printf '\033[1;31m[SYN-CAST-APK]\033[0m %s\n' "$*"; }
for t in "$BT/aapt2" "$BT/d8" "$BT/zipalign" "$BT/apksigner" "$JAR" "$NDK/build/cmake/android.toolchain.cmake"; do
  [ -e "$t" ] || { say "missing $t (sdkmanager \"platforms;android-36\" \"build-tools;36.1.0\" \"ndk;29.0.14206865\")"; exit 1; }
done

# ---- libopus + JNI, one per ABI ----
mkdir -p "$CACHE"
if [ ! -d "$CACHE/opus-$OPUS_VER" ]; then
  say "fetching libopus $OPUS_VER from xiph"
  curl -sSfL -o "$CACHE/opus-$OPUS_VER.tar.gz" "https://downloads.xiph.org/releases/opus/opus-$OPUS_VER.tar.gz"
  echo "$OPUS_SHA  $CACHE/opus-$OPUS_VER.tar.gz" | sha256sum -c --quiet -
  tar xzf "$CACHE/opus-$OPUS_VER.tar.gz" -C "$CACHE"
fi
rm -rf "$OUT/stage"
for abi in $ABIS; do
  b=$CACHE/native/$abi
  if [ ! -f "$b/libsyncast.so" ] || [ "$HERE/jni/syncast_opus.c" -nt "$b/libsyncast.so" ] || [ "$HERE/jni/CMakeLists.txt" -nt "$b/libsyncast.so" ]; then
    say "native $abi"
    cmake -G Ninja -S "$HERE/jni" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release -DOPUS_SRC="$CACHE/opus-$OPUS_VER" >/dev/null
    ninja -C "$b" -j2 syncast >/dev/null
  fi
  mkdir -p "$OUT/stage/lib/$abi"
  "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded -o "$OUT/stage/lib/$abi/libsyncast.so" "$b/libsyncast.so"
done

# ---- assets: the SYN-OS themes, the live one as default, names the app must refuse ----
rm -rf "$OUT/assets" "$OUT/assets-personal"
mkdir -p "$OUT/assets/themes"
cp "$THEMES"/*.theme "$OUT/assets/themes/"
cat "$HOME/.config/syn-os/current-theme" 2>/dev/null > "$OUT/assets/default-theme" || true
[ -s "$OUT/assets/default-theme" ] || echo SYN-OS-M141 > "$OUT/assets/default-theme"
# SYN-CASTER's account: one session per Zello account, so the phone must not take it
python3 -I - "$ZCFG" > "$OUT/assets/reserved.json" <<'EOF'
import json, sys
try:
    u = json.load(open(sys.argv[1])).get("username")
except Exception:
    u = None
print(json.dumps([u] if u else []))
EOF

# ---- resources, Java, dex ----
say "aapt2, javac, d8"
rm -rf "$OUT/gen" "$OUT/classes" "$OUT/dex" "$OUT/res.zip"
mkdir -p "$OUT/gen" "$OUT/classes" "$OUT/dex"
"$BT/aapt2" compile --dir "$HERE/res" -o "$OUT/res.zip"
link() { # assets dir, output
  "$BT/aapt2" link -o "$2" -I "$JAR" --manifest "$HERE/AndroidManifest.xml" --min-sdk-version 26 \
    --target-sdk-version 34 --version-code $CODE --version-name $VERSION -A "$1" "$OUT/res.zip" ${3:+--java "$3"}
}
link "$OUT/assets" "$OUT/base.apk" "$OUT/gen"
javac --release 17 -nowarn -Xlint:-options -cp "$JAR" -d "$OUT/classes" \
  $(find "$HERE/java" "$OUT/gen" -name '*.java') 2>&1 | grep -v '^Note:' || true
[ -f "$OUT/classes/com/syntax990/syncast/MainActivity.class" ] || { say "javac failed"; exit 1; }
"$BT/d8" --release --min-api 26 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')

# ---- signing key, made once and kept ----
if [ ! -f "$KS" ]; then
  say "making the signing key $KS"
  (umask 077; mkdir -p "$(dirname "$KS")"; head -c 24 /dev/urandom | base64 > "$KSPASS")
  keytool -genkeypair -keystore "$KS" -storepass:file "$KSPASS" -keypass:file "$KSPASS" -alias syn-cast \
    -keyalg RSA -keysize 3072 -validity 10000 -dname "CN=SYN-CAST, O=SYN-OS" >/dev/null 2>&1
  chmod 600 "$KS"
fi

assemble() { # base apk, output
  local u=$OUT/unsigned.apk a=$OUT/aligned.apk
  cp "$1" "$u"
  (cd "$OUT/dex" && zip -q -X "$u" classes.dex)
  (cd "$OUT/stage" && zip -q -X -r "$u" lib)
  "$BT/zipalign" -f -P 16 4 "$u" "$a"
  # (JDK 27 warns about apksigner's native crypto; nothing to act on)
  "$BT/apksigner" sign --ks "$KS" --ks-pass "file:$KSPASS" --ks-key-alias syn-cast --out "$2" "$a" 2>&1 | grep -v '^WARNING\|^$' || true
  "$BT/apksigner" verify "$2" 2>&1 | grep -v '^WARNING\|^$' || true
  "$BT/apksigner" verify "$2" >/dev/null 2>&1 || { say "signature check failed for $2"; exit 1; }
  rm -f "$u" "$a" "$2.idsig"
}

mkdir -p "$DEST"
if [ "${1:-}" = "--shareable" ] || [ ! -f "$ZCFG" ] || [ ! -f "$ZKEY" ]; then
  assemble "$OUT/base.apk" "$OUT/SYN-CAST-APK-$VERSION.apk"
  cp "$OUT/SYN-CAST-APK-$VERSION.apk" "$DEST/"
  say "shareable (no key inside): $DEST/SYN-CAST-APK-$VERSION.apk"
fi

if [ -f "$ZCFG" ] && [ -f "$ZKEY" ]; then
  (umask 077; mkdir -p "$OUT/assets-personal"; cp -r "$OUT/assets/." "$OUT/assets-personal/")
  python3 -I - "$ZCFG" "$ZKEY" "$OUT/assets-personal/devkey.json" <<'EOF'
import json, os, sys
issuer = json.load(open(sys.argv[1])).get("issuer", "")
fd = os.open(sys.argv[3], os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
os.write(fd, json.dumps({"issuer": issuer, "key_pem": open(sys.argv[2]).read()}).encode())
os.close(fd)
EOF
  link "$OUT/assets-personal" "$OUT/base-personal.apk"
  (umask 077; assemble "$OUT/base-personal.apk" "$OUT/SYN-CAST-APK-$VERSION-personal.apk")
  install -m 600 "$OUT/SYN-CAST-APK-$VERSION-personal.apk" "$DEST/"
  rm -rf "$OUT/assets-personal" "$OUT/base-personal.apk"
  say "$DEST/SYN-CAST-APK-$VERSION-personal.apk"
fi

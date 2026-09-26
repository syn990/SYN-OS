#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#   syn-lfs-base.zsh ROOT PKG...
#
#   Void's binary packages on top of the LFS base. Writes syn-lfs-base, an
#   empty xbps package that tells xbps what the base already provides, into
#   ROOT/var/lib/syn-os/xbps-local, and xbps's repository config into
#   ROOT/etc/xbps.d. Then `xbps-install -r ROOT PKG...` adds only what the
#   base doesn't have.
#
#   For every package in the PKGs' Void dependency chains: one with at least
#   half its shared libraries in ROOT (or with none, and most of its files
#   there) is claimed by syn-lfs-base, at Void's own version so exact pins
#   are met. Every shared library in ROOT is declared too.
#
#   Runs on the build host, as root, with the image mounted at ROOT. Needs
#   Void's static xbps in $XBPS (default ~/.cache/xbps-probe/usr/bin).
# ------------------------------------------------------------------------------
setopt err_exit pipe_fail
ROOT=${1:?usage: syn-lfs-base.zsh ROOT PKG...}; shift
(( $# )) || { print "usage: syn-lfs-base.zsh ROOT PKG..."; exit 1 }
XBPS=${XBPS:-/home/syntax990/.cache/xbps-probe/usr/bin}
REPO=https://repo-default.voidlinux.org/current
LOCAL=$ROOT/var/lib/syn-os/xbps-local
W=$(mktemp -d); trap 'rm -rf -- "$W"' EXIT
Q=($XBPS/xbps-query.static -R --repository=$REPO -r $ROOT)

# Every shared library the base has, by file name (what xbps matches on)
find $ROOT/usr/lib $ROOT/usr/lib/qt6 -maxdepth 1 \( -name '*.so.*' -o -name '*.so' \) |
	sed 's|.*/||' | sort -u > $W/sonames
# Qt's private-API tag, which Void's Qt modules require by exact version
qtv=$(ls $ROOT/usr/lib/libQt6Core.so.6.* 2>/dev/null | sed -n 's/.*libQt6Core\.so\.//p' | head -1)
[[ -n $qtv ]] && print "libQt_6_PRIVATE_API.$qtv" >> $W/sonames

# The PKGs' dependency chains, as names
for p in "$@"; do $Q -x $p --fulldeptree; done | sed -E 's/-[^-]+_[0-9]+$//' | sort -u > $W/deps

: > $W/provides
while read d; do
	have=0
	shl=(${(f)"$($Q -p shlib-provides $d 2>/dev/null)"})
	if (( ${#shl} )); then
		# Half its libraries or more: the rest are optional add-ons the LFS
		# build left out (harfbuzz-cairo, gnutls-dane). libjpeg-turbo is
		# Void's: its libjpeg.so.8 goes in next to the base's libjpeg.so.62
		n=0; for s in $shl; do grep -qxF $s $W/sonames && n=$((n + 1)); done
		(( n * 2 >= ${#shl} )) && [[ $d != libjpeg-turbo ]] && have=1
	else
		files=(${(f)"$($Q -f $d 2>/dev/null | sed 's/ -> .*//' | grep -E '^/usr/(bin|lib|share|libexec)/')"})
		n=0; for f in $files; do [[ -e $ROOT$f ]] && n=$((n + 1)); done
		(( ${#files} == 0 || n * 2 >= ${#files} )) && have=1
	fi
	# with the virtual names it provides (libglvnd is libEGL, libGL...)
	(( have )) && { $Q -p pkgver $d; $Q -p provides $d 2>/dev/null | grep -v '^cmd:\|^pc:' || true } >> $W/provides
done < $W/deps
# glibc and the base files are always the base's
for b in glibc base-files; do $Q -p pkgver $b >> $W/provides; done
sort -u -o $W/provides $W/provides
print "$(wc -l < $W/deps) packages in the chains; the base provides $(wc -l < $W/provides)"

# The package, versioned by date so each run is an update
v=$(date +%Y%m%d.%H%M)_1
mkdir -p $W/empty $W/repo
(cd $W/repo && $XBPS/xbps-create.static -A x86_64 -n syn-lfs-base-$v \
	-s "SYN-OS LFS base: what the system already provides" \
	--provides "$(tr '\n' ' ' < $W/provides)" \
	--shlib-provides "$(tr '\n' ' ' < $W/sonames)" $W/empty >/dev/null)
install -d $LOCAL
find $LOCAL -maxdepth 1 -type f -name 'syn-lfs-base-*.xbps' -delete
rm -f -- $LOCAL/x86_64-repodata
cp $W/repo/syn-lfs-base-$v.x86_64.xbps $LOCAL/
$XBPS/xbps-rindex.static -a $LOCAL/syn-lfs-base-$v.x86_64.xbps >/dev/null

# Void and Arch link /usr/lib64 to lib, and their binaries' rpaths use it
# (Audacity looks for its own libraries in $ORIGIN/../lib64/audacity)
[[ -e $ROOT/usr/lib64 ]] || ln -s lib $ROOT/usr/lib64
# Void's QtWebEngine puts its helper in qt6/libexec; the base's Qt (like
# Arch's) looks in qt6/ itself
[[ -e $ROOT/usr/lib/qt6/QtWebEngineProcess ]] ||
	ln -sf libexec/QtWebEngineProcess $ROOT/usr/lib/qt6/QtWebEngineProcess

install -d $ROOT/etc/xbps.d $ROOT/var/db/xbps/keys
cat > $ROOT/etc/xbps.d/10-syn-os.conf << EOF2
# SYN-OS: Void's binary packages on top of the LFS base. syn-lfs-base, in
# the local repository, tells xbps what the base already provides, so it
# never installs Void's copies of those (syn-lfs-base.zsh writes it)
repository=/var/lib/syn-os/xbps-local
repository=$REPO
EOF2
print "syn-lfs-base-$v written to $LOCAL"

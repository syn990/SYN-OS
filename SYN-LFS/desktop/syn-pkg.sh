#!/bin/bash
# ------------------------------------------------------------------------------
#                        S Y N - L F S   D E S K T O P
#
#   Builds the SYN desktop on top of the LFS base, one recipe per package
#   in pkgs/, in the order listed in `order`. build.zsh calls both halves:
#
#     syn-pkg.sh fetch DIR           on the build host: download every
#                                    source into DIR, check its checksum
#     syn-pkg.sh build DIR [PKG...]  inside the LFS chroot, DIR holding the
#                                    fetched sources: build every package
#                                    not built yet, or only PKG... (always)
#
#   A recipe sets v (version), src (one "url checksum [filename]" string
#   per file, the first one gets unpacked, the rest sit next to it for
#   ../name references like the BLFS book uses) and build(), which runs
#   as root inside the unpacked source with lib.sh loaded and errexit on.
#   Checksums are md5:... (straight from the BLFS book), sha256:... or -
#   for the few files with no fixed release (Chrome, the Mozilla CA list).
#   Built versions go to /var/lib/syn-lfs/pkgs, logs to /var/log/syn-lfs.
# ------------------------------------------------------------------------------
set -o pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
STATE=/var/lib/syn-lfs/pkgs
LOGS=/var/log/syn-lfs

recipes() { grep -v -e '^#' -e '^[[:space:]]*$' "$HERE/order"; }

load() {
	unset v src noextract always
	unset -f build
	. "$HERE/pkgs/$1"
}

# Split one src entry into url, sum, name
fields() {
	read -r url sum name <<< "$1"
	name=${name:-${url##*/}}
}

check() {
	case $2 in
		-) [ -s "$1" ] ;;
		md5:*) echo "${2#md5:}  $1" | md5sum -c --quiet - >/dev/null 2>&1 ;;
		sha256:*) echo "${2#sha256:}  $1" | sha256sum -c --quiet - >/dev/null 2>&1 ;;
		*) echo "unknown checksum type: $2" >&2; return 1 ;;
	esac
}

do_fetch() {
	local dir=$1 p e bad=0
	[ -n "$dir" ] || { echo "usage: syn-pkg.sh fetch DIR"; return 1; }
	mkdir -p "$dir"
	for p in $(recipes); do
		load "$p"
		for e in "${src[@]}"; do
			fields "$e"
			[ -f "$dir/$name" ] && check "$dir/$name" "$sum" && continue
			echo "fetch  $p: $name"
			if ! curl -fL -# --retry 3 -o "$dir/$name.part" "$url"; then
				echo "FAILED $url"; bad=1; continue
			fi
			mv "$dir/$name.part" "$dir/$name"
			check "$dir/$name" "$sum" || { echo "BAD CHECKSUM $name (wanted $sum)"; bad=1; }
		done
	done
	[ $bad = 0 ] && echo "all sources present in $dir"
	return $bad
}

build_one() {
	local p=$1 work=/tmp/syn-build/$1 log=$LOGS/$1.log start=$SECONDS e dirs
	load "$p"
	printf '%-26s %-16s ' "$p" "$v"
	rm -rf "$work"
	mkdir -p "$work"
	cd "$work" || return 1
	for e in "${src[@]}"; do
		fields "$e"
		ln -s "$SYN_SRC/$name" "$name"
	done
	local dir=$work
	if [ ${#src[@]} -gt 0 ] && [ -z "$noextract" ]; then
		fields "${src[0]}"
		case $name in
			*.zip) python3 -m zipfile -e "$name" . ;;
			*) tar -xf "$name" ;;
		esac || { echo "FAILED to unpack $name"; return 1; }
		# Into the tarball's top directory if it has exactly one
		dirs=(*/)
		[ ${#dirs[@]} = 1 ] && [ -d "${dirs[0]}" ] && dir=$work/${dirs[0]%/}
	fi
	if bash -e -o pipefail -c '. "$1"; . "$2"; cd "$3"; build' syn \
		"$HERE/lib.sh" "$HERE/pkgs/$p" "$dir" >"$log" 2>&1; then
		ldconfig
		echo "$v" > "$STATE/$p"
		cd / && rm -rf "$work"
		printf 'ok   %dm%02ds\n' $(((SECONDS - start) / 60)) $(((SECONDS - start) % 60))
	else
		echo "FAILED"
		tail -n 40 "$log"
		echo
		echo "full log: $log   build tree kept in $work"
		return 1
	fi
}

do_build() {
	local p
	export SYN_SRC=$1
	shift
	[ -d "$SYN_SRC" ] || { echo "no sources at $SYN_SRC (run: build.zsh fetch)"; return 1; }
	export SYN_FILES=$HERE/files SYN_REPO=$(cd "$HERE/../.." && pwd)
	export MAKEFLAGS=-j$(nproc)
	export XORG_PREFIX=/usr
	export XORG_CONFIG="--prefix=/usr --sysconfdir=/etc --localstatedir=/var --disable-static"
	mkdir -p "$STATE" "$LOGS"
	if [ $# -gt 0 ]; then
		for p in "$@"; do
			[ -f "$HERE/pkgs/$p" ] || { echo "no recipe: $p"; return 1; }
			build_one "$p" || return 1
		done
		return 0
	fi
	for p in $(recipes); do
		load "$p"
		[ -z "$always" ] && [ "$(cat "$STATE/$p" 2>/dev/null)" = "$v" ] && continue
		build_one "$p" || return 1
	done
	echo "desktop build complete"
}

case $1 in
	fetch) shift; do_fetch "$@" ;;
	build) shift; do_build "$@" ;;
	*) sed -n '/^#   Builds/,/^# ---/p' "$0"; exit 1 ;;
esac

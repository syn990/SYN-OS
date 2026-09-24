#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                        S Y N - O S   D E S K T O P
#
#   Builds the SYN desktop on top of the base, one recipe per package
#   in pkgs/, in the order listed in `order`. build.zsh calls both halves:
#
#     syn-pkg.zsh fetch DIR           on the build host: download every
#                                     source into DIR, check its checksum
#     syn-pkg.zsh build DIR [PKG...]  inside the image's chroot, DIR holding the
#                                     fetched sources: build every package
#                                     not built yet, or only PKG... (always)
#     syn-pkg.zsh list                the recipes SYN_PROFILE builds, in order
#
#   SYN_PROFILE (full or minimal, default full) is a profile from `order`:
#   the categories it builds, with `# [category]` headers there putting
#   the recipes under them in a category.
#
#   A recipe sets v (version), src (one "url checksum [filename]" string
#   per file, the first one gets unpacked, the rest sit next to it for
#   ../name references like the BLFS book uses) and build(), which runs
#   as root inside the unpacked source with lib.zsh loaded, errexit on and
#   unquoted variables word-split, as the book's commands expect.
#   Checksums are md5:... (straight from the BLFS book), sha256:... or -
#   for the few files with no fixed release (the Mozilla CA list).
#   Built versions go to /var/lib/syn-os/pkgs, logs to /var/log/syn-os.
# ------------------------------------------------------------------------------
setopt pipe_fail

HERE=${0:A:h}
STATE=/var/lib/syn-os/pkgs
LOGS=/var/log/syn-os

# Recipe names in build order, filtered to the categories of $SYN_PROFILE
# (default full). `order` declares each profile as `profile NAME cat...`
# and puts a `# [cat]` header over each run of recipes.
recipes() {
	awk -v prof="${SYN_PROFILE:-full}" '
		/^profile / { if ($2 == prof) { for (i = 3; i <= NF; i++) want[$i] = 1; found = 1 }; next }
		/^# \[/ { cat = substr($2, 2, length($2) - 2); next }
		/^#/ || /^[[:space:]]*$/ { next }
		{ if (cat in want) print }
		END { if (!found) { print "no profile called " prof " in order" > "/dev/stderr"; exit 1 } }
	' "$HERE/order"
}

load() {
	unset v src noextract always keep
	unfunction build 2>/dev/null
	src=()
	. "$HERE/pkgs/$1"
}

# Split one src entry into url, sum, name
fields() {
	read -r url sum name <<< "$1"
	name=${name:-${url:t}}
}

check() {
	case $2 in
		-) [ -s "$1" ] ;;
		md5:*) print -r -- "${2#md5:}  $1" | md5sum -c --quiet - >/dev/null 2>&1 ;;
		sha256:*) print -r -- "${2#sha256:}  $1" | sha256sum -c --quiet - >/dev/null 2>&1 ;;
		*) print -u2 "unknown checksum type: $2"; return 1 ;;
	esac
}

do_fetch() {
	local dir=$1 p e bad=0
	[ -n "$dir" ] || { print "usage: syn-pkg.zsh fetch DIR"; return 1 }
	mkdir -p "$dir"
	for p in $(recipes); do
		load "$p"
		for e in "${src[@]}"; do
			fields "$e"
			[ -f "$dir/$name" ] && check "$dir/$name" "$sum" && continue
			print "fetch  $p: $name"
			if ! curl -fL -# --retry 3 -o "$dir/$name.part" "$url"; then
				print "FAILED $url"; bad=1; continue
			fi
			mv "$dir/$name.part" "$dir/$name"
			check "$dir/$name" "$sum" || { print "BAD CHECKSUM $name (wanted $sum)"; bad=1 }
		done
	done
	(( bad == 0 )) && print "all sources present in $dir"
	return $bad
}

build_one() {
	local p=$1 work=/tmp/syn-build/$1 log=$LOGS/$1.log start=$SECONDS e dir dirs
	load "$p"
	printf '%-26s %-16s ' "$p" "$v"
	[ -z "$keep" ] && rm -rf "$work"
	mkdir -p "$work"
	cd "$work" || return 1
	for e in "${src[@]}"; do
		fields "$e"
		ln -s "$SYN_SRC/$name" "$name"
	done
	dir=$work
	if (( ${#src} > 0 )) && [ -z "$noextract" ]; then
		fields "${src[1]}"
		case $name in
			*.zip) python3 -m zipfile -e "$name" . ;;
			*) tar -xf "$name" ;;
		esac || { print "FAILED to unpack $name"; return 1 }
		# Into the tarball's top directory if it has exactly one
		dirs=(*/(N))
		(( ${#dirs} == 1 )) && dir=$work/${dirs[1]%/}
	fi
	if zsh -c 'setopt err_exit pipe_fail sh_word_split; . "$1"; . "$2"; cd "$3"; build' syn \
		"$HERE/lib.zsh" "$HERE/pkgs/$p" "$dir" >"$log" 2>&1; then
		ldconfig
		print -r -- "$v" > "$STATE/$p"
		cd / && rm -rf "$work"
		printf 'ok   %dm%02ds\n' $(((SECONDS - start) / 60)) $(((SECONDS - start) % 60))
	else
		print "FAILED"
		tail -n 40 "$log"
		print
		print "full log: $log   build tree kept in $work"
		return 1
	fi
}

do_build() {
	local p
	export SYN_SRC=$1
	shift
	[ -d "$SYN_SRC" ] || { print "no sources at $SYN_SRC (run: build.zsh fetch)"; return 1 }
	export SYN_FILES=$HERE/files SYN_REPO=${HERE:h:h}
	export MAKEFLAGS=-j$(nproc)
	export XORG_PREFIX=/usr
	export XORG_CONFIG="--prefix=/usr --sysconfdir=/etc --localstatedir=/var --disable-static"
	mkdir -p "$STATE" "$LOGS"
	if (( $# > 0 )); then
		for p in "$@"; do
			[ -f "$HERE/pkgs/$p" ] || { print "no recipe: $p"; return 1 }
			build_one "$p" || return 1
		done
		return 0
	fi
	for p in $(recipes); do
		load "$p"
		[ -z "$always" ] && [ "$(cat "$STATE/$p" 2>/dev/null)" = "$v" ] && continue
		build_one "$p" || return 1
	done
	print "desktop build complete"
}

case $1 in
	fetch) shift; do_fetch "$@" ;;
	build) shift; do_build "$@" ;;
	list) recipes ;;
	*) sed -n '/^#   Builds/,/^# ---/p' "$0"; exit 1 ;;
esac

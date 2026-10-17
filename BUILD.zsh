#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                        S Y N - O S   B U I L D
#
#   One way in to building SYN-OS, whichever way it's built. An edition is
#   one plain file in SYN-EDITIONS/ saying what to build and how, by its
#   strategy selectors, the way synos.conf does for an install: BaseStrat
#   (arch or lfs), PackageStrat, KernelStrat, LayerStrat, OutputStrat. This
#   reads it, checks it carefully, refusing anything that doesn't add up,
#   and only then runs the builder those selectors pick:
#
#     BaseStrat=arch  BUILD-ARCHISO.zsh on the edition's archiso profile,
#                     or with LayerStrat=none, mkarchiso on a copy of the
#                     profile as it is (Arch's own releng, say)
#     BaseStrat=lfs   SYN-LFS/build.zsh, every step in order, carrying on
#                     from the one that stopped last time; PackageStrat
#                     says where its desktop comes from (source or xbps)
#
#   Usage: zsh BUILD.zsh                    the editions there are
#          zsh BUILD.zsh check EDITION      check it, say what would run
#          zsh BUILD.zsh status EDITION     how far its build has got
#          zsh BUILD.zsh show EDITION       its settings, KEY=value a line
#                                           (what syn-iso-builder reads)
#          zsh BUILD.zsh EDITION [STEP...]  check it, then build it; for an
#                                           lfs edition, STEPs run just those
#                                           build.zsh steps (kernel, iso...)
#   EDITION is a name in SYN-EDITIONS (syn-os-arch) or a path to a file.
#   Nothing here asks anything: every answer is in the edition's file.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : BUILD (Build)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
setopt err_return pipe_fail

REPO=${0:A:h}
EDITIONS=$REPO/SYN-EDITIONS
SU=doas
(( $+commands[doas] )) || SU=sudo

# ---- Reading an edition ------------------------------------------------
# The file is plain zsh assignments; it's syntax-checked before it's read
edition_file() {
	local e=$1
	[[ -f $e ]] && { print -r -- ${e:A}; return }
	[[ -f $EDITIONS/$e.conf ]] && { print -r -- $EDITIONS/$e.conf; return }
	return 1
}

edition_load() {
	local f
	f=$(edition_file $1) || { print "no edition '$1': zsh BUILD.zsh lists them"; return 1 }
	zsh -n $f || { print "$f: not valid zsh"; return 1 }
	Edition= Summary= BaseStrat= PackageStrat= KernelStrat= OutputStrat= Profile= WorkDir= LfsProfile=
	LayerStrat=syn
	source $f
	EditionFile=$f
}

# ---- Checking it -----------------------------------------------------------
# Every problem is listed, not just the first, and nothing runs while there
# is one
edition_check() {
	local -a bad warn
	local k
	for k in Edition BaseStrat PackageStrat KernelStrat OutputStrat; do
		[[ -n ${(P)k} ]] || bad+="$k is not set"
	done
	[[ $BaseStrat == (arch|lfs|) ]] || bad+="BaseStrat=$BaseStrat: arch or lfs"
	[[ $OutputStrat == (iso|) ]] || bad+="OutputStrat=$OutputStrat: iso"
	[[ $LayerStrat == (syn|none) ]] || bad+="LayerStrat=$LayerStrat: syn or none"

	case $BaseStrat in
	arch)
		[[ $PackageStrat == pacman ]] || bad+="PackageStrat=$PackageStrat: an arch base gets its packages with pacman"
		[[ $KernelStrat == arch ]] || bad+="KernelStrat=$KernelStrat: an arch base uses Arch's kernel (arch)"
		[[ -f $(profile_dir)/profiledef.sh ]] || bad+="Profile=$Profile: no profiledef.sh there"
		[[ -f /etc/arch-release ]] || bad+="this host isn't Arch Linux, which mkarchiso needs"
		(( $+commands[mkarchiso] )) || bad+="mkarchiso is missing: $SU pacman -S archiso"
		;;
	lfs)
		[[ $PackageStrat == (source|xbps) ]] ||
			bad+="PackageStrat=$PackageStrat: an lfs base takes source (compiled) or xbps (Void's)"
		[[ $KernelStrat == syn ]] || bad+="KernelStrat=$KernelStrat: an lfs base builds its own kernel (syn)"
		[[ $LayerStrat == syn ]] || bad+="LayerStrat=$LayerStrat: an lfs base always has the SYN layer"
		[[ $LfsProfile == (full|minimal) ]] || bad+="LfsProfile=$LfsProfile: full or minimal"
		[[ -n $WorkDir ]] || bad+="WorkDir is not set"
		[[ -f /etc/arch-release ]] || bad+="this host isn't Arch Linux, which SYN-LFS/build.zsh host needs"
		if [[ -n $WorkDir ]]; then
			[[ -d $WorkDir/jhalfs ]] || warn+="no jhalfs in $WorkDir yet: the first run needs zsh BUILD.zsh ${EditionFile:t:r} host"
			# One build at a time in a work directory (build.zsh all holds the lock)
			[[ -e $WorkDir/build.lock ]] && ! flock -n $WorkDir/build.lock true 2>/dev/null &&
				bad+="a build is already running in $WorkDir"
			# A work directory is built one way (build.zsh all refuses to switch)
			local was=$(cat $WorkDir/build.packages 2>/dev/null)
			[[ -z $was && -e $WorkDir/synos.img ]] && was=source
			[[ -z $was || $was == $PackageStrat ]] ||
				bad+="$WorkDir is built with PackageStrat=$was: give this edition its own WorkDir"
			# One image at a time: jhalfs builds at /mnt/syn-os
			if mountpoint -q /mnt/syn-os; then
				local dev=$(findmnt -no SOURCE /mnt/syn-os) back
				back=$(losetup -nO BACK-FILE ${dev%p<->} 2>/dev/null)
				[[ ${back:A} == ${WorkDir:A}/synos.img ]] ||
					bad+="/mnt/syn-os holds another image (${back:-$dev}): build.zsh umount in that one's work directory first"
			fi
			if [[ ! -e $WorkDir/synos.img ]]; then
				local free=$(df --output=avail -BG ${WorkDir:h} 2>/dev/null | tail -1 | tr -dc 0-9)
				(( ${free:-0} >= 60 )) || bad+="${free:-0}G free under ${WorkDir:h}; a new LFS image needs 60G"
			fi
		fi
		local ram=$(awk '/MemTotal/ {print int($2 / 1048576)}' /proc/meminfo)
		(( ram >= 15 )) || warn+="${ram}G of RAM: LFS's biggest packages want 16G"
		;;
	esac
	(( $+commands[$SU] )) || bad+="neither doas nor sudo: the build needs root for its image and chroot"

	for k in $warn; do print "  note: $k"; done
	if (( $#bad )); then
		print "${EditionFile:t}: won't build:"
		for k in $bad; do print "  - $k"; done
		return 1
	fi
}

# What a build of it would run, in words
edition_plan() {
	print "$Edition (${EditionFile:t:r}): $Summary"
	case $BaseStrat in
	arch) if [[ $LayerStrat == none ]]; then print "  runs: $SU mkarchiso on a copy of $(profile_dir) (no SYN layer)"
	      else print "  runs: $SU zsh BUILD-ARCHISO.zsh --profile=$Profile"; fi
	      print "  ISO:  .syncache/isos/${EditionFile:t:r}-<date>.iso" ;;
	lfs)  print "  runs: SYN_OS_WORK=$WorkDir SYN_OS_PACKAGES=$PackageStrat SYN_OS_PROFILE=$LfsProfile zsh SYN-LFS/build.zsh ${@:-all}"
	      print "  ISO:  $WorkDir/syn-os-<date>.iso" ;;
	esac
}

# ---- Status ---------------------------------------------------------------
# lfs: build.zsh's own order of steps, each done or still to come, from the
# build.state it keeps. Both: the edition's newest ISO, which for an arch
# one is NAME-DATE.iso, BUILD.zsh's in .syncache/isos and the ISO
# Builder's in its own directory
edition_status() {
	local name=${EditionFile:t:r} f newest=
	local -a isos
	case $BaseStrat in
	arch)
		isos=({$REPO/.syncache/isos,$HOME/.local/share/syn-os/iso-builder/isos}/$name-<->-<->-<->.iso(N)) ;;
	lfs)
		local steps=(${(f)"$(SYN_OS_PACKAGES=$PackageStrat zsh $REPO/SYN-LFS/build.zsh steps)"})
		local state=$WorkDir/build.state s next=
		[[ ! -f $state && -e $WorkDir/synos.img ]] &&
			print "  (no build.state in $WorkDir: its steps were run one at a time, so what's done isn't recorded;" &&
			print "   a build goes through every step again: what's built is skipped, the kernel and the ISO are made again)"
		for s in $steps; do
			if [[ -f $state ]] && grep -qx $s $state; then print "  done     $s"
			elif [[ -z $next ]]; then next=$s; print "  next     $s"
			else print "  to come  $s"; fi
		done
		[[ -n $next ]] || print "every step done"
		isos=($WorkDir/syn-os-*.iso(N)) ;;
	esac
	for f in $isos; do
		[[ -z $newest || $f -nt $newest ]] && newest=$f
	done
	if [[ -n $newest ]]; then print -r -- "newest ISO: $newest ($(date -r $newest '+%F %H:%M'))"
	else print "no ISO built yet"; fi
}

# ---- Building -------------------------------------------------------------
# Profile is relative to the repository, or an absolute path
profile_dir() { [[ $Profile == /* ]] && print -r -- $Profile || print -r -- $REPO/$Profile }

# LayerStrat=none: the profile exactly as it is, built by mkarchiso alone.
# On a copy, since mkarchiso writes into it (and Arch's own sit in
# /usr/share/archiso); the ISO goes where BUILD-ARCHISO.zsh puts its own
arch_plain() {
	local w=$REPO/.syncache/plain-${EditionFile:t:r} isos=$REPO/.syncache/isos
	$SU rm -rf $w
	mkdir -p $w $isos
	cp -r $(profile_dir) $w/profile
	$SU mkarchiso -v -w $w/work -o $w/out $w/profile || return
	local iso=$isos/${EditionFile:t:r}-$(date +%F).iso
	$SU mv $w/out/*.iso $iso && $SU rm -rf $w
	print "ISO: $iso"
}

# LayerStrat=syn: BUILD-ARCHISO.zsh, which asks before it starts (yes,
# here) and names a --profile build local-DATE, renamed for the edition
arch_syn() {
	local isos=$REPO/.syncache/isos day=$(date +%F)
	print y | $SU zsh $REPO/BUILD-ARCHISO.zsh --profile=$(profile_dir) || return
	$SU mv $isos/local-$day.iso $isos/${EditionFile:t:r}-$day.iso
	print "ISO: $isos/${EditionFile:t:r}-$day.iso"
}

edition_build() {
	case $BaseStrat in
	arch) if [[ $LayerStrat == none ]]; then arch_plain
	      else arch_syn; fi ;;
	lfs)  SYN_OS_WORK=$WorkDir SYN_OS_PACKAGES=$PackageStrat SYN_OS_PROFILE=$LfsProfile \
		zsh $REPO/SYN-LFS/build.zsh ${@:-all} ;;
	esac
}

# Every setting, as the file leaves it once read ($HOME expanded and all)
edition_show() {
	local k
	for k in Edition Summary BaseStrat PackageStrat KernelStrat LayerStrat OutputStrat Profile WorkDir LfsProfile; do
		print -r -- "$k=${(P)k}"
	done
}

list_editions() {
	local f
	print "Editions (SYN-EDITIONS):"
	for f in $EDITIONS/*.conf(N); do
		(
			Edition= Summary= BaseStrat= PackageStrat=
			source $f 2>/dev/null
			printf '  %-18s %-5s %-7s %s\n' ${f:t:r} $BaseStrat $PackageStrat $Summary
		)
	done
	print "\nzsh BUILD.zsh check EDITION, then zsh BUILD.zsh EDITION"
}

case $1 in
	''|list) list_editions ;;
	check)   edition_load $2 && edition_plan "${@:3}" && edition_check && print "ready to build" ;;
	status)  edition_load $2 && edition_status ;;
	show)    edition_load $2 && edition_show ;;
	-h|--help) sed -n '/^#   Usage/,/^#   Nothing/p' $0 ;;
	*)       edition_load $1 && edition_check && edition_plan "${@:2}" && edition_build "${@:2}" ;;
esac

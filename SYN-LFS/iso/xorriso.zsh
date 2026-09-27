#!/usr/bin/env zsh
# xorriso as grub-mkrescue calls it (`-as mkisofs ...`), with the ISO level
# and volume label added up front. grub-mkrescue appends options given after
# `--` once its mkisofs arguments have ended, too late for these: level 3 is
# what lets rootfs.sfs be bigger than 4 GiB, and iso/init finds the medium
# by the SYN_OS label
if [[ $1 == -as && $2 == mkisofs && $3 != -help ]]; then
	exec xorriso -as mkisofs -iso-level 3 -V SYN_OS "${@:3}"
fi
exec xorriso "$@"

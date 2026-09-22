#!/bin/bash
# Builds the live ISO's initramfs tree in /tmp/syn-initramfs, inside the
# chroot (build.zsh iso runs it, then packs the tree): iso/init plus the
# programs it runs and the libraries they link, all taken from this system.
# The drivers it needs (loop, squashfs, overlay, iso9660, USB and disk
# controllers) are built into the kernel, so no modules go in.
set -e
out=/tmp/syn-initramfs
rm -rf $out
mkdir -p $out/{dev,proc,sys,run,newroot,usr/bin,usr/lib,lib64}
ln -s usr/bin $out/bin
ln -s usr/bin $out/sbin
ln -s usr/lib $out/lib
ln -s bin $out/usr/sbin

for prog in bash mount umount switch_root blkid mkdir cp sed grep sleep cat ls; do
	path=$(command -v $prog)
	install -m755 "$path" $out/usr/bin/
	for lib in $(ldd "$path" | grep -o '/[^ ]*'); do
		[ -e "$out$lib" ] || install -Dm755 "$lib" "$out$lib"
	done
done

install -m755 "$(dirname "$0")/init" $out/init
mknod -m 600 $out/dev/console c 5 1
mknod -m 666 $out/dev/null c 1 3
echo "initramfs tree ready in $out"

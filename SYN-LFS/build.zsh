#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                          S Y N - L F S   B U I L D
#
#   Builds a bootable Linux From Scratch system into a disk image and boots
#   it in QEMU under UEFI. The book's own commands are run by jhalfs, the LFS
#   project's automation: the SysV multilib book, whose packages are r13.1's
#   plus i686 libraries next to the x86_64 ones (for Steam); runit/ then
#   swaps in runit.
#   The kernel is a single EFI-stub file with its command line built in,
#   installed as the firmware's default boot path: no bootloader, and the
#   same file Secure Boot will sign later. Nothing on this host's disks or
#   bootloader is touched; root is only used for the image, mounts and the
#   jhalfs chroot (via bin/sudo -> doas).
#   On top of that base, desktop/ builds the SYN desktop (labwc, waybar,
#   iwd, PipeWire, Xwayland, Chrome, Steam) from BLFS-pinned sources, see
#   desktop/syn-pkg.sh.
#
#   Usage: build.zsh <step>
#     host       install what the build needs on an Arch host, and jhalfs
#     all        every step below in order, disk image to installer ISO.
#                Resumes at the step that failed; log in the work directory
#     image      create + partition + mount the disk image (once)
#     mount      re-attach and mount an existing image
#     kernel     generate the kernel .config (defconfig + kernel.syn)
#     configure  write jhalfs's configuration file
#     build      run jhalfs (downloads sources, builds LFS + kernel)
#     runit      replace sysvinit with runit as PID 1 (runit/install.sh, chroot)
#     rekernel   rebuild the kernel in the chroot from the current config
#     fetch      download the desktop's sources (no root needed)
#     desktop    build the desktop in the chroot (desktop NAME...: just those)
#     user NAME  add a login user (wheel, video, audio, input; zsh) and
#                set their password
#     esp        copy the built kernel to the EFI partition as BOOTX64.EFI
#     umount     unmount and detach the image
#     boot       boot the image in QEMU (serial console on this terminal)
#     gui        boot it with a virtio GPU, input and sound, for the desktop
#                (SYN_LFS_QEMU adds QEMU arguments, e.g. a USB Wi-Fi stick)
#     iso        make the installer ISO from the image (see iso/)
#     isotest    boot the ISO in QEMU with a blank disk to install onto
#                (isotest disk: boot that disk afterwards)
# ------------------------------------------------------------------------------
setopt err_exit pipe_fail

HERE=${0:A:h}
W=${SYN_LFS_WORK:-$HOME/SYN-LFS-build}
IMG=$W/lfs.img
MNT=/mnt/lfs
SRC=$W/sources
JHALFS=$W/jhalfs
# The multilib book is a branch that keeps moving, so it's pinned: at this
# commit (2026-09-14) its package versions are exactly LFS 13.1's, which
# is also what the LFS source mirror below is looked up by
LFS_COMMIT=aaa6933c1a018c4cfb8a503e07835b9af3f8b3b2
LFS_RELEASE=13.1
KVER=7.1.8
RUNIT=2.2.0
RUNIT_SHA256=95ef4d2868b978c7179fe47901e5c578e11cf273d292bd6208bd3a7ccb029290
# By partition label, so the same kernel boots the image in QEMU and on a
# real disk or USB stick; rootwait because USB disks show up late. The
# screen (tty0) comes last so it is /dev/console, where boot messages and
# any emergency shell appear on real hardware; the serial port still gets
# the kernel log for QEMU's `boot`
CMDLINE="root=PARTLABEL=synroot rootwait rw console=ttyS0 console=tty0"
OVMF=/usr/share/edk2/x64
LFS_MIRROR=https://ftp.osuosl.org/pub/lfs/lfs-packages

loopdev() { losetup -j $IMG | cut -d: -f1 | head -1 }

step_image() {
	[[ -e $IMG ]] && { print "exists: $IMG (use: mount)"; exit 1 }
	mkdir -p $W $SRC
	truncate -s 20G $IMG
	sgdisk -o -n 1:0:+256M -t 1:ef00 -c 1:SYNEFI -n 2:0:0 -t 2:8300 -c 2:synroot $IMG
	local dev=$(doas losetup -Pf --show $IMG)
	doas mkfs.vfat -F 32 -n SYNEFI ${dev}p1
	doas mkfs.ext4 -q -L synroot ${dev}p2
	doas mkdir -p $MNT
	doas mount ${dev}p2 $MNT
	print "image ready: $dev mounted at $MNT"
}

step_mount() {
	local dev=$(loopdev)
	[[ -n $dev ]] || dev=$(doas losetup -Pf --show $IMG)
	doas mkdir -p $MNT
	mountpoint -q $MNT || doas mount ${dev}p2 $MNT
	print "$dev mounted at $MNT"
}

step_kernel() {
	mkdir -p $SRC $W/kcfg
	[[ -f $SRC/linux-$KVER.tar.xz ]] || \
		curl -fL -o $SRC/linux-$KVER.tar.xz https://www.kernel.org/pub/linux/kernel/v${KVER%%.*}.x/linux-$KVER.tar.xz
	[[ -d $W/kcfg/linux-$KVER ]] || tar -xf $SRC/linux-$KVER.tar.xz -C $W/kcfg
	cd $W/kcfg/linux-$KVER
	make -s defconfig
	make -s kvm_guest.config
	scripts/kconfig/merge_config.sh -m -O . .config $HERE/kernel.syn $HERE/kernel-sound.syn >/dev/null
	scripts/config --enable EFI --enable EFI_STUB \
		--enable DEVTMPFS --enable DEVTMPFS_MOUNT \
		--enable SYSFB_SIMPLEFB --enable DRM --enable DRM_SIMPLEDRM \
		--enable DRM_FBDEV_EMULATION --enable FRAMEBUFFER_CONSOLE \
		--enable CMDLINE_BOOL --set-str CMDLINE "$CMDLINE"
	# CPU microcode, built into the kernel: with no initramfs there's no
	# other way to load it early. The desktop's cpu-microcode recipe puts
	# Intel's and AMD's files in the image; once they're there, every file
	# is named here, and `rekernel` builds them in (the kernel picks the
	# one for the CPU it boots on)
	local ucode=$MNT/usr/lib/syn-lfs/ucode fw
	if [[ -d $ucode/intel-ucode ]]; then
		fw=(${(f)"$(cd $ucode && find intel-ucode -type f && find amd-ucode -name '*.bin')"})
		scripts/config --set-str EXTRA_FIRMWARE "${fw[*]}" \
			--set-str EXTRA_FIRMWARE_DIR /usr/lib/syn-lfs/ucode
		print "microcode: ${#fw} files built in"
	fi
	make -s olddefconfig
	# kernel.syn options that olddefconfig dropped or changed (a renamed
	# symbol, or a dependency that is off): worth a look, not fatal
	local line want got
	for line in ${(f)"$(grep '^CONFIG_' $HERE/kernel.syn)"}; do
		want=${line#*=}
		got=$(grep -E "^${line%%=*}=" .config | cut -d= -f2- || true)
		[[ $got == $want ]] || print "kernel.syn: ${line%%=*} wanted $want, got ${got:-unset}"
	done
	cp .config $W/kernel.config
	print "kernel config: $W/kernel.config"
}

step_configure() {
	cd $JHALFS
	CONFIG_= LFS_COMMIT=$LFS_COMMIT SRC=$SRC W=$W HERE=$HERE python3 - <<'EOF'
import os, sys
sys.path.insert(0, "menu")
import kconfiglib
k = kconfiglib.Kconfig("Config.in")
want = {
    "BOOK_LFS_ANY": "y", "BOOK_LFS": "y", "BRANCH": "y",
    "COMMIT": os.environ["LFS_COMMIT"], "LFS_MULTILIB_I686": "y",
    "BUILD_CHROOT": "y", "BUILDDIR": "/mnt/lfs",
    "GETPKG": "y", "SRC_ARCHIVE": os.environ["SRC"],
    "RUNMAKE": "n", "ALL_CORES": "y", "CONFIG_TESTS": "n", "STRIP": "y",
    "HAVE_FSTAB": "y", "FSTAB": os.environ["HERE"] + "/fstab",
    "CONFIG_BUILD_KERNEL": "y", "CONFIG": os.environ["W"] + "/kernel.config",
    "TIMEZONE": "Europe/London", "LANG": "en_GB.UTF-8", "PAGE_A4": "y",
    "HOSTNAME": "syn-lfs", "NO_PROGRESS_BAR": "y", "REPORT": "y",
}
bad = []
for name, val in want.items():
    sym = k.syms.get(name)
    if sym is None:
        bad.append(name + " (no such symbol)")
        continue
    sym.set_value(val)
    if sym.str_value != val:
        bad.append(f"{name}={sym.str_value!r} (wanted {val!r})")
k.write_config("configuration")
if bad:
    sys.exit("configuration: could not set " + ", ".join(bad))
print("configuration written")
EOF
}

step_build() {
	cd $JHALFS
	[[ -f configuration ]] || { print "run: configure"; exit 1 }
	mountpoint -q $MNT || { print "run: mount"; exit 1 }
	# jhalfs asks up to three yes/no questions before building; answer
	# those and nothing else (an endless `yes` would also feed make).
	printf 'yes\nyes\nyes\n' | PATH=$HERE/bin:$PATH ./jhalfs run
	# Upstream URLs rot (ncurses snapshots vanish); the LFS project mirrors
	# every source of a release, so fall back to it, checked against the
	# book's MD5.
	local dmp=$MNT/sources/MISSING_FILES.DMP f md5
	if [[ -s $dmp ]]; then
		for f in $(awk '{print $1}' $dmp); do
			md5=$(awk -v f="/$f" 'index($1, f) { print $2; exit }' $MNT/sources/urls.lst)
			curl -fsL -o $SRC/$f $LFS_MIRROR/$LFS_RELEASE/$f
			print "$md5  $SRC/$f" | md5sum -c --quiet || { print "bad or missing: $f"; exit 1 }
			doas install -m644 $SRC/$f $MNT/sources/
			print "fetched from the LFS mirror: $f"
		done
		doas rm -f $dmp
	fi
	# The book creates the build user itself and stops if it already exists
	# (left over from an earlier run).
	getent passwd lfs >/dev/null && doas userdel -r lfs
	# jhalfs's Makefile refuses to run without a terminal of at least 80x24.
	PATH=$HERE/bin:$PATH make -C $MNT/jhalfs </dev/tty
}

# The kernel filesystems a chroot into the image needs (as in the book's
# chapter 7), and a clean environment to run things in there
chroot_up() {
	mountpoint -q $MNT || { print "run: mount"; exit 1 }
	doas mount --bind /dev $MNT/dev
	doas mount -t devpts devpts -o gid=5,mode=0620 $MNT/dev/pts
	doas mount -t tmpfs -o nosuid,nodev tmpfs $MNT/dev/shm
	doas mount -t proc proc $MNT/proc
	doas mount -t sysfs sysfs $MNT/sys
	doas mount -t tmpfs tmpfs $MNT/run
}

chroot_down() {
	doas umount $MNT/run $MNT/sys $MNT/proc $MNT/dev/shm $MNT/dev/pts $MNT/dev
}

in_chroot() {
	doas chroot $MNT /usr/bin/env -i HOME=/root TERM=linux \
		PATH=/usr/bin:/usr/sbin LANG=en_GB.UTF-8 "$@"
}

step_runit() {
	mountpoint -q $MNT || { print "run: mount"; exit 1 }
	[[ -f $SRC/runit-$RUNIT.tar.gz ]] || \
		curl -fL -o $SRC/runit-$RUNIT.tar.gz https://smarden.org/runit/runit-$RUNIT.tar.gz
	print "$RUNIT_SHA256  $SRC/runit-$RUNIT.tar.gz" | sha256sum -c --quiet
	doas rm -rf $MNT/tmp/syn-runit
	doas mkdir -p $MNT/tmp/syn-runit
	doas cp -r $HERE/runit $SRC/runit-$RUNIT.tar.gz $MNT/tmp/syn-runit/
	chroot_up
	local rc=0
	in_chroot /bin/sh /tmp/syn-runit/runit/install.sh || rc=$?
	chroot_down
	return $rc
}

# Builds the kernel inside the image from $W/kernel.config (run `kernel`
# first), for an LFS that jhalfs already built with an older config.
# Replaces the book's kernel file and module tree; `esp` then boots it.
step_rekernel() {
	[[ -f $W/kernel.config ]] || { print "run: kernel"; exit 1 }
	chroot_up
	doas rm -rf $MNT/tmp/syn-kernel
	doas mkdir -p $MNT/tmp/syn-kernel
	doas cp $SRC/linux-$KVER.tar.xz $W/kernel.config $MNT/tmp/syn-kernel/
	local rc=0
	in_chroot /bin/bash -e -c '
		cd /tmp/syn-kernel
		tar -xf linux-$1.tar.xz
		cd linux-$1
		cp ../kernel.config .config
		make olddefconfig
		make -j$(nproc)
		rm -rf /usr/lib/modules/$1
		make modules_install
		kernel=$(ls /boot/vmlinuz-$1-* 2>/dev/null | head -1)
		cp arch/x86/boot/bzImage ${kernel:-/boot/vmlinuz-$1-syn}
		cp .config /boot/config-$1
		cd / && rm -rf /tmp/syn-kernel
	' rekernel $KVER || rc=$?
	chroot_down
	(( rc == 0 )) && print "kernel rebuilt; next: esp"
	return $rc
}

step_fetch() {
	bash $HERE/desktop/syn-pkg.sh fetch $SRC/desktop
}

# The chroot sees the fetched sources at /sources/syn-desktop and this
# repo, read-only, at /usr/src/SYN-OS (recipes, SYN-SOFTWARE, dotfiles)
step_desktop() {
	[[ -d $SRC/desktop ]] || { print "run: fetch"; exit 1 }
	chroot_up
	doas mkdir -p $MNT/sources/syn-desktop $MNT/usr/src/SYN-OS
	doas mount --bind $SRC/desktop $MNT/sources/syn-desktop
	doas mount --bind $HERE:h $MNT/usr/src/SYN-OS
	doas mount -o remount,bind,ro $MNT/usr/src/SYN-OS
	local rc=0
	in_chroot /bin/bash /usr/src/SYN-OS/SYN-LFS/desktop/syn-pkg.sh \
		build /sources/syn-desktop "$@" || rc=$?
	doas umount $MNT/usr/src/SYN-OS $MNT/sources/syn-desktop
	chroot_down
	return $rc
}

# Run after `desktop`, so the new home gets the SYN dotfiles from /etc/skel
step_user() {
	[[ -n $1 ]] || { print "usage: build.zsh user NAME"; exit 1 }
	chroot_up
	local rc=0
	in_chroot /bin/bash -e -c '
		id -u "$1" > /dev/null 2>&1 ||
			useradd -m -G wheel,video,audio,input -s /bin/zsh "$1"
		passwd "$1"
		home=$(getent passwd "$1" | cut -d: -f6)
		if [ -x /usr/lib/syn-os/syn-wallgen ] && [ -d "$home/.config/syn-os/themes" ]; then
			/usr/lib/syn-os/syn-wallgen --themes-dir "$home/.config/syn-os/themes" \
				--out-dir "$home/.wallpaper" && chown -R "$1:$1" "$home/.wallpaper"
		fi
	' user $1 </dev/tty || rc=$?
	chroot_down
	(( rc == 0 )) && print "user $1 ready: log in on tty1 and type synos"
	return $rc
}

step_esp() {
	local dev=$(loopdev) kernel=($MNT/boot/vmlinuz-*(N))
	(( $#kernel )) || { print "no kernel in $MNT/boot yet"; exit 1 }
	mkdir -p $W/esp
	doas mount ${dev}p1 $W/esp
	doas install -Dm644 $kernel[1] $W/esp/EFI/BOOT/BOOTX64.EFI
	doas umount $W/esp
	print "installed ${kernel[1]:t} as EFI/BOOT/BOOTX64.EFI"
}

step_umount() {
	local dev=$(loopdev)
	mountpoint -q $MNT && doas umount -R $MNT
	[[ -n $dev ]] && doas losetup -d $dev
	print "image detached"
}

qemu_run() {
	[[ -n $(loopdev) ]] && { print "image still attached — run: umount"; exit 1 }
	[[ -f $W/OVMF_VARS.fd ]] || cp $OVMF/OVMF_VARS.4m.fd $W/OVMF_VARS.fd
	qemu-system-x86_64 -enable-kvm -cpu host -smp 4 \
		-drive if=pflash,format=raw,readonly=on,file=$OVMF/OVMF_CODE.4m.fd \
		-drive if=pflash,format=raw,file=$W/OVMF_VARS.fd \
		-drive file=$IMG,format=raw,if=virtio \
		-nic user,model=virtio-net-pci \
		-serial mon:stdio "$@"
}

step_boot() {
	qemu_run -m 2G -display none
}

# Display, input and sound for a desktop VM, in $reply. virtio-vga-gl
# needs QEMU's virgl support (Arch: qemu-desktop); with SYN_LFS_DISPLAY=sdl
# or gtk (no ,gl=on) Mesa's llvmpipe draws instead
desktop_args() {
	local gpu=virtio-vga-gl
	[[ ${SYN_LFS_DISPLAY:-gtk,gl=on} == *gl=on* ]] || gpu=virtio-vga
	reply=(-device $gpu -display ${SYN_LFS_DISPLAY:-gtk,gl=on}
		-device virtio-keyboard-pci -device virtio-tablet-pci
		-audiodev pipewire,id=snd0 -device ich9-intel-hda -device hda-duplex,audiodev=snd0
		${=SYN_LFS_QEMU})
}

step_gui() {
	desktop_args
	qemu_run -m 4G $reply
}

# The installer ISO, from the built image: the system as a squashfs (with
# anyone added by `user` and the build leftovers left out), its kernel, an
# initramfs that boots it live (iso/init), and GRUB with the SYN-OS splash.
# Run after `desktop`, then `kernel` and `rekernel`, so the kernel has the
# live-boot options and the CPU microcode built in.
step_iso() {
	local t
	for t in mksquashfs grub-mkrescue xorriso mformat bsdtar zstd; do
		(( $+commands[$t] )) || {
			print "missing $t. Arch: doas pacman -S --needed squashfs-tools grub libisoburn mtools libarchive zstd"
			exit 1
		}
	done
	mountpoint -q $MNT || { print "run: mount"; exit 1 }
	[[ -f $MNT/usr/lib/syn-os/syn-lfs-install.zsh ]] || { print "no installer in the image yet; run: desktop"; exit 1 }
	grep -q '^CONFIG_OVERLAY_FS=y' $MNT/boot/config-$KVER 2>/dev/null ||
		{ print "the image's kernel can't boot live yet; run: kernel, rekernel"; exit 1 }

	local iso=$W/iso out=$W/syn-lfs-$(date +%Y.%m.%d).iso acct=$W/iso-accounts rc=0
	doas rm -rf $iso $acct
	mkdir -p $iso/boot/grub $iso/syn-lfs

	# initramfs: put together from the system's own programs in the
	# chroot, packed out here
	chroot_up
	doas mkdir -p $MNT/usr/src/SYN-OS
	doas mount --bind $HERE:h $MNT/usr/src/SYN-OS
	doas mount -o remount,bind,ro $MNT/usr/src/SYN-OS
	in_chroot /bin/bash /usr/src/SYN-OS/SYN-LFS/iso/mkinitramfs.sh || rc=$?
	doas umount $MNT/usr/src/SYN-OS
	chroot_down
	(( rc == 0 )) || return $rc
	(cd $MNT/tmp/syn-initramfs && doas bsdtar --format newc -cf - .) | zstd -19 -T0 -q > $iso/boot/initramfs.img
	doas rm -rf $MNT/tmp/syn-initramfs

	local kernel=($MNT/boot/vmlinuz-*(N))
	cp $kernel[1] $iso/boot/vmlinuz
	cp $HERE/iso/grub.cfg $iso/boot/grub/grub.cfg
	cp $HERE:h/SYN-ISO-PROFILE/grub/splash.png $iso/boot/grub/splash.png

	# The root image. Only the system accounts go on it: filtered copies
	# (iso/sanitize-accounts.sh) sit over the image's own while it's read
	doas mkdir -m 700 $acct
	doas sh $HERE/iso/sanitize-accounts.sh $MNT/etc $acct
	local f bound=()
	for f in passwd group shadow gshadow; do
		[[ -e $acct/$f ]] || continue
		doas mount --bind $acct/$f $MNT/etc/$f
		bound+=($MNT/etc/$f)
	done
	doas mksquashfs $MNT $iso/syn-lfs/rootfs.sfs -noappend -comp zstd -Xcompression-level 19 -b 1M \
		-wildcards -e sources jhalfs 'tmp/*' 'var/tmp/*' var/log/syn-lfs usr/src/SYN-OS \
		'home/*' 'root/*' 'root/.*' lost+found 'boot/efi/*' || rc=$?
	for f in $bound; do doas umount $f; done
	doas rm -rf $acct
	(( rc == 0 )) || return $rc

	grub-mkrescue -o $out $iso -- -V SYN_LFS -iso-level 3
	print "ISO ready: $out ($(du -h $out | cut -f1)). Try it with: isotest"
}

# Boots the newest ISO in QEMU with a blank 40G disk to install onto
# ($W/install-test.img); `isotest disk` then boots that disk on its own
step_isotest() {
	local isos=($W/syn-lfs-*.iso(Nom)) disk=$W/install-test.img vars=$W/OVMF_VARS.isotest.fd media=()
	if [[ $1 == disk ]]; then
		[[ -f $disk ]] || { print "no $disk yet: run isotest and install first"; exit 1 }
	else
		(( $#isos )) || { print "run: iso"; exit 1 }
		[[ -f $disk ]] || truncate -s 40G $disk
		media=(-drive file=$isos[1],media=cdrom,readonly=on)
	fi
	[[ -f $vars ]] || cp $OVMF/OVMF_VARS.4m.fd $vars
	desktop_args
	qemu-system-x86_64 -enable-kvm -cpu host -smp 4 -m 4G \
		-drive if=pflash,format=raw,readonly=on,file=$OVMF/OVMF_CODE.4m.fd \
		-drive if=pflash,format=raw,file=$vars \
		-drive file=$disk,format=raw,if=virtio $media \
		-nic user,model=virtio-net-pci -serial mon:stdio $reply
}

# The build host: the LFS book's host requirements, the tools these steps
# use, QEMU to boot the result, and jhalfs itself
step_host() {
	local su=sudo
	(( $+commands[doas] )) && su=doas
	$su pacman -Syu --needed --noconfirm base-devel git python wget texinfo bc zsh \
		opendoas libxslt docbook-xml docbook-xsl gptfdisk dosfstools e2fsprogs \
		squashfs-tools grub libisoburn mtools libarchive zstd qemu-base edk2-ovmf
	if [[ ! -e /etc/doas.conf ]]; then
		print 'permit persist :wheel' | $su tee /etc/doas.conf > /dev/null
		$su chmod 600 /etc/doas.conf
	fi
	[[ -d $JHALFS ]] || git clone -q https://git.linuxfromscratch.org/jhalfs.git $JHALFS
	print "host ready: $(nproc) CPUs, $(free -g | awk '/Mem:/ {print $2}')G RAM, jhalfs in $JHALFS"
}

# Everything, in order, unattended: expect the best part of a day. Each
# finished step is recorded in build.state, so running this again carries
# on from the one that failed rather than starting over. The kernel is
# built twice; the second pass picks up the CPU microcode the desktop
# installed, which is built into it
step_all() {
	local state=$W/build.state log=$W/build.log s cmd start
	mkdir -p $W
	touch $state
	for s in image kernel configure build runit fetch desktop kernel2 rekernel iso; do
		grep -qx $s $state && continue
		cmd=${s%2}
		[[ $cmd == image && -e $IMG ]] && cmd=mount
		[[ $cmd != (image|fetch) && -e $IMG ]] && step_mount > /dev/null
		print "== $s: started $(date +%T)"
		start=$SECONDS
		if step_$cmd 2>&1 | tee -a $log; then
			print $s >> $state
			print "== $s: done in $(( (SECONDS - start) / 60 ))m"
		else
			print "== $s: FAILED after $(( (SECONDS - start) / 60 ))m, log in $log"
			return 1
		fi
	done
	print "== finished. Boot the ISO with: $0 isotest"
}

(( $+functions[step_$1] )) || { sed -n '/^#   Usage/,/^# ---/p' $0; exit 1 }
step_$1 "${@:2}"

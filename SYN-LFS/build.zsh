#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                          S Y N - L F S   B U I L D
#
#   Builds a bootable Linux From Scratch system into a disk image and boots
#   it in QEMU under UEFI. The book's own commands are run by jhalfs, the LFS
#   project's automation (SysV book r13.1; runit/ then swaps in runit).
#   The kernel is a single EFI-stub file with its command line built in,
#   installed as the firmware's default boot path: no bootloader, and the
#   same file Secure Boot will sign later. Nothing on this host's disks or
#   bootloader is touched; root is only used for the image, mounts and the
#   jhalfs chroot (via bin/sudo -> doas).
#
#   Usage: build.zsh <step>
#     image      create + partition + mount the disk image (once)
#     mount      re-attach and mount an existing image
#     kernel     generate the kernel .config for the book's kernel version
#     configure  write jhalfs's configuration file
#     build      run jhalfs (downloads sources, builds LFS + kernel)
#     runit      replace sysvinit with runit as PID 1 (runit/install.sh, chroot)
#     esp        copy the built kernel to the EFI partition as BOOTX64.EFI
#     umount     unmount and detach the image
#     boot       boot the image in QEMU (serial console on this terminal)
# ------------------------------------------------------------------------------
setopt err_exit pipe_fail

HERE=${0:A:h}
W=${SYN_LFS_WORK:-$HOME/SYN-LFS-build}
IMG=$W/lfs.img
MNT=/mnt/lfs
SRC=$W/sources
JHALFS=$W/jhalfs
LFS_VERSION=r13.1
KVER=7.1.8
RUNIT=2.2.0
RUNIT_SHA256=95ef4d2868b978c7179fe47901e5c578e11cf273d292bd6208bd3a7ccb029290
CMDLINE="root=/dev/vda2 rw console=tty0 console=ttyS0"
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
	scripts/config --enable EFI --enable EFI_STUB \
		--enable DEVTMPFS --enable DEVTMPFS_MOUNT \
		--enable SYSFB_SIMPLEFB --enable DRM --enable DRM_SIMPLEDRM \
		--enable DRM_FBDEV_EMULATION --enable FRAMEBUFFER_CONSOLE \
		--enable CMDLINE_BOOL --set-str CMDLINE "$CMDLINE"
	make -s olddefconfig
	cp .config $W/kernel.config
	print "kernel config: $W/kernel.config"
}

step_configure() {
	cd $JHALFS
	CONFIG_= LFS_VERSION=$LFS_VERSION SRC=$SRC W=$W HERE=$HERE python3 - <<'EOF'
import os, sys
sys.path.insert(0, "menu")
import kconfiglib
k = kconfiglib.Kconfig("Config.in")
want = {
    "BOOK_LFS_ANY": "y", "BOOK_LFS": "y", "BRANCH": "y",
    "COMMIT": os.environ["LFS_VERSION"], "LFS_MULTILIB_NO": "y",
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
			curl -fsL -o $SRC/$f $LFS_MIRROR/${LFS_VERSION#r}/$f
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

step_runit() {
	mountpoint -q $MNT || { print "run: mount"; exit 1 }
	[[ -f $SRC/runit-$RUNIT.tar.gz ]] || \
		curl -fL -o $SRC/runit-$RUNIT.tar.gz https://smarden.org/runit/runit-$RUNIT.tar.gz
	print "$RUNIT_SHA256  $SRC/runit-$RUNIT.tar.gz" | sha256sum -c --quiet
	doas rm -rf $MNT/tmp/syn-runit
	doas mkdir -p $MNT/tmp/syn-runit
	doas cp -r $HERE/runit $SRC/runit-$RUNIT.tar.gz $MNT/tmp/syn-runit/
	doas mount --bind /dev $MNT/dev
	doas mount -t devpts devpts -o gid=5,mode=0620 $MNT/dev/pts
	doas mount -t proc proc $MNT/proc
	doas mount -t sysfs sysfs $MNT/sys
	doas mount -t tmpfs tmpfs $MNT/run
	local rc=0
	doas chroot $MNT /usr/bin/env -i HOME=/root TERM=linux PATH=/usr/bin:/usr/sbin \
		/bin/sh /tmp/syn-runit/runit/install.sh || rc=$?
	doas umount $MNT/run $MNT/sys $MNT/proc $MNT/dev/pts $MNT/dev
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

step_boot() {
	[[ -n $(loopdev) ]] && { print "image still attached — run: umount"; exit 1 }
	[[ -f $W/OVMF_VARS.fd ]] || cp $OVMF/OVMF_VARS.4m.fd $W/OVMF_VARS.fd
	qemu-system-x86_64 -enable-kvm -cpu host -smp 4 -m 2G \
		-drive if=pflash,format=raw,readonly=on,file=$OVMF/OVMF_CODE.4m.fd \
		-drive if=pflash,format=raw,file=$W/OVMF_VARS.fd \
		-drive file=$IMG,format=raw,if=virtio \
		-nic user,model=virtio-net-pci \
		-display none -serial mon:stdio
}

(( $+functions[step_$1] )) || { sed -n '/^#   Usage/,/^# ---/p' $0; exit 1 }
step_$1

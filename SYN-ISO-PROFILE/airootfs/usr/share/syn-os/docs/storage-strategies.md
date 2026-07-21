# Disk and storage options

SYN-OS gives you a few independent choices about how your disk gets set
up: how it's partitioned, whether it's encrypted, whether it uses LVM,
and which filesystem to use. Mix and match whichever you need. All of
this is set in `synos.conf` before install.

## Partition layout

Leave this on `auto` and SYN-OS figures out the right layout for your
machine based on its firmware and whether you want encryption. If you
want to choose yourself, there are three options:

- **UEFI** (most modern machines): the standard modern layout.
- **BIOS, simple**: for older machines. Doesn't support encryption.
- **BIOS, with GRUB**: for older machines that also want encryption. A
  small extra unencrypted boot area is added so the machine can still
  find what it needs to start up.

## Encryption and LVM

Two separate yes/no choices:

- **Encryption** locks your whole drive behind a password, so the data
  is unreadable without it, even if the drive is removed and read on
  another machine.
- **LVM** makes your disk space more flexible to resize and manage later.
  Optional, and independent of encryption, you can use either, both, or
  neither.

If you turn on swap space too, it gets carved out automatically when LVM
is in use.

## Filesystem

Pick which filesystem your system runs on. The default, F2FS, is a good
fit for SSDs and NVMe drives. If you're installing on a spinning hard
drive, ext4 is the safer, more broadly compatible choice.

See [Choosing your setup](./synos-conf.md) for exactly where to set all
of this.

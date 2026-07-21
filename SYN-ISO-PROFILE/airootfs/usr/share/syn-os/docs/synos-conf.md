# Choosing your setup

`/etc/syn-os/synos.conf` is where you tell SYN-OS what you want before
installing. Open it in a text editor, fill in your choices, and run the
installer. That's the whole setup process, no wizard, no clicking through
screens.

If something's wrong in the file, like a missing hostname or a disk that
doesn't exist, the installer tells you exactly what to fix before it
touches your disk at all.

## What you'll need to set

**Basics**

| Setting | What it's for |
|---|---|
| `Hostname` | Your machine's name on the network |
| `UserAccountName` | Your username |
| `UserAccountPassword` | Your password. You have to change this from the placeholder, the installer won't proceed with it left as-is |
| `UserShell` | Your login shell, defaults to zsh |

**Language, keyboard, and time**

| Setting | What it's for |
|---|---|
| `Locale` | Your language and region, e.g. `en_GB.UTF-8` |
| `KeyMap` | Your keyboard layout |
| `TimeZone` | Your time zone |

**Disk**

| Setting | What it's for |
|---|---|
| `Disk` | Which drive to install to, e.g. `/dev/sda`. Check the exact name with `lsblk` first, this gets wiped |
| `PartitionStrat` | Leave as `auto` unless you have a reason to choose manually. See [Disk & storage options](./storage-strategies.md) |
| `Encryption` | `yes` or `no`, whether to encrypt your drive |
| `UseLvm` | `yes` or `no`, whether to use LVM |
| `FilesystemStrat` | Which filesystem to use, `f2fs` (default), `ext4`, `btrfs`, or `xfs` |
| `PackageProfile` | `full` (default) or `minimal` |
| `ZramPercent` | How much of your RAM to use as fast, compressed swap. Defaults to 75%, set to `0` to turn it off |
| `ZramMaxMiB` | A hard cap on that zram swap size, regardless of `ZramPercent` |

**Extras**

| Setting | What it's for |
|---|---|
| `EnableSsh` | `yes` or `no`, turn on remote access over SSH |
| `KernelOpts` | Advanced boot options, leave as default unless you know you need something specific |
| `RequireWipeConfirm` | Leave this on `yes`. It's the one safety check that stops the installer and asks you to confirm before wiping your disk |

Every setting has a sensible default already filled in, the only things
you truly need to change before installing are your disk, username,
password, and locale/timezone/keyboard settings.

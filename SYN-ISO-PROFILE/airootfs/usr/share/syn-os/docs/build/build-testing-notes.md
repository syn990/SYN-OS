# Build and boot testing notes

Working notes from testing named builds in `build-manifest.json` against
real `mkarchiso` builds and real VM boot/install runs. Where
[iso-builder.md](./iso-builder.md) gives the reference answer (does this
build work, and why or why not), this doc keeps the per-build detail: real
quirks, real bugs, and the reasoning behind leaving some builds as
confirmed dead ends rather than patching around them.

This project never had a fully clean, gap-free release across its history,
so these notes record real findings per build, not just a pass/fail stamp.

## Wipe-confirm mechanisms differ by era

The safety gate that stops `synos-install` from wiping a disk by accident
has taken three different forms across the project's history, and each
named build uses whichever form was current when it was made:

- **AEGIS**: an environment variable, `SynosIUnderstandWipe=yes`, must be
  set before running the install command (`SynosIUnderstandWipe=yes
  synos-install`). There is no interactive prompt at all in this era.
- **SYNAPTICS**: a differently-named, differently-cased environment
  variable, `SYNOS_I_UNDERSTAND_WIPE=YES`. Confirmed by reading
  `syn-stage0.zsh` directly, not assumed from AEGIS's convention.
- **Current mainline**: an interactive `[y/N]` prompt at the point of wipe,
  no environment variable needed or read.

All three are real and all three work correctly for the era they belong to.
Anyone testing an older build needs to know which gate that build expects —
setting the current mainline's expectations (or no variable at all) against
an AEGIS or SYNAPTICS build will just stall at a prompt that era doesn't
have, or that era's variable will be silently ignored by current mainline.

## AEGIS

- **Build**: builds cleanly via `--build=aegis`. Produces a real ISO,
  roughly 1.1GB, named `SYN-OS AEGIS - <date>-x86_64.iso`.
- **Boot**: confirmed on a UEFI VM. Real AEGIS-branded live shell, `[SYN-OS
  Live]` banner, internet detected, `synos-install` ready to run.
- **`Encryption=no`/`UseLvm=no` are not respected in this era.** Setting
  either field in `synos.conf` to disable encryption or LVM has no effect
  on AEGIS's install strategy — stage0 always announces
  `uefi-bootctl + luks-lvm + f2fs` and always prompts for a real LUKS
  passphrase, regardless of what those two fields say. This is not a
  testing mistake or a config-file typo; it reflects how that era's
  `syn-config.zsh` actually resolves the install strategy. LUKS+LVM simply
  weren't optional in AEGIS — the fields exist in the config file but
  don't yet gate anything. Per the project's own account, AEGIS was a
  deliberate "leap" that was later stepped back from partly because of
  rough edges like this one; it is not a fluke or a regression to chase
  down and fix retroactively.
- **Password setup is interactive**, a plain `passwd`-style prompt (`Set
  password for syntax990: / New password: / Retype new password:`), not
  config-driven. The `UserAccountPassword` field current mainline reads
  from `synos.conf` doesn't exist yet in AEGIS's config format.
- **Install**: partitioning (GPT + 512MiB ESP + LUKS2 + LVM `vg0`/`root`)
  and `f2fs` filesystem creation both complete once a real passphrase is
  supplied. `pacstrap` retrieves 706 packages (1633 MiB download) — a
  genuine full-desktop install, not a stub profile.
- **Both stages complete successfully end to end.** Stage 0 (partition,
  LUKS, LVM, f2fs, pacstrap) and Stage 1 (chroot, locale, doas/sudo swap,
  user password, bootloader) both finish cleanly, ending in a real "Stage
  One Complete, Congratulations! You have successfully installed SYN-OS."
  banner, with `systemd-bootx64.efi` correctly copied into the ESP.
- **Verdict**: AEGIS is a genuine, complete, working build today. The
  `Encryption=no`/`UseLvm=no` gap is real and would be worth fixing if this
  era were ever revisited, but it doesn't block a full, successful install —
  it just means those two fields are decorative in AEGIS specifically.

## Host prerequisite: grub

`--build=synaptics` fails outright on a host that doesn't already have
`grub` installed, with `mkarchiso` refusing to even validate the profile:
`grub-install is not available on this host. Install 'grub'!`. This is not
a bug in the build itself — SYNAPTICS's real, historical `profiledef.sh`
declares `bootmodes=('bios.syslinux' 'uefi.grub')`, and a build host that
normally boots itself via systemd-boot/`bootctl` has no reason to have
`grub` installed by default.

The build script handles this automatically now: before building any
profile, it greps that profile's actual `profiledef.sh` for a grub boot
mode and only installs `grub` on the host when that specific build declares
one — not as a blanket requirement for every build regardless of whether it
uses grub. This doesn't touch the host's own bootloader or boot
configuration; it only makes `grub-install`/`grub-mkconfig` available for
`mkarchiso`'s own profile-validation and build steps.

## SYNAPTICS

- **Build**: succeeds once the host has `grub` installed (see above).
  Produces a real ISO, roughly 1.1GB, named
  `SYN-OS-SYNAPTIC-EDITION-<date>-x86_64.iso`, archived to
  `.syncache/isos/synaptics.iso`.
- **Boot**: confirmed. The live-shell layout is distinctly different from
  both AEGIS and current mainline: scripts live at
  `/root/syn-resources/scripts/syn-stage0.zsh` (not
  `/usr/lib/syn-os/syn-stage0.zsh`), the install alias is `syntax990` (not
  `synos-install`), and disk selection is a hand-edited `DISK="/dev/sda"`
  value inside `syn-disk-config.zsh` — there is no `synos.conf` field-based
  disk configuration yet at this point in the project's history.
- **Two disk-signature bugs exist in SYNAPTICS's real February 2026 code,
  and both are already fixed on current mainline**:
  1. A stale LUKS signature left on a disk from a prior install attempt
     survives partitioning and breaks the mount
     (`unknown filesystem type 'crypto_LUKS'`). Current mainline runs
     `wipefs -a` before partitioning specifically to prevent this; that fix
     did not exist yet in SYNAPTICS's era. Clearing it manually with
     `wipefs -a` on the test disk lets the install continue.
  2. The `f2fs` kernel module isn't loaded before the mount is attempted,
     even though `mkfs.f2fs` itself succeeds. Current mainline runs
     `modprobe f2fs` before mounting specifically to prevent this; again,
     not present yet in SYNAPTICS's era. Clearing it manually with
     `modprobe f2fs` lets the install continue.

  Both are real bugs that existed in the shipped code for a meaningful
  stretch of the project's history, not something introduced recently —
  and both are already resolved in current mainline, so a fresh install of
  today's build does not hit either one. They only surface when building
  and testing the historical SYNAPTICS profile specifically.
- **Install completes successfully end to end with no interactive prompts
  at all** — unlike AEGIS, this era's Stage 1 sets up the account
  non-interactively. Ends in a real "SUMMARY: Stage One Complete,
  Congratulations! You have successfully installed SYN-OS with
  systemd-boot bootloader." banner.
- **Verdict**: SYNAPTICS is a genuine, complete, working build once the
  `grub` host prerequisite is satisfied and the two disk-signature bugs
  above are worked around manually on a reused test disk. A fresh disk
  with no prior LUKS signature and a host where `f2fs` is already loaded
  would not hit either bug in the first place.

## Repo-layout fix: retired `[community]`

Arch merged the `[community]` package repository into `[extra]` in
mid-2023 — same packages, same mirrors, purely a repo-index consolidation
on Arch's side. Every profile committed before that merge still has a
`pacman.conf` with a `[community]` block pointing at a repository that no
longer exists, which makes every package listed under it 404 against every
current mirror — a build-breaking failure that has nothing to do with the
profile's actual install logic or package selection.

The build script strips this dead block automatically from any extracted
profile's `pacman.conf` before building, only when the block is actually
present, and prints when it does so rather than doing it silently. Every
package that used to resolve through `[community]` still resolves the same
way through `[extra]` under the same name. This fix touches only the
`pacman.conf` repo-index block itself — it never touches airootfs scripts,
disk configuration, or package selection, which stay exactly as originally
committed for every build.

## Five confirmed dead ends

Five named builds fail even after the `[community]` fix is applied, because
their real, as-shipped package lists depend on packages that no longer
exist anywhere in Arch's repositories — not renamed, not moved to a
different repo, genuinely gone. These are left failing rather than patched:
the goal of `--build=<name>` is reproducing each historical build's real
install strategy and package selection as it actually shipped, not quietly
rewriting what a build depends on to make it pass today.

- **`rtos-v3`** (SYN-RTOS-V3, 2023-02-11) — `neofetch` (deprecated
  upstream, replaced by `fastfetch`), `grub-customizer` (was AUR-only,
  since dropped/unmaintained), `reiserfsprogs` (removed from Arch
  entirely).
- **`syn-rtos-old`** (2023-06-04) — the same package list as `rtos-v3`,
  since it's a direct wholesale import of that same tree: `neofetch`,
  `grub-customizer`, `reiserfsprogs`.
- **`v4`** (SYN-OS-V4, 2023-06-05) — `gnu-netcat` (renamed/replaced; not
  present under this name in current repos) and `reiserfsprogs`.
- **`2035-launch`** (SYN-OS-2035 pre-rename, 2023-09-25) — `gnu-netcat`,
  `reiserfsprogs`, and `wezterm-terminfo` (no longer packaged separately).
- **`rtos-pre-v3`** (2021-03-08, the oldest build in the manifest, and the
  worst-affected) — `gnu-netcat`, `ipw2100-fw` and `ipw2200-fw` (old Intel
  wireless firmware, long since folded into `linux-firmware`),
  `reiserfsprogs`, and `termite-terminfo` (the `termite` terminal itself
  is unmaintained and dropped from Arch).

All five fail for the same class of reason and none of them are script
bugs — the `[community]` reconciliation applies cleanly to every one of
them first, and each still fails afterward purely on missing packages that
have no current equivalent to substitute in without changing what the
build actually is.

## Summary

11 of the 16 named builds in `build-manifest.json` produce real, working
ISOs today: `2035-reset`, `aegis`, `chronomorph`, `m-141`,
`soam-do-huawei`, `synaptics`, `syn-os-raw-baseline`, `syntex`, `volition`,
`xenith`, plus the unnamed `current`/mainline build. 5 are confirmed
permanent dead ends on current Arch infrastructure: `rtos-pre-v3`,
`rtos-v3`, `syn-rtos-old`, `2035-launch`, `v4` — see the table above for
the exact missing packages behind each one.

The `[community]`→`[extra]` repo-index fix is applied automatically to
every build that needs it, and is not the reason any of these five fail.
What stops them is packages permanently gone from Arch's repositories,
which is a fact about Arch's package history, not about this project's
build script — and not something expected to change with any future infra
update. See [iso-builder.md](./iso-builder.md) for the build script
reference and the full 16-build status table, and
[Project History](../history.md) for what each of these named editions
actually was.

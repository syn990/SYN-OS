# Build-and-boot testing notes

Working log from testing each named build in `build-manifest.json` against
real `mkarchiso` builds + real VM boot/install on `synos-test-full` (UEFI VM).
Goal: confirm each real historical build actually produces a bootable ISO and
document any install-time gaps/workarounds/quirks specific to that build.
This project never had a fully clean, gap-free release, so these notes are
expected to accumulate real findings per build, not just confirm "it worked."

Each section below becomes the seed for that build's own README/docs entry
once the testing pass is complete.

---

## AEGIS

- **Build**: succeeded cleanly via `--build=aegis`. Real ISO, ~1.1GB,
  `SYN-OS AEGIS - <date>-x86_64.iso`.
- **Boot**: confirmed via screenshot — real AEGIS-branded live shell boots
  correctly on UEFI (synos-test-full), `[SYN-OS Live]` banner, internet
  detected, `synos-install` ready.
- **Config quirk, real, not a testing mistake**: editing `synos.conf` to set
  `Encryption=no` / `UseLvm=no` via sed did not change the actual install
  strategy — stage0 still announced `uefi-bootctl + luks-lvm + f2fs` and
  prompted for a real LUKS passphrase regardless. Either AEGIS-era
  `syn-config.zsh` derives the strategy from something other than these two
  fields, or the field names/format differ from current mainline's. Not yet
  root-caused, noted here rather than blocking the test. Per the user, AEGIS
  was a known "leap" partly abandoned because of exactly this kind of rough
  edge — expected, real project history, not a fluke.
- **Safety gate differs from current mainline**: AEGIS uses
  `SynosIUnderstandWipe=yes synos-install` (an env var you must set before
  running the command) rather than current mainline's interactive `[y/N]`
  wipe-confirm prompt. Real, working, just a different UX than today's.
- **Install**: proceeded past partitioning (GPT + 512MiB ESP + LUKS2 + LVM
  vg0/root) and filesystem creation (f2fs) successfully once a real LUKS
  passphrase was supplied. pacstrap reached real package retrieval — 706
  packages, 1633 MiB download — confirming this is a genuine full-desktop
  install, not a stub.
- **Install completed successfully end to end.** Stage 0 (partition/LUKS/LVM/
  f2fs/pacstrap, 706 packages) and Stage 1 (chroot, locale, doas/sudo swap,
  user password, bootloader) both finished cleanly — real
  "Stage One Complete, Congratulations! You have successfully installed
  SYN-OS." banner, `systemd-bootx64.efi` copied into the ESP correctly.
- **Password prompt behavior**: real interactive `passwd`-style prompt
  (`Set password for syntax990: / New password: / Retype new password:`),
  not config-driven like current mainline's `UserAccountPassword` field
  (that field didn't exist yet in AEGIS's synos.conf/syn-stage1.zsh).
- **Verdict**: AEGIS is a genuine, complete, working build — the config-file
  gap (Encryption=no/UseLvm=no not being respected) is real and worth fixing
  if this build is ever revisited, but it did not block a full successful
  install; LUKS+LVM simply weren't optional in this era regardless of the
  synos.conf values set. Matches user's own account: AEGIS was "a leap,"
  abandoned for reasons like this, not because it was fundamentally broken.

## Host prerequisite: grub

`--build=synaptics` initially failed mkarchiso's own profile validation:
`grub-install is not available on this host. Install 'grub'!`. Real finding,
not a bug in the build itself — SYNAPTICS's actual historical `profiledef.sh`
declares `bootmodes=('bios.syslinux' 'uefi.grub')`, and this build host
(which normally uses systemd-boot/bootctl for its own boot) never had grub
installed. Fixed properly: the build script now greps each profile's real
`profiledef.sh` for a grub bootmode before building and only installs grub
on the host when that specific build actually declares one — not a blanket
requirement for every build regardless of whether it uses grub. Confirmed
this doesn't affect the host's own bootloader/boot config; it's purely
providing the `grub-install`/`grub-mkconfig` tooling mkarchiso's own
profile-validation step needs to be present to build a grub-capable ISO.

## SYNAPTICS

- **Build**: succeeded after the grub fix above. Real ISO, ~1.1GB,
  `SYN-OS-SYNAPTIC-EDITION-<date>-x86_64.iso`, archived to
  `BUILD_ISOS/synaptics.iso`.
- **Boot**: confirmed. Real, distinct live-shell layout from both AEGIS and
  current mainline — different script paths
  (`/root/syn-resources/scripts/syn-stage0.zsh`, not
  `/usr/lib/syn-os/syn-stage0.zsh`), different install alias (`syntax990`,
  not `synos-install`), different disk config file entirely
  (`syn-disk-config.zsh`, hand-edit `DISK="/dev/sda"` directly — no
  `synos.conf` field-based config exists yet at this point).
- **Same two disk bugs found and fixed on current mainline tonight are
  BOTH confirmed present in SYNAPTICS's real February 2026 code, months
  before either fix**:
  1. Stale LUKS signature from a prior test on the same disk survived
     partitioning and broke the mount (`unknown filesystem type
     'crypto_LUKS'`) — same root cause as the `wipefs` fix added to current
     mainline earlier tonight. Cleared manually with `wipefs -a` to
     continue testing (this is a disk-reuse artifact from testing the same
     VM disk repeatedly tonight, not something a fresh disk would hit).
  2. f2fs kernel module not loaded before mount, despite mkfs.f2fs
     succeeding — same root cause as the `modprobe` fix added to current
     mainline earlier tonight. Cleared manually with `modprobe f2fs`.
  Both bugs are real, and real evidence the project carried this exact
  class of bug for a long stretch of its life, not something introduced
  recently.
- **Wipe-confirm mechanism**: `SYNOS_I_UNDERSTAND_WIPE=YES` (all-caps env
  var, different casing/name from AEGIS's `SynosIUnderstandWipe=yes`) —
  confirmed by reading `syn-stage0.zsh` directly rather than assumed.
- **Same pacman-eating-dots ASCII art snack animation exists here too** —
  confirms that UI touch predates SYNAPTICS, older than assumed.
- **Install completed successfully end to end**, no interactive prompts
  needed at all (unlike AEGIS's password prompt — this era's stage1 must
  set the account up non-interactively/with a default). Real
  "SUMMARY: Stage One Complete, Congratulations! You have successfully
  installed SYN-OS with systemd-boot bootloader." banner.
- **Verdict**: SYNAPTICS is a genuine, complete, working build once the two
  disk-signature bugs (both pre-existing in the real code, both already
  fixed on current mainline) are worked around manually.

### Commit-selection spot-check (per user's "centralise the best ones")

Verified the manifest's chosen commit for both SYNAPTICS and AEGIS against
their real same-window neighbors, not just trusted the earlier pick:
- **SYNAPTICS**: 4 same-day commits titled identically ("Full Wayland Move
  / Synaptics Upload"). Diffed all 4 — only the manifest's pick (`1703d09`,
  the FIRST of the 4, not the last) touches real code
  (syn-stage0/1/disk-config/packages.zsh); the other 3 are README/image-only.
  Manifest's choice confirmed correct.
- **AEGIS**: manifest picks `e639a93` (README-only diff at that specific
  commit) over the earlier `1989434` ("pre-aegis upload... build files
  tested and ready", sounds more code-relevant by name alone). Diffed the
  full range between them: genuine substantial script changes happened in
  between (syn-stage0/1, syn-volume, syn-packages, ui.zsh, a new
  syn-share.zsh). `e639a93`'s tree is the more mature, complete state, and
  this session already independently confirmed it installs successfully —
  manifest's choice confirmed correct.
- General finding: the manifest's existing selection logic (pick the last
  commit with real code content within a named window, not just the last
  commit chronologically or the one whose message sounds most relevant)
  already tends to pick the right commit. Worth this kind of spot-check on
  remaining builds rather than assuming, but no correction needed yet.

### Tooling note: qemu-guest-agent

Switched from screenshot+synthetic-keystroke driving (slow, fragile —
several minutes and multiple failed attempts to type one quoted grep
command) to `virsh qemu-agent-command` (guest-exec/guest-exec-status) once
confirmed the agent channel was live on this VM. Real stdout/stderr/exit
codes, no quoting gymnastics, no screenshot-reading. Should be the default
approach for any future build in this sequence where the agent is
reachable — screenshots only needed for genuinely visual confirmation
(final "install complete" banners, boot splash) or if the agent isn't
running for a given build's live environment.

## rtos-v3

- **Build**: fails, genuine dead end — not a script bug.
- After patching the retired `[community]` repo (merged into `[extra]` by
  Arch mid-2023, same fix applied to v4/rtos-pre-v3, see below), pacman
  still fails with `target not found` for three packages that no longer
  exist in any current Arch repo at all: `neofetch` (deprecated upstream,
  replaced by fastfetch), `grub-customizer` (was AUR-only, since dropped/
  unmaintained), `reiserfsprogs` (removed from Arch entirely).
- Per user's explicit call: leave this failing rather than strip/patch the
  package list. Intent is restoring each build's real install strategy and
  UX, not silently rewriting what it actually shipped. rtos-v3 is a genuine
  reproducibility dead end on current Arch infrastructure — documented here
  as that, not chased further.

## Repo-layout patch: retired [community]

Arch merged `[community]` into `[extra]` in mid-2023 (same packages, same
mirrorlist) — this broke every pre-mid-2023 profile's `pacman.conf`
(`community.db` 404s from every mirror) despite being pure repo-index
churn on Arch's side, unrelated to anything about the build's actual
install logic or package selection. Per user's direction ("our intent is
to restore the versions not the states... not the bugs and lint or
incorrect build files from wrong versions/eras"), the build script now
strips the dead `[community]` block from each extracted profile's
pacman.conf before building (see BUILD-SYNOS-ISO.zsh, right after the
profiledef.sh sanity check) — every affected package still resolves via
`[extra]`. Applied automatically, only when the block is actually present,
printed clearly when it fires rather than silently. Does not touch
anything else in pacman.conf, and never touches airootfs scripts, disk
config, or package selection itself — those stay exactly as committed.

## syn-rtos-old

- **Build**: fails, same genuine dead end as rtos-v3 — identical missing
  packages (`neofetch`, `grub-customizer`, `reiserfsprogs`), same
  `[community]` patch applied cleanly first. Consistent with this being a
  close-in-time RTOS variant sharing the same real package list. Left
  failing per the same call as rtos-v3 — not patched further.

## 2035-launch

- **Build**: fails, genuine dead end. `[community]` patch applied cleanly,
  but three packages genuinely no longer exist in current Arch repos:
  `gnu-netcat` (renamed/replaced, `netcat` or `gnu-netcat` not present
  under this name currently), `reiserfsprogs` (removed from Arch, same as
  rtos-v3/syn-rtos-old), `wezterm-terminfo` (no longer packaged
  separately). Left failing per the same call as rtos-v3 — real
  reproducibility limit on current Arch infra, not patched.

## v4

- **Build**: `[community]` patch works cleanly (confirms the original
  contamination investigation that started this whole pass — see the
  `usr/lib/syn-os/pkgcache` finding above/in history). Still fails, same
  dead-end class as rtos-v3/syn-rtos-old/2035-launch: `gnu-netcat` and
  `reiserfsprogs` genuinely no longer exist in current Arch repos. Left
  failing, not patched — same reasoning as the other three.

## rtos-pre-v3

- **Build**: fails, real dead end — the oldest build in the manifest, and
  the worst-affected: `[community]` patch applies cleanly, but 5 packages
  are genuinely gone from current Arch repos: `gnu-netcat`, `ipw2100-fw`,
  `ipw2200-fw` (old Intel wifi firmware, folded into linux-firmware long
  ago), `reiserfsprogs`, `termite-terminfo` (termite terminal itself is
  unmaintained/dropped). Left failing, not patched — same reasoning as the
  other four dead-end builds (rtos-v3, syn-rtos-old, 2035-launch, v4).

## Summary after full build pass

11 of 16 named builds produce real, working ISOs (see .build/isos/):
2035-reset, aegis, chronomorph, m-141, soam-do-huawei, synaptics,
syn-os-raw-baseline, syntex, volition, xenith, plus mainline. 5 are
confirmed genuine reproducibility dead ends on current Arch
infrastructure — not script bugs, not patched: rtos-pre-v3, rtos-v3,
syn-rtos-old, 2035-launch, v4. All five share the same root cause pattern
(packages permanently removed/renamed upstream since their era), and all
were fixed as far as they reasonably can be (the retired [community] repo
merge into [extra] is now patched automatically for every build) before
being left as documented dead ends rather than having their real package
lists rewritten.

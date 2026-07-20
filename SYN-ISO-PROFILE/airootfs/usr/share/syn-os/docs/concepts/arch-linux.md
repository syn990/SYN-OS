# What is Arch Linux?

Arch Linux is the base every SYN-OS install is built from. Understanding what Arch specifically gives you — and doesn't — explains a lot of design choices in this repo.

## Rolling release

Arch has no version numbers and no scheduled "upgrade to the next major release" event. Running `pacman -Syu` keeps a system continuously on the latest packages, forever — there's no Arch 12 vs Arch 13 the way there's Ubuntu 22.04 vs 24.04. SYN-OS follows the same model: there's no separate SYN-OS version to track after install. `pacman -Syu` is the only upgrade path, same as any Arch system.

## pacman

Arch's package manager — fast, dependency-resolving — is the tool every installer script in this repo shells out to directly (`pacstrap`, `pacman -Sy`, `pacman -U`). There's no abstraction layer between SYN-OS's scripts and pacman; [`syn-packages.zsh`](../packages.md) is literally arrays of pacman package names, nothing more.

## The Arch philosophy

Arch's stated principles — simplicity (minimal patching, upstream-as-possible), user-centrality (you configure it, nothing is decided for you by default), transparency (documented, inspectable) — are the same principles SYN-OS states as its own [Philosophy](../philosophy.md). SYN-OS doesn't deviate from Arch's approach so much as extend it one layer up: Arch gives you a blank, well-documented base and expects you to configure it yourself; SYN-OS is one specific, fully-disclosed configuration of that base — a personal starting point, not a wrapper that hides Arch underneath it.

## The [Arch Wiki](https://wiki.archlinux.org)

SYN-OS doesn't abstract Arch away, so nearly everything worth customizing beyond what these docs cover — a filesystem's advanced features, a service SYN-OS doesn't configure, hardware-specific quirks — is standard Arch, and the Arch Wiki's instructions apply unmodified. It's the first place to look for anything not covered here.

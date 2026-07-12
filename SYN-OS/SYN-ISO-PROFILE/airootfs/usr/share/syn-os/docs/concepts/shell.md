# What is a Shell?

"The terminal" is actually several distinct layers stacked on top of each other. Knowing which is which matters when something's misbehaving: a font problem, a keybinding problem, and a broken alias live in three completely different places.

## TTY

The kernel-level abstraction for a text terminal — historically a physical serial console, now almost always virtual (`/dev/tty1` etc.). Switch to a non-graphical console with `Ctrl+Alt+F2` and you're looking at a raw TTY with no graphical rendering at all, just the kernel's built-in text mode, using whatever console font was set via `vconsole.conf`. That's why [`synos.conf`](../synos-conf.md) has a `VconsoleFont` field — it configures this layer specifically, independent of anything graphical.

## Terminal emulator

A GUI (or Wayland-client) application that emulates what a hardware terminal used to do — draws text, handles a virtual TTY device, renders colors and cursor movement — while running under a windowing system rather than being the actual physical console. SYN-OS ships [`foot`](../../../../../../../../readme.md), a Wayland-native terminal emulator, launched via `LabWC`'s `Super+Return` keybind (see [LabWC](../labwc.md)).

The terminal emulator does not interpret your commands. It just displays text and forwards keystrokes to whatever's running inside it.

## Shell

The actual command interpreter running inside the terminal emulator: this is what parses `ls -la | grep foo`, expands `*.txt`, resolves `$PATH`, and runs your `.zshrc`. SYN-OS uses [zsh](../zsh.md) as the default shell for new accounts (`UserShell` in `synos.conf`), configured with autosuggestions, syntax highlighting, `fzf`, and `zoxide`.

A shell can run without any terminal emulator at all. Scripts, cron jobs, and `arch-chroot ... /bin/zsh /path/to/script.zsh` (exactly how [Stage 0 hands off to Stage 1](../installer-overview.md)) all invoke a shell directly, with no TTY or terminal emulator involved.

## Prompt

The shell-generated text shown before your cursor (`user@host ~/path $`), configurable independently of everything above. SYN-OS's zsh prompt (defined in `.zshrc`) shows user@host, the current directory, git branch/status via `vcs_info`, and a right-aligned clock + last exit code.

## Putting it together

Booting SYN-OS and running `synos-install`, you're typing into a **TTY** (the raw console, since there's no graphical session yet in the live environment), which runs a **shell** (zsh, per `/etc/zsh/zshrc`), which shows you a **prompt**. Once installed and running `synos`, you get a graphical session where [`foot`](../labwc.md) (a **terminal emulator**) runs the **shell** instead of the TTY hosting it directly.

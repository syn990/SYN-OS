# What is a Shell?

"The terminal" is actually several distinct layers stacked on top of each other. Knowing which is which matters when something's misbehaving: a font problem, a keybinding problem, and a broken alias live in three completely different places.

## TTY

The kernel-level abstraction for a text terminal — historically a physical serial console, now almost always virtual (`/dev/tty1` etc.). Switching to a non-graphical console shows a raw TTY with no graphical rendering at all, just the kernel's built-in text mode, using whatever console font was set via `vconsole.conf`. This is why [`synos.conf`](../synos-conf.md) has a console-font field — it configures this layer specifically, independent of anything graphical.

## Terminal emulator

A GUI (or Wayland-client) application that emulates what a hardware terminal used to do — draws text, handles a virtual TTY device, renders colors and cursor movement — while running under a windowing system rather than being the actual physical console. SYN-OS ships `foot`, a Wayland-native terminal emulator, launched via [LabWC](../labwc.md)'s terminal keybind.

The terminal emulator does not interpret commands. It just displays text and forwards keystrokes to whatever's running inside it.

## Shell

The actual command interpreter running inside the terminal emulator: this is what parses `ls -la | grep foo`, expands `*.txt`, resolves `$PATH`, and runs `.zshrc`. SYN-OS uses [zsh](../zsh.md) as the default shell for new accounts, configured with autosuggestions and syntax highlighting sourced from pacman-packaged plugins.

A shell can run without any terminal emulator at all. Scripts, cron jobs, and Stage 0 handing off into a chrooted Stage 1 all invoke a shell directly, with no TTY or terminal emulator involved.

## Prompt

The shell-generated text shown before the cursor (`user@host ~/path $`), configurable independently of everything above. SYN-OS's zsh prompt is defined in `.zshrc`.

## Putting it together

Booting SYN-OS and running the installer, a user is typing into a **TTY** (the raw console, since there's no graphical session yet in the live environment), which runs a **shell** (zsh), which shows a **prompt**. Once installed and the desktop session is started, the same stack exists again one layer up: `foot` (a **terminal emulator**) runs the **shell** instead of the TTY hosting it directly.

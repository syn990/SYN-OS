# What's a shell?

"The terminal" is really a couple of things stacked together.

The **terminal emulator** is the window you see, it draws the text and
handles your keystrokes, but doesn't actually understand any commands.
SYN-OS uses `foot`, a fast, lightweight one built for Wayland.

The **shell** is what actually runs inside that window. It's the thing
reading your commands, running programs, and showing you a prompt. SYN-OS
uses zsh, with a few conveniences already set up, see [The shell](../zsh.md).

You'll mostly just think of these as one thing, "the terminal," and
that's fine day to day. It only matters to tell them apart if something's
misbehaving, a font issue is the terminal window's problem, a broken
command is the shell's.

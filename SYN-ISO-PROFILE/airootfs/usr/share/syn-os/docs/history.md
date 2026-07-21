# Project history

SYN-OS is one continuous project. It's changed names a few times and been
reset to a clean slate more than once, but it's never been a fork of
something else, it's the same project carrying forward.

## Before it was a project

The habits behind SYN-OS go back further than any of the code, roughly
to 2017. Hand-building Arch installs, rewriting dotfiles, compiling
custom ISOs, done from memory each time with nothing saved anywhere.

That changed in 2021, when it finally got a repository: SYN-RTOS. It
wasn't started with the ambition of "let's build an OS," it was closer to
"this is getting too messy to keep redoing from scratch." That's stayed
true through every reset since, the practical need to save the work comes
first, and the project grows from there. SYN-RTOS became SYN-OS in 2023,
and the name's stuck since.

## From a 32-line script to a real installer

The very first installer was 32 lines: one hardcoded disk, no config
file, no encryption, nothing configurable at all. Everything since has
been that script slowly growing capabilities it genuinely needed:
encryption, different disk layouts, a real config file, checks that
refuse to continue if something doesn't add up. Today's installer is far
larger, and every bit of that growth maps to something the very first
version genuinely couldn't do.

## Moving to Wayland

For most of its life, SYN-OS ran the older X11 display system. When it
was time to move to Wayland, the whole old setup was removed in one
clean pass rather than a slow migration, deleted the moment its
replacement actually worked. See [Why Wayland, not X11](./wayland.md).

## The shell

Zsh has been the shell since the very first scripts. For years, the
project carried an entire third-party shell framework bundled directly
inside it. That's gone now, replaced with a much smaller, hand-written
setup that does the same job with far less baggage. One thing that's
never changed across the whole history: nano has always been the default
editor.

## Settling on one config file

The idea of a proper config file to install from goes back almost as far
as the project itself, but it took years to land in the right shape and
the right place on disk. Today's setup, one file, checked carefully
before anything runs, is the result of that slow settling process.

## What's been swapped out along the way

A few tools have been replaced with better fits over time: `htop`
became `btop`, `kitty` became `foot`, `sudo` became `doas`. The file
manager, the audio mixer, and a few other tools each went through a
couple of changes before landing on SYN-OS's own, purpose-built versions.
One package has survived, completely unchanged, since 2022: OpenRA, the
open-source real-time strategy game, the single longest-running entry in
the whole project's history.

## A few things that were built and then removed

Not everything that got built stuck around. A full login-screen system
was built at one point, complete with its own user account and
authentication setup, and never actually shipped, replaced by something
much simpler that does the same job with far less moving underneath it.
The project's directory-mapping tool went through several competing
rewrites before settling into the one clean version that exists today.

## Full resets

A few times over the years, the whole project got consolidated back down
to one clean tree, folding years of side branches and experiments into
what exists today. Release names changed even more often than that,
several different era names have come and gone. None of that naming
survives today, SYN-OS ships as one project, no rotating naming scheme
layered on top.

## What that all adds up to

None of this is just trivia, it's the reason the system looks the way it
does today. SYN-OS has grown real capability over the years: encryption,
proper firmware detection, a real config file in the right place. But
just as often, things that stopped earning their keep got removed rather
than left to pile up. That's the throughline from a 32-line script to
what SYN-OS is now: it grows when it genuinely needs to, and gets cut
back everywhere else.

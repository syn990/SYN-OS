# Philosophy

Most distros make the choices for you and then hide where those choices live. This one doesn't, mostly because I never wanted to have to reverse-engineer my own system six months later.

Every package is in [`syn-packages.zsh`](../../../lib/syn-os/syn-packages.zsh), commented, in plain arrays. Every config ships from [`DotfileOverlay/`](../../../lib/syn-os/DotfileOverlay/) and is just the file, not a template that generates one. The install runs through named stage scripts you can open and read start to finish. If something behaves oddly, the file that caused it is in this repo somewhere.

![A typical distro hides decisions inside the installer; SYN-OS keeps them in plain files the installer just reads, so changing the files changes the result](./diagrams/svg/philosophy-simple.svg)

I've rebuilt this system from nothing more times than I'd like to admit, going back to before this repository existed, before any repository existed. [Project History](./history.md) has the real version of that, sourced from the actual commits and file contents across both the current repo and the one before it, not a tidied-up summary.

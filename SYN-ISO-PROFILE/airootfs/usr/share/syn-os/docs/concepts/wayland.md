# What's Wayland?

Wayland is the modern way Linux desktops draw windows and handle input.
It replaces the much older X11 system, which dates back to 1984 and
carries a lot of design baggage from a very different era of computing.

The big difference: X11 splits the job across two separate pieces
talking to each other indirectly. Wayland does it all in one place, the
compositor, which simplifies things a lot and tends to just run smoother
with less tuning needed.

It's also safer by default. On X11, one app could technically spy on
another app's window or fake your keystrokes. Wayland doesn't allow that
without your window manager's explicit cooperation.

The tradeoff is that Wayland tooling tends to be a bit more tied to which
specific compositor you're running, rather than working identically
everywhere the way old X11 tools did. SYN-OS's own screenshot, recording,
and display tools are all built specifically for the compositor it uses,
LabWC, see [Why Wayland, not X11](../wayland.md).

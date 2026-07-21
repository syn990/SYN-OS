# Why Wayland, not X11

SYN-OS runs entirely on Wayland, the modern replacement for the older
X11 display system most Linux desktops have used for decades. New to
these terms? See [What's Wayland](./concepts/wayland.md) and
[What's a window manager](./concepts/window-manager.md) for a plain
explanation.

## Why it matters

Wayland handles multiple monitors and different screen resolutions much
more gracefully than X11 ever did. It's also simpler under the hood, the
window manager and the display server are one and the same thing,
instead of two separate pieces glued together. That means less that can
go wrong, and tools that work more reliably.

Screenshots, screen recording, locking your screen, and managing your
monitors all use modern, Wayland-native tools built for this, not older
utilities bolted on as an afterthought.

## If you're used to an older setup

If you've customized an older Linux desktop before, using tools like
`xrandr` or `.xinitrc`, none of that applies here. SYN-OS's window
manager, LabWC, is a different, modern project, even though its
configuration style happens to look similar to older tools you might
recognize. Don't assume old guides apply directly, check LabWC's own
documentation if you're going deep on customizing it.

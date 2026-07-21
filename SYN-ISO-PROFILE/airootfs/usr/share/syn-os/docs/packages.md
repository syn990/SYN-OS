# What's included

SYN-OS comes with a curated set of software, everything you need for a
working desktop, without the bloat of installing everything under the
sun.

![What's included, broken down by category](./diagrams/svg/packages-map.svg)

## What you get

- **The core system**: the Linux kernel, drivers, and the tools needed
  to keep the system running and encrypted disks working.
- **Networking**: Wi-Fi, Bluetooth, VPN, and SSH support, all ready to
  go.
- **The shell**: a nicer terminal experience out of the box, faster
  search, smarter navigation, syntax highlighting as you type.
- **The desktop**: the window manager, the bar, the app launcher,
  notifications, screenshot tools, everything that makes up the visual
  experience.
- **Build tools**: a full compiler toolchain, in case you want to build
  your own software or packages later.
- **Fonts**: broad language support and the icon fonts the bar and menus
  use.
- **A few everyday apps**: a media player, an image editor, a web
  browser, and a couple of others to round things out.

## Full or minimal

When you install, you can choose the full set above, or a minimal
profile that skips the build tools and extra apps, just enough for a
working desktop and nothing more. Set this in `synos.conf` under
`PackageProfile`, see [Choosing your setup](./synos-conf.md).

## Changing what's included

If you're building your own version of SYN-OS, the full package list
lives in one file, easy to edit before you build your own ISO. See
[Building your own ISO](./build/iso-builder.md).

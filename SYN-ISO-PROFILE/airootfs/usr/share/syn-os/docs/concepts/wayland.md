# What is Wayland?

Wayland is a display server protocol: a specification for how a compositor and applications talk to each other about windows, input, and rendering. It's the successor to the X Window System (X11), which dates to 1984 and carries architecture decisions from an era of shared mainframes and networked terminals that no longer match how a modern single-user desktop actually works.

## The core architectural difference

**X11** splits the job across two things: the X server (owns the display, handles input, does *some* compositing) and a separate window manager process that tells the X server how to arrange windows. Applications talk to the X server; the X server and the WM negotiate window placement somewhat indirectly, through decades of protocol extensions bolted onto the original design.

**Wayland** collapses this into one role: the compositor. It is the display server, the input handler, and the window manager's rendering backend, all as one process talking a much smaller, simpler protocol directly to applications. There's no intermediary X server to route through — an app hands the compositor a finished pixel buffer, and the compositor composites it directly onto the screen.

## Why this matters practically

- **Security:** on X11, any application can, by default, read another application's window contents or inject fake input — keyloggers, screen-scrapers — a consequence of X11's original networked/multi-user design. Wayland's protocol isolates clients from each other by default; screenshots and input injection require the compositor's explicit cooperation, which is why SYN-OS's [screenshot tooling](../tools/screenshot-and-recording.md) uses `grim`+`slurp`, Wayland-aware tools that ask the compositor for a frame rather than reading X11 window buffers directly.
- **Tearing/latency:** Wayland compositors control frame timing directly rather than negotiating with a separate X server, generally producing smoother output with less manual tuning — no separate compositor like `picom` needed for basic tear-free rendering.
- **Simplicity, at a cost:** Wayland deliberately doesn't standardize things X11 apps took for granted — global window positioning, moving the cursor programmatically, arbitrary screen capture. Each compositor exposes these, if at all, through its own protocol extensions (`wlr-*` protocols, which wlroots-based compositors like LabWC/Sway implement). That's why some older X11-only tools have no Wayland equivalent yet, and why Wayland tooling is generally compositor-specific rather than universal.

## Where this shows up in SYN-OS

Everything in [the desktop stack](../wayland.md) — LabWC, Waybar's `wlr/*` modules, `grim`/`slurp`, `wlr-randr` — depends on the compositor implementing the `wlr` protocol family from the wlroots library. That's a specific flavor of Wayland compositor, shared by Sway and Hyprland among others, not "Wayland" as a monolith. A different Wayland compositor without wlroots support, like GNOME's Mutter, wouldn't run these same modules unmodified.

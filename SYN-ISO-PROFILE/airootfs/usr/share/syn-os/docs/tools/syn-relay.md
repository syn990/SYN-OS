# Connecting to another machine (syn-relay)

SYN-RELAY is how one SYN-OS machine talks to another one on the same
network. Check its stats, launch one of its apps as a window on your own
desktop, or watch and control its screen entirely.

It's one binary that can play several roles at once. A machine can serve
its own stats to a laptop in the next room while also watching a desktop
across the house, all at the same time, each tracked independently.

## What it can do

- **See a remote machine's stats.** Connect to another SYN-OS box and its
  CPU, memory, and disk show up in your own bar and in [System monitor &
  logs](./syn-sysmon.md), same as if it were local.
- **Let others see yours.** Turn on services and any machine that can
  reach you can pull the same numbers.
- **Launch a remote app as a local window.** Pick an app from the
  connected node's menu and it opens as its own window on your desktop,
  streamed over via `waypipe`.
- **Watch and control a screen.** Full remote desktop, one direction at a
  time: you watching them, or them watching you.

## Getting there

Open `SYN-OS Tools → SYN-RELAY` from the main menu. **Connect** links up
to another machine (you'll be prompted for a host if you don't already
have one saved), **Enable/Disable Services** turns your own stats server
on and off, **Remote Apps** lists whatever's launchable on the connected
node, and **Watch Remote Node** / **Allow Remote Watching** cover the two
directions of screen sharing. The bar picks up a matching indicator the
moment any of these are active.

There's a second, separate way to reach a remote app: `SYN-OS Tools →
Remote` reads your `~/.ssh/config` and lists each `Host` entry you've
already got saved there, so you can stream one of that machine's apps
over without going through Connect first.

## Worth knowing before you use it

There's no password and no encryption on any of this, on purpose, for
now. Anyone who can reach your machine on the network can read its stats
or launch an app on it if the stats server is running, and anyone who
knows a watch session's ports can watch and control that screen. It's
built for a trusted home or lab network, not for exposing over the
internet. Don't port-forward it.

Pairing and authentication are real future work, just not built yet.
Nothing about the wire format stops it from being added later.

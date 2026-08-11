# Password and connection prompts (syn-uplink-dialpad)

Whenever SYN-OS needs a password from you, or an IP address, or a host
to connect to, this is the small popup that shows up to ask. Same look
every time, no matter which tool triggered it. The name and the look are
both a straight nod to the dialpad from *Uplink*.

## The three shapes it takes

**The dialpad.** Type digits, `A`-`D`, or `*`/`#`, and each press plays
through syn-bar-core as you go, same as a real DTMF dial tone. Type
anything outside that alphabet, like letters or dots, and it still gets
added to the field quietly, so this same popup doubles as a general
IP-or-hostname prompt anywhere SYN-OS needs one.

**The password box.** A masked single field. Enter submits it, Esc or
`q` cancels with nothing sent.

**The login box.** Username above a masked password field, `Tab` or
`Enter` moves down between them, and both lines get submitted together
once you confirm the password.

Password characters never get their own tone. Every keypress in a
password field plays the exact same fixed click, on purpose, so a
recording of the sound couldn't tell anyone your password's length or
timing.

## Where you'll actually see it

- Any graphical `doas` prompt on the desktop (SYN-OS uses `doas`, not
  `sudo`).
- The first time you connect to a VPN in [Connectivity](./syn-connect.md),
  to collect the username and password once.
- [Connecting to another machine](./syn-relay.md), whenever you connect,
  watch, or allow watching without already having a host typed out.

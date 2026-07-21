# File sharing

A quick way to move files to and from another machine, whether that's
another SYN-OS box, a Windows PC, or anything else on the network. Turn
your machine into a server, or connect out to one, all from the menu.

Find it under SYN-OS Tools → SYN-SHARE, or from the sharing icon on the
bar for one-click access without leaving what you're doing.

## What it can do

Six ways to move files, pick whichever fits what you're connecting to:

- **rsync**, good for syncing folders quickly.
- **Samba**, the standard way to share with Windows machines.
- **NFS**, the standard way to share with other Linux/Unix machines.
- **HTTP**, browse the shared files from any web browser.
- **TFTP**, for network installs or older embedded devices.
- **Netcat**, a raw, no-frills way to just send a folder somewhere.

Each one works both ways. Turn it on as a server to share from this
machine, or use it as a client to pull from (or push to) another one.

## Using it

The menu shows you, at a glance, which services are currently running,
and lets you start or stop any of them with one click. There's also a
one-click "quick share" option that turns on the two simplest methods
(rsync and HTTP) at once and shows you the address to connect to,
handy when you just want to grab a file fast.

Files you share out live in one shared folder on this machine. Files you
pull in from elsewhere land in `~/syn-share-pull`.

Nothing you're not using gets installed. The first time you turn on a
protocol, SYN-OS installs what it needs for that one, nothing more.

You can check what's running, and view the activity log, right from the
same menu.

# How Linux organizes files

Linux keeps everything under one single root folder, no separate drive
letters like C: or D:. A handful of top-level folders show up constantly
once you start poking around, and it helps to know what each one's for.

- **`/etc`**: settings for the system itself. SYN-OS keeps its own
  settings in `/etc/syn-os/`.
- **`/usr`**: where installed software actually lives, programs, shared
  files, and so on.
- **`/home`**: your personal files, and your own settings for the apps
  you use.
- **`/mnt`**: a temporary spot to attach a drive you're working with,
  this is where the installer works while setting up your new system,
  before it's actually your system yet.

You don't need to memorize any of this to use SYN-OS day to day, it's
here for when you're curious about where something lives.

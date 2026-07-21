# How your settings are set up

Every default setting on a fresh SYN-OS install, your desktop config,
themes, wallpapers, terminal setup, all comes from one folder in this
project. Nothing is generated on the fly or hidden away, it's just plain
files, laid out the same way they'll end up on your actual system.

![How your settings reach a real user's home folder](./diagrams/svg/dotfile-overlay.svg)

## How it reaches your account

During install, that whole folder gets copied onto your new system first.
Then, when your user account is created, everything in it becomes the
starting point for your home folder, your `.config`, your wallpaper, all
of it. Any account you create later, even by hand after install, gets the
exact same starting point.

## If you're changing SYN-OS itself

If you've customized your own desktop by hand, that's just your machine,
it doesn't feed back into this project. And editing this project's files
doesn't change your machine either. They only meet at install or rebuild
time, one direction only, from the project into your system.

If you want a change to actually ship in future installs or rebuilds,
make it here in the project, not just on your own desktop.

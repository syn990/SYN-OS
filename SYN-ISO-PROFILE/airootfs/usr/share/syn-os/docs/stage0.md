# The first half of the install

This is what runs before your new system exists yet, while you're still
in the live USB environment.

It checks the disk you picked isn't currently in use by an old install,
then asks you to confirm before wiping it, this is the one moment the
installer stops and waits for you, everything after this runs on its
own.

From there it partitions the disk the way you asked for in the config
file (see [Disk & storage options](./storage-strategies.md) for what the
different choices mean), sets up encryption if you turned it on, formats
everything, and installs the base system onto it.

Once that's done, it hands off to [the second half of the install](./stage1.md),
which runs from inside the system it just built.

Everything printed along the way is saved to a log file on your new
system, so you've got a record if you ever need to check what happened.

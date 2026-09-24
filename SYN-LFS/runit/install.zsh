#!/bin/zsh -f
# Installs runit as PID 1 in place of sysvinit. Runs inside the build chroot
# from /tmp/syn-runit (build.zsh runit copies this directory and the runit
# tarball there). sysvinit stays installed as a fallback: boot with
# init=/usr/sbin/init.sysv to get the book's SysV boot back.
setopt err_exit
cd /tmp/syn-runit
tar xzf runit-2.2.0.tar.gz
cd admin/runit-2.2.0
# GCC 16 defaults to C23, where "int f();" means no arguments; runit's
# djb-style declarations need the older meaning.
echo 'gcc -O2 -Wall -std=gnu17' > src/conf-cc
package/compile
install -m755 command/* /usr/sbin/
install -m644 man/*.8 /usr/share/man/man8/

cd /tmp/syn-runit/runit
install -d /etc/runit /etc/sv /var/service
install -m755 1 2 3 ctrlaltdel /etc/runit/
for dir in sv/*; do
	name=${dir##*/}
	install -Dm755 "$dir/run" "/etc/sv/$name/run"
	[ -f "$dir/finish" ] && install -m755 "$dir/finish" "/etc/sv/$name/finish"
	ln -sfn "/etc/sv/$name" "/var/service/$name"
done

# The book's udev rule runs its setclock script when the RTC appears.
# That is hwclock, which waits for a clock tick before reading and sat
# out a 10 s timeout on every boot in QEMU; the kernel has already set
# the clock, for the SysV fallback too.
sed -i '/setclock/d' /etc/udev/rules.d/55-lfs.rules

# Swap init; halt/poweroff/reboot/shutdown become runit-init front ends.
[ -e /usr/sbin/init.sysv ] || mv /usr/sbin/init /usr/sbin/init.sysv
ln -sf runit-init /usr/sbin/init
for cmd in halt poweroff reboot shutdown; do
	[ -e /usr/sbin/$cmd ] && [ ! -e /usr/sbin/$cmd.sysv ] && mv /usr/sbin/$cmd /usr/sbin/$cmd.sysv
done
printf '#!/bin/zsh -f\nexec /usr/sbin/runit-init 0\n' > /usr/sbin/poweroff
printf '#!/bin/zsh -f\nexec /usr/sbin/runit-init 0\n' > /usr/sbin/halt
printf '#!/bin/zsh -f\nexec /usr/sbin/runit-init 6\n' > /usr/sbin/reboot
printf '#!/bin/zsh -f\n# shutdown [-r] ...: -r reboots, anything else powers off\ncase " $* " in *" -r "*) exec /usr/sbin/runit-init 6 ;; esac\nexec /usr/sbin/runit-init 0\n' > /usr/sbin/shutdown
chmod 755 /usr/sbin/poweroff /usr/sbin/halt /usr/sbin/reboot /usr/sbin/shutdown

rm -rf /tmp/syn-runit
echo "runit installed as PID 1 ($(ls /var/service | tr '\n' ' '))"

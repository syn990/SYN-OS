#!/bin/sh
# Installs runit as PID 1 in place of sysvinit. Runs inside the LFS chroot
# from /tmp/syn-runit (build.zsh runit copies this directory and the runit
# tarball there). sysvinit stays installed as a fallback: boot with
# init=/usr/sbin/init.sysv to get the book's SysV boot back.
set -e
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

# Swap init; halt/poweroff/reboot/shutdown become runit-init front ends.
[ -e /usr/sbin/init.sysv ] || mv /usr/sbin/init /usr/sbin/init.sysv
ln -sf runit-init /usr/sbin/init
for cmd in halt poweroff reboot shutdown; do
	[ -e /usr/sbin/$cmd ] && [ ! -e /usr/sbin/$cmd.sysv ] && mv /usr/sbin/$cmd /usr/sbin/$cmd.sysv
done
printf '#!/bin/sh\nexec /usr/sbin/runit-init 0\n' > /usr/sbin/poweroff
printf '#!/bin/sh\nexec /usr/sbin/runit-init 0\n' > /usr/sbin/halt
printf '#!/bin/sh\nexec /usr/sbin/runit-init 6\n' > /usr/sbin/reboot
printf '#!/bin/sh\n# shutdown [-r] ...: -r reboots, anything else powers off\ncase " $* " in *" -r "*) exec /usr/sbin/runit-init 6 ;; esac\nexec /usr/sbin/runit-init 0\n' > /usr/sbin/shutdown
chmod 755 /usr/sbin/poweroff /usr/sbin/halt /usr/sbin/reboot /usr/sbin/shutdown

rm -rf /tmp/syn-runit
echo "runit installed as PID 1 ($(ls /var/service | tr '\n' ' '))"

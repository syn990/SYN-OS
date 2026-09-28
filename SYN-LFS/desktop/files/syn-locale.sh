# The language, from /etc/locale.conf (the installer writes it from
# synos.conf's Locale). /etc/profile is the LFS book's and is bash's; zsh
# logins read /etc/zsh/zprofile, which sources this directory
[ -r /etc/locale.conf ] && . /etc/locale.conf
export LANG=${LANG:-en_GB.UTF-8}

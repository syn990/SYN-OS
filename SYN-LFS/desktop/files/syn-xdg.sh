# XDG_RUNTIME_DIR, which logind would set up at login elsewhere. runit
# stage 1 creates /run/user/<uid> for every normal user at boot.
if [ -d "/run/user/$(id -u)" ]; then
	export XDG_RUNTIME_DIR=/run/user/$(id -u)
fi

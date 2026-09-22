#!/usr/bin/env zsh
# Starts the audio stack from labwc's autostart, where systemd user units
# would on Arch: PipeWire, then WirePlumber and the PulseAudio-compatible
# server once PipeWire's socket is up. Returns once the pulse socket
# exists (or after a few seconds), so waybar's pulseaudio module finds a
# server when it starts next. Leftovers from an earlier session go first,
# as the daemons refuse to run twice.
uid=$(id -u)
for p in pipewire-pulse wireplumber pipewire; do
	pkill -u "$uid" -x "$p"
done

wait_for() {
	i=0
	while [ ! -S "$1" ] && [ $i -lt 50 ]; do
		sleep 0.1
		i=$((i + 1))
	done
}

pipewire > /dev/null 2>&1 &
wait_for "$XDG_RUNTIME_DIR/pipewire-0"
wireplumber > /dev/null 2>&1 &
pipewire-pulse > /dev/null 2>&1 &
wait_for "$XDG_RUNTIME_DIR/pulse/native"

# QEMU's virtio GPU with virgl shows the compositor's hardware cursor
# upside down. On that GPU only, labwc (wlroots) draws the cursor into
# the frame itself; real GPUs keep the hardware cursor
[ -d /sys/module/virtio_gpu ] && export WLR_NO_HARDWARE_CURSORS=1

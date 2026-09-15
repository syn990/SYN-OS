# ------------------------------------------------------------------------------
#                    S Y N - V A S T - T E M P L A T E
#
#   SYN-OS as a rentable Vast.ai container: labwc/waybar desktop stack
#   from SYN-ISO-PROFILE/airootfs baked onto a fresh Arch base, launched
#   headless (WLR_BACKENDS=headless) so a WebRTC streamer (Selkies) can
#   capture and serve it in the browser. No mkarchiso, no bootloader, no
#   installer chain — those assume a real disk being partitioned, which a
#   container never has.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-VAST-TEMPLATE (Cloud container)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

FROM archlinux:latest

# Refresh keyring first — a stale/expired keyring is the most common reason
# a fresh archlinux:latest pull fails partway through package installs.
RUN pacman -Sy --noconfirm archlinux-keyring && \
    pacman -Syu --noconfirm

# ---- SYN-OS's own package list (SYNCONTAINER array) ----
# syn-packages.zsh is the single source of truth for what SYN-OS installs;
# this script just sources it and prints SYNCONTAINER space-separated so
# pacman can consume it directly, instead of hand-duplicating the list here.
COPY SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/syn-packages.zsh /tmp/syn-packages.zsh
RUN pacman -S --noconfirm --needed zsh && \
    PKGLIST=$(zsh -c 'source /tmp/syn-packages.zsh; echo ${SYNCONTAINER[@]}') && \
    pacman -S --noconfirm --needed ${PKGLIST} && \
    rm /tmp/syn-packages.zsh

# ---- SYN-OS's own rootfs overlay ----
# The real dotfiles/configs/theme system/syn-* scripts — same tree the ISO
# build bakes in, copied here directly rather than reimplemented.
COPY SYN-ISO-PROFILE/airootfs/usr /usr
COPY SYN-ISO-PROFILE/airootfs/etc /etc

# ---- Container user ----
# Real installs get skel-seeded dotfiles via useradd's own -m behavior at
# install time (syn-stage1.zsh); same mechanism here, no separate copy step.
RUN useradd -m -G wheel -s /usr/bin/zsh syn-cloud && \
    echo "syn-cloud ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/syn-cloud

WORKDIR /home/syn-cloud
USER syn-cloud

COPY --chmod=755 SYN-SOFTWARE/syn-vast-template/entrypoint.sh /usr/local/bin/syn-vast-entrypoint.sh
ENTRYPOINT ["/usr/local/bin/syn-vast-entrypoint.sh"]

/* ------------------------------------------------------------------------
 *                       S Y N - B A R - V P N
 *
 *   Waybar custom/vpn module backend: prints "on" if a wg0 interface
 *   exists, nothing otherwise, then exits. Waybar re-execs this on
 *   every interval tick (5s per config.jsonc).
 *
 *   Replaces the inline "ip link show wg0 >/dev/null 2>&1" config.jsonc
 *   exec — if_nametoindex(3) is the direct libc equivalent of that
 *   existence check (it does not consider operstate either, matching
 *   the old command's behavior exactly) with no forked ip(8) process.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-VPN (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <net/if.h>

int main(void) {
	if (if_nametoindex("wg0") != 0) {
		printf("on\n");
	}
	return 0;
}

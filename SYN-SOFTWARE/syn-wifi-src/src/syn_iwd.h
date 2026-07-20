/* ------------------------------------------------------------------------
 *   iwd D-Bus client: finds the station device, scans, lists networks via
 *   Station.GetOrderedNetworks (already signal-sorted, no text parsing),
 *   and connects — registering a real net.connman.iwd.Agent object so iwd
 *   can call back into us for a passphrase instead of us guessing when to
 *   supply one.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WIFI (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_IWD_H
#define SYN_IWD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct sd_bus sd_bus;

typedef struct {
	char name[256];        /* real SSID, already UTF-8 decoded by iwd */
	char type[16];         /* "psk" | "open" | "8021x" | ... */
	char object_path[256]; /* net.connman.iwd network object path */
	int16_t signal_strength; /* raw iwd value, roughly dBm*100; see syn_iwd_signal_bars() */
	bool connected;
} syn_iwd_network;

/* Connects to the system bus and locates the first station-mode wireless
 * device (mirrors the old script's iwctl device list | awk '$5=="station"'
 * search). Returns NULL and fills *err with a human-readable reason if no
 * bus connection or no such device exists. Caller owns *out_bus and must
 * pass it to syn_iwd_close() when done. */
bool syn_iwd_open(sd_bus **out_bus, char *device_path, size_t device_path_len, char *err, size_t err_len);
void syn_iwd_close(sd_bus *bus);

/* Called once per poll tick (every ~50-100ms) while syn_iwd_scan() waits,
 * so a caller with a UI can redraw a spinner/animation — scanning takes
 * real seconds and a static message alone gives no sign of life. */
typedef void (*syn_iwd_tick_cb)(void *userdata);

/* Triggers Station.Scan() and blocks (processing the bus) until the
 * Scanning property goes back to false, or timeout_ms elapses. No fixed
 * sleep — returns as soon as iwd actually reports the scan finished.
 * tick_cb (may be NULL) is invoked on every poll iteration. */
bool syn_iwd_scan(sd_bus *bus, const char *device_path, int timeout_ms, syn_iwd_tick_cb tick_cb, void *userdata, char *err, size_t err_len);

/* Fills *out (caller-allocated array of out_cap entries) from
 * Station.GetOrderedNetworks + each Network object's Name/Type/Connected
 * properties. Returns the real count (may be less than out_cap; excess
 * networks beyond out_cap are silently dropped, not an error). */
int syn_iwd_get_networks(sd_bus *bus, const char *device_path, syn_iwd_network *out, int out_cap, char *err, size_t err_len);

/* Connects to `network_path`. If iwd needs a passphrase it isn't already
 * storing, it calls back into our registered Agent's RequestPassphrase —
 * password_prompt_cb is invoked synchronously from inside this call (the
 * bus call blocks on it), and its return value becomes iwd's answer.
 * password_prompt_cb returns true and fills passphrase_out to supply one,
 * false to decline (iwd reports Connect() as cancelled/failed). Blocks
 * until Connect() actually completes or fails — no polling loop needed
 * by the caller. */
typedef bool (*syn_iwd_password_cb)(const char *ssid, char *passphrase_out, size_t passphrase_out_len, void *userdata);
bool syn_iwd_connect(sd_bus *bus, const char *network_path, syn_iwd_password_cb cb, void *userdata, char *err, size_t err_len);

/* Calls Station.Disconnect() on the device — drops whatever network is
 * currently connected, if any. No Agent involved. */
bool syn_iwd_disconnect(sd_bus *bus, const char *device_path, char *err, size_t err_len);

/* Maps iwd's raw signal_strength (roughly dBm * 100, negative, e.g. -6000
 * for -60dBm) to a 0-4 bar count for display — same rough thresholds
 * NetworkManager/iwctl's own asterisk display uses. */
int syn_iwd_signal_bars(int16_t signal_strength);

#endif

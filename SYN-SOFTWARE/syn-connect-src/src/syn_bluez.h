/* ------------------------------------------------------------------------
 *   BlueZ D-Bus client: finds the first adapter, scans via
 *   Adapter1.StartDiscovery + org.freedesktop.DBus.ObjectManager, and
 *   pairs/trusts/connects real org.bluez.Device1 objects — registering a
 *   real org.bluez.Agent1 (NoInputNoOutput) so BlueZ can drive pairing
 *   without a text-mode bluetoothctl subprocess in the middle.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BLUEZ_H
#define SYN_BLUEZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct sd_bus sd_bus;

typedef struct {
	char name[256];        /* Alias if set, else Name, else the address itself */
	char address[24];      /* "AA:BB:CC:DD:EE:FF" */
	char object_path[256]; /* org.bluez Device1 object path */
	char icon[32];         /* BlueZ's Icon property, e.g. "input-gaming" ("" if unset) */
	int16_t rssi;          /* raw dBm; 0 if the device hasn't advertised one yet */
	bool paired;
	bool trusted;
	bool connected;
} syn_bluez_device;

/* Connects to the system bus and locates the first adapter (hci0 in the
 * common single-adapter case). Returns NULL and fills *err with a
 * human-readable reason if no bus connection or no adapter exists. Caller
 * owns *out_bus and must pass it to syn_bluez_close() when done. */
bool syn_bluez_open(sd_bus **out_bus, char *adapter_path, size_t adapter_path_len, char *err, size_t err_len);
void syn_bluez_close(sd_bus *bus);

/* Called once per poll tick while syn_bluez_scan() waits, so a caller with
 * a UI can redraw a spinner — same convention as syn_iwd_tick_cb. */
typedef void (*syn_bluez_tick_cb)(void *userdata);

/* Calls Adapter1.StartDiscovery, waits duration_ms while pumping the bus
 * (so devices already known plus newly-discovered ones populate BlueZ's
 * object tree), then StopDiscovery. Always calls StopDiscovery before
 * returning, even on error, so a leftover discovery session doesn't drain
 * the adapter after this function gives up. */
bool syn_bluez_scan(sd_bus *bus, const char *adapter_path, int duration_ms, syn_bluez_tick_cb tick_cb, void *userdata, char *err, size_t err_len);

/* Fills *out (caller-allocated array of out_cap entries) by walking
 * GetManagedObjects for every org.bluez.Device1 under adapter_path.
 * Returns the real count (may be less than out_cap; excess devices beyond
 * out_cap are silently dropped, not an error). Works whether or not a scan
 * is currently running — BlueZ keeps discovered devices in its object tree
 * until they age out or get removed. */
int syn_bluez_get_devices(sd_bus *bus, const char *adapter_path, syn_bluez_device *out, int out_cap, char *err, size_t err_len);

/* Registers a NoInputNoOutput org.bluez.Agent1 (auto-confirms "just works"
 * SSP requests and PIN-less legacy pairing — the DS4/DualSense case this
 * was built for), calls Device1.Pair(), then Device1.SetTrusted(true) so
 * the device auto-reconnects later without repairing. Blocks until Pair()
 * completes or fails; the agent is registered only for the duration of
 * this call, not process-lifetime. On success also sets Trusted. */
bool syn_bluez_pair_and_trust(sd_bus *bus, const char *device_path, char *err, size_t err_len);

/* Calls Device1.Connect(). For most peripherals (DS4/DualSense included)
 * a prior Pair()+Trusted is what makes this actually work — a bare
 * Connect() against an unbonded device is expected to fail with the same
 * "Rejected connection from !bonded device" class of error the manual
 * bluetoothctl dance was hitting before ClassicBondedOnly=false. */
bool syn_bluez_connect(sd_bus *bus, const char *device_path, char *err, size_t err_len);

/* Calls Device1.Disconnect(). */
bool syn_bluez_disconnect(sd_bus *bus, const char *device_path, char *err, size_t err_len);

/* Calls Adapter1.RemoveDevice(device_path) — drops pairing/bonding info
 * entirely, same effect as `bluetoothctl remove <MAC>`. Needed before a
 * re-pair attempt on a device stuck in a bad bonded-but-no-linkkey state. */
bool syn_bluez_remove(sd_bus *bus, const char *adapter_path, const char *device_path, char *err, size_t err_len);

/* Maps a raw RSSI dBm reading to a 0-4 bar count for display, same rough
 * thresholds as syn_iwd_signal_bars(). Returns 0 for rssi == 0 (BlueZ
 * hasn't reported one yet — most already-paired devices don't advertise). */
int syn_bluez_signal_bars(int16_t rssi);

#endif

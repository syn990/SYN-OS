/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L
#include "syn_bluez.h"

#include <systemd/sd-bus.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <poll.h>
#include <time.h>

#define BLUEZ_SERVICE "org.bluez"
#define AGENT_PATH "/syn/connect/agent"
#define AGENT_INTERFACE "org.bluez.Agent1"

static void set_err(char *err, size_t err_len, const char *fmt, ...) {
	if (!err || err_len == 0) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, err_len, fmt, ap);
	va_end(ap);
}

/* Walks GetManagedObjects looking for the first object exposing
 * org.bluez.Adapter1 — mirrors syn_iwd's find_station_device() search,
 * just for BlueZ's adapter interface instead of iwd's station device. */
static bool find_adapter(sd_bus *bus, char *out, size_t out_len, char *err, size_t err_len) {
	sd_bus_message *reply = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(bus, BLUEZ_SERVICE, "/", "org.freedesktop.DBus.ObjectManager",
	                           "GetManagedObjects", &error, &reply, "");
	if (r < 0) {
		set_err(err, err_len, "GetManagedObjects failed: %s", error.message ? error.message : strerror(-r));
		sd_bus_error_free(&error);
		return false;
	}

	bool found = false;
	r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{oa{sa{sv}}}");
	if (r < 0) {
		goto done;
	}

	while (!found && sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "oa{sa{sv}}") > 0) {
		const char *obj_path;
		sd_bus_message_read(reply, "o", &obj_path);
		char obj_path_copy[256];
		snprintf(obj_path_copy, sizeof(obj_path_copy), "%s", obj_path);

		bool is_adapter = false;
		sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
		while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") > 0) {
			const char *iface_name;
			sd_bus_message_read(reply, "s", &iface_name);
			if (strcmp(iface_name, "org.bluez.Adapter1") == 0) {
				is_adapter = true;
			}
			sd_bus_message_skip(reply, "a{sv}");
			sd_bus_message_exit_container(reply);
		}
		sd_bus_message_exit_container(reply);
		sd_bus_message_exit_container(reply);

		if (is_adapter) {
			snprintf(out, out_len, "%s", obj_path_copy);
			found = true;
		}
	}
	sd_bus_message_exit_container(reply);

done:
	sd_bus_message_unref(reply);
	if (!found) {
		set_err(err, err_len, "No Bluetooth adapter found");
	}
	return found;
}

bool syn_bluez_open(sd_bus **out_bus, char *adapter_path, size_t adapter_path_len, char *err, size_t err_len) {
	sd_bus *bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0) {
		set_err(err, err_len, "Could not connect to the system D-Bus: %s", strerror(-r));
		return false;
	}

	if (!find_adapter(bus, adapter_path, adapter_path_len, err, err_len)) {
		sd_bus_unref(bus);
		return false;
	}

	*out_bus = bus;
	return true;
}

void syn_bluez_close(sd_bus *bus) {
	if (bus) {
		sd_bus_unref(bus);
	}
}

bool syn_bluez_scan(sd_bus *bus, const char *adapter_path, int duration_ms, syn_bluez_tick_cb tick_cb, void *userdata, char *err, size_t err_len) {
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(bus, BLUEZ_SERVICE, adapter_path, "org.bluez.Adapter1",
	                           "StartDiscovery", &error, NULL, "");
	if (r < 0) {
		/* "org.bluez.Error.InProgress" just means a discovery session from
		 * an earlier run (or another client) is already active — the
		 * object tree still fills in normally, so this isn't fatal. */
		if (!error.name || strcmp(error.name, "org.bluez.Error.InProgress") != 0) {
			set_err(err, err_len, "StartDiscovery failed: %s", error.message ? error.message : strerror(-r));
			sd_bus_error_free(&error);
			return false;
		}
	}
	sd_bus_error_free(&error);

	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed_ms > duration_ms) {
			break;
		}
		if (tick_cb) {
			tick_cb(userdata);
		}
		struct pollfd pfd = {.fd = sd_bus_get_fd(bus), .events = POLLIN};
		poll(&pfd, 1, 100);
		sd_bus_process(bus, NULL);
	}

	sd_bus_error stop_error = SD_BUS_ERROR_NULL;
	sd_bus_call_method(bus, BLUEZ_SERVICE, adapter_path, "org.bluez.Adapter1",
	                   "StopDiscovery", &stop_error, NULL, "");
	sd_bus_error_free(&stop_error);
	return true;
}

/* Reads whichever string properties/booleans/int16 are present for one
 * Device1 object out of a GetManagedObjects sub-dict already positioned at
 * its "sa{sv}" interfaces array — mirrors syn_iwd_get_networks()'s
 * per-property GetAll walk, just inlined into the ObjectManager walk since
 * GetManagedObjects already hands us every device's properties in one
 * call (unlike iwd's GetOrderedNetworks, which only returns paths). */
static void read_device_props(sd_bus_message *reply, syn_bluez_device *d) {
	sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
		const char *prop_name;
		sd_bus_message_read(reply, "s", &prop_name);

		if (strcmp(prop_name, "Alias") == 0) {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "s");
			const char *v;
			sd_bus_message_read(reply, "s", &v);
			snprintf(d->name, sizeof(d->name), "%s", v);
			sd_bus_message_exit_container(reply);
		} else if (strcmp(prop_name, "Name") == 0 && d->name[0] == '\0') {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "s");
			const char *v;
			sd_bus_message_read(reply, "s", &v);
			snprintf(d->name, sizeof(d->name), "%s", v);
			sd_bus_message_exit_container(reply);
		} else if (strcmp(prop_name, "Address") == 0) {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "s");
			const char *v;
			sd_bus_message_read(reply, "s", &v);
			snprintf(d->address, sizeof(d->address), "%s", v);
			sd_bus_message_exit_container(reply);
		} else if (strcmp(prop_name, "Icon") == 0) {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "s");
			const char *v;
			sd_bus_message_read(reply, "s", &v);
			snprintf(d->icon, sizeof(d->icon), "%s", v);
			sd_bus_message_exit_container(reply);
		} else if (strcmp(prop_name, "RSSI") == 0) {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "n");
			int16_t v;
			sd_bus_message_read(reply, "n", &v);
			d->rssi = v;
			sd_bus_message_exit_container(reply);
		} else if (strcmp(prop_name, "Paired") == 0) {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "b");
			int v;
			sd_bus_message_read(reply, "b", &v);
			d->paired = v != 0;
			sd_bus_message_exit_container(reply);
		} else if (strcmp(prop_name, "Trusted") == 0) {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "b");
			int v;
			sd_bus_message_read(reply, "b", &v);
			d->trusted = v != 0;
			sd_bus_message_exit_container(reply);
		} else if (strcmp(prop_name, "Connected") == 0) {
			sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "b");
			int v;
			sd_bus_message_read(reply, "b", &v);
			d->connected = v != 0;
			sd_bus_message_exit_container(reply);
		} else {
			sd_bus_message_skip(reply, "v");
		}
		sd_bus_message_exit_container(reply);
	}
	sd_bus_message_exit_container(reply);
}

int syn_bluez_get_devices(sd_bus *bus, const char *adapter_path, syn_bluez_device *out, int out_cap, char *err, size_t err_len) {
	sd_bus_message *reply = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(bus, BLUEZ_SERVICE, "/", "org.freedesktop.DBus.ObjectManager",
	                           "GetManagedObjects", &error, &reply, "");
	if (r < 0) {
		set_err(err, err_len, "GetManagedObjects failed: %s", error.message ? error.message : strerror(-r));
		sd_bus_error_free(&error);
		return -1;
	}

	int count = 0;
	size_t adapter_len = strlen(adapter_path);
	r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{oa{sa{sv}}}");
	if (r < 0) {
		sd_bus_message_unref(reply);
		set_err(err, err_len, "Malformed GetManagedObjects reply");
		return -1;
	}

	while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "oa{sa{sv}}") > 0) {
		const char *obj_path;
		sd_bus_message_read(reply, "o", &obj_path);

		/* Device1 object paths are always /<adapter_path>/dev_XX_XX_...,
		 * so a plain prefix check confirms this device belongs to our
		 * adapter without a separate round trip per candidate. */
		bool under_adapter = (strncmp(obj_path, adapter_path, adapter_len) == 0 && obj_path[adapter_len] == '/');
		syn_bluez_device d;
		memset(&d, 0, sizeof(d));
		snprintf(d.object_path, sizeof(d.object_path), "%s", obj_path);
		bool is_device = false;

		sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
		while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") > 0) {
			const char *iface_name;
			sd_bus_message_read(reply, "s", &iface_name);
			if (strcmp(iface_name, "org.bluez.Device1") == 0) {
				is_device = true;
				read_device_props(reply, &d);
			} else {
				sd_bus_message_skip(reply, "a{sv}");
			}
			sd_bus_message_exit_container(reply);
		}
		sd_bus_message_exit_container(reply);
		sd_bus_message_exit_container(reply);

		if (is_device && under_adapter && count < out_cap) {
			if (d.name[0] == '\0') {
				snprintf(d.name, sizeof(d.name), "%s", d.address);
			}
			out[count++] = d;
		}
	}
	sd_bus_message_exit_container(reply);
	sd_bus_message_unref(reply);
	return count;
}

/* Minimal org.bluez.Agent1: NoInputNoOutput capability means BlueZ drives
 * "just works" SSP without calling back into us at all for most devices,
 * but RequestAuthorization/AuthorizeService can still fire for
 * device-initiated reconnects, and Cancel/Release must exist for
 * RegisterAgent to accept the vtable at all. Auto-accept everything —
 * this agent only runs for the duration of one Pair() call the user just
 * explicitly requested from the TUI, not process-lifetime. */
static int agent_request_authorization(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	(void)userdata;
	(void)ret_error;
	sd_bus_message *reply = NULL;
	sd_bus_message_new_method_return(m, &reply);
	int r = sd_bus_send(NULL, reply, NULL);
	sd_bus_message_unref(reply);
	return r;
}

static int agent_authorize_service(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	(void)userdata;
	(void)ret_error;
	sd_bus_message *reply = NULL;
	sd_bus_message_new_method_return(m, &reply);
	int r = sd_bus_send(NULL, reply, NULL);
	sd_bus_message_unref(reply);
	return r;
}

static int agent_cancel(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	(void)userdata;
	(void)ret_error;
	sd_bus_message *reply = NULL;
	sd_bus_message_new_method_return(m, &reply);
	int r = sd_bus_send(NULL, reply, NULL);
	sd_bus_message_unref(reply);
	return r;
}

static int agent_release(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	(void)userdata;
	(void)ret_error;
	sd_bus_message *reply = NULL;
	sd_bus_message_new_method_return(m, &reply);
	int r = sd_bus_send(NULL, reply, NULL);
	sd_bus_message_unref(reply);
	return r;
}

static const sd_bus_vtable agent_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("RequestAuthorization", "o", "", agent_request_authorization, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("AuthorizeService", "os", "", agent_authorize_service, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Cancel", "", "", agent_cancel, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Release", "", "", agent_release, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END
};

typedef struct {
	bool done;
	bool ok;
	char *err;
	size_t err_len;
} call_ctx;

static int on_call_reply(sd_bus_message *reply, void *userdata, sd_bus_error *ret_error) {
	(void)ret_error;
	call_ctx *c = userdata;
	c->done = true;
	c->ok = !sd_bus_message_is_method_error(reply, NULL);
	if (!c->ok) {
		const sd_bus_error *e = sd_bus_message_get_error(reply);
		set_err(c->err, c->err_len, "%s", e && e->message ? e->message : "call failed");
	}
	return 0;
}

/* Sends `method` (no args) on device_path/Device1 asynchronously and pumps
 * the bus until it completes — same async-call-plus-pump shape as
 * syn_iwd_connect()'s Connect() call, needed here too because Pair() can
 * trigger our own exported Agent1 methods mid-call; a blocking
 * sd_bus_call_method() would starve those callbacks exactly like it would
 * for iwd's RequestPassphrase. */
static bool call_device_method_async(sd_bus *bus, const char *device_path, const char *method, char *err, size_t err_len) {
	sd_bus_message *call = NULL;
	int r = sd_bus_message_new_method_call(bus, &call, BLUEZ_SERVICE, device_path, "org.bluez.Device1", method);
	if (r < 0) {
		set_err(err, err_len, "Could not build %s call: %s", method, strerror(-r));
		return false;
	}

	call_ctx ctx = {.err = err, .err_len = err_len};
	sd_bus_slot *slot = NULL;
	/* 30s: real pairing (waiting on a controller's Share+PS press, or a
	 * headset's own confirmation) takes longer than any of syn_iwd's
	 * timeouts, which are all scan/connect against an already-visible AP. */
	r = sd_bus_call_async(bus, &slot, call, on_call_reply, &ctx, 30000000);
	sd_bus_message_unref(call);
	if (r < 0) {
		set_err(err, err_len, "%s call failed: %s", method, strerror(-r));
		return false;
	}

	while (!ctx.done) {
		sd_bus_process(bus, NULL);
		if (ctx.done) {
			break;
		}
		struct pollfd pfd = {.fd = sd_bus_get_fd(bus), .events = POLLIN};
		poll(&pfd, 1, 100);
	}
	sd_bus_slot_unref(slot);
	return ctx.ok;
}

bool syn_bluez_pair_and_trust(sd_bus *bus, const char *device_path, char *err, size_t err_len) {
	sd_bus_slot *agent_slot = NULL;
	int r = sd_bus_add_object_vtable(bus, &agent_slot, AGENT_PATH, AGENT_INTERFACE, agent_vtable, NULL);
	if (r < 0) {
		set_err(err, err_len, "Could not export local D-Bus agent: %s", strerror(-r));
		return false;
	}

	sd_bus_error reg_error = SD_BUS_ERROR_NULL;
	r = sd_bus_call_method(bus, BLUEZ_SERVICE, "/org/bluez", "org.bluez.AgentManager1",
	                       "RegisterAgent", &reg_error, NULL, "os", AGENT_PATH, "NoInputNoOutput");
	if (r < 0) {
		set_err(err, err_len, "RegisterAgent failed: %s", reg_error.message ? reg_error.message : strerror(-r));
		sd_bus_error_free(&reg_error);
		sd_bus_slot_unref(agent_slot);
		return false;
	}
	sd_bus_error_free(&reg_error);

	bool paired = call_device_method_async(bus, device_path, "Pair", err, err_len);

	sd_bus_error unreg_error = SD_BUS_ERROR_NULL;
	sd_bus_call_method(bus, BLUEZ_SERVICE, "/org/bluez", "org.bluez.AgentManager1",
	                   "UnregisterAgent", &unreg_error, NULL, "o", AGENT_PATH);
	sd_bus_error_free(&unreg_error);
	sd_bus_slot_unref(agent_slot);

	if (!paired) {
		return false;
	}

	sd_bus_error trust_error = SD_BUS_ERROR_NULL;
	r = sd_bus_set_property(bus, BLUEZ_SERVICE, device_path, "org.bluez.Device1", "Trusted", &trust_error, "b", 1);
	if (r < 0) {
		set_err(err, err_len, "Paired, but SetTrusted failed: %s", trust_error.message ? trust_error.message : strerror(-r));
		sd_bus_error_free(&trust_error);
		return false;
	}
	sd_bus_error_free(&trust_error);
	return true;
}

bool syn_bluez_connect(sd_bus *bus, const char *device_path, char *err, size_t err_len) {
	return call_device_method_async(bus, device_path, "Connect", err, err_len);
}

bool syn_bluez_disconnect(sd_bus *bus, const char *device_path, char *err, size_t err_len) {
	return call_device_method_async(bus, device_path, "Disconnect", err, err_len);
}

bool syn_bluez_remove(sd_bus *bus, const char *adapter_path, const char *device_path, char *err, size_t err_len) {
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(bus, BLUEZ_SERVICE, adapter_path, "org.bluez.Adapter1",
	                           "RemoveDevice", &error, NULL, "o", device_path);
	if (r < 0) {
		set_err(err, err_len, "RemoveDevice failed: %s", error.message ? error.message : strerror(-r));
		sd_bus_error_free(&error);
		return false;
	}
	sd_bus_error_free(&error);
	return true;
}

int syn_bluez_signal_bars(int16_t rssi) {
	if (rssi == 0) return 0;
	if (rssi >= -50) return 4;
	if (rssi >= -60) return 3;
	if (rssi >= -70) return 2;
	if (rssi >= -80) return 1;
	return 0;
}

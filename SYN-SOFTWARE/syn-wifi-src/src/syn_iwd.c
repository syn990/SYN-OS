/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WIFI (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L
#include "syn_iwd.h"

#include <systemd/sd-bus.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <poll.h>
#include <time.h>

#define IWD_SERVICE "net.connman.iwd"
#define AGENT_PATH "/syn/wifi/agent"
#define AGENT_INTERFACE "net.connman.iwd.Agent"

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
 * net.connman.iwd.Device with Mode=="station" — the same "first real
 * wifi card in station mode" search the old zsh script did via
 * `iwctl device list | awk '$5=="station"'`, just against structured
 * D-Bus data instead of column-parsing colored CLI text. */
static bool find_station_device(sd_bus *bus, char *out, size_t out_len, char *err, size_t err_len) {
	sd_bus_message *reply = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(bus, IWD_SERVICE, "/", "org.freedesktop.DBus.ObjectManager",
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

		bool is_device = false, is_station_mode = false;
		char obj_path_copy[256];
		snprintf(obj_path_copy, sizeof(obj_path_copy), "%s", obj_path);

		sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
		while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") > 0) {
			const char *iface_name;
			sd_bus_message_read(reply, "s", &iface_name);
			bool this_is_device_iface = (strcmp(iface_name, "net.connman.iwd.Device") == 0);
			if (this_is_device_iface) {
				is_device = true;
			}

			sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
			while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
				const char *prop_name;
				sd_bus_message_read(reply, "s", &prop_name);
				if (this_is_device_iface && strcmp(prop_name, "Mode") == 0) {
					sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "s");
					const char *mode;
					sd_bus_message_read(reply, "s", &mode);
					if (strcmp(mode, "station") == 0) {
						is_station_mode = true;
					}
					sd_bus_message_exit_container(reply);
				} else {
					sd_bus_message_skip(reply, "v");
				}
				sd_bus_message_exit_container(reply);
			}
			sd_bus_message_exit_container(reply);
			sd_bus_message_exit_container(reply);
		}
		sd_bus_message_exit_container(reply);
		sd_bus_message_exit_container(reply);

		if (is_device && is_station_mode) {
			snprintf(out, out_len, "%s", obj_path_copy);
			found = true;
		}
	}
	sd_bus_message_exit_container(reply);

done:
	sd_bus_message_unref(reply);
	if (!found) {
		set_err(err, err_len, "No wireless device in station mode found");
	}
	return found;
}

bool syn_iwd_open(sd_bus **out_bus, char *device_path, size_t device_path_len, char *err, size_t err_len) {
	sd_bus *bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0) {
		set_err(err, err_len, "Could not connect to the system D-Bus: %s", strerror(-r));
		return false;
	}

	if (!find_station_device(bus, device_path, device_path_len, err, err_len)) {
		sd_bus_unref(bus);
		return false;
	}

	*out_bus = bus;
	return true;
}

void syn_iwd_close(sd_bus *bus) {
	if (bus) {
		sd_bus_unref(bus);
	}
}

static bool get_scanning_property(sd_bus *bus, const char *device_path, bool *out) {
	int r = sd_bus_get_property_trivial(bus, IWD_SERVICE, device_path, "net.connman.iwd.Station",
	                                     "Scanning", NULL, 'b', out);
	return r >= 0;
}

bool syn_iwd_scan(sd_bus *bus, const char *device_path, int timeout_ms, char *err, size_t err_len) {
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(bus, IWD_SERVICE, device_path, "net.connman.iwd.Station",
	                           "Scan", &error, NULL, "");
	if (r < 0) {
		set_err(err, err_len, "Scan failed: %s", error.message ? error.message : strerror(-r));
		sd_bus_error_free(&error);
		return false;
	}

	/* Scan() returns as soon as the request is accepted, not once results
	 * are in (same async behavior the old script's `sleep 3` was working
	 * around) — poll the real Scanning property instead of guessing a
	 * fixed wait, so this returns exactly when iwd is actually done. */
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	bool scanning = true;
	/* Give iwd a moment to flip Scanning to true first — on a fast
	 * network Scan() can complete before our first poll would ever see
	 * it go true, which would otherwise read as "already done". */
	for (int i = 0; i < 20; i++) {
		if (get_scanning_property(bus, device_path, &scanning) && scanning) {
			break;
		}
		struct pollfd pfd = {.fd = sd_bus_get_fd(bus), .events = POLLIN};
		poll(&pfd, 1, 50);
		sd_bus_process(bus, NULL);
	}

	while (scanning) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed_ms > timeout_ms) {
			set_err(err, err_len, "Scan timed out after %dms", timeout_ms);
			return false;
		}
		struct pollfd pfd = {.fd = sd_bus_get_fd(bus), .events = POLLIN};
		poll(&pfd, 1, 100);
		sd_bus_process(bus, NULL);
		if (!get_scanning_property(bus, device_path, &scanning)) {
			break;
		}
	}
	return true;
}

int syn_iwd_get_networks(sd_bus *bus, const char *device_path, syn_iwd_network *out, int out_cap, char *err, size_t err_len) {
	sd_bus_message *reply = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(bus, IWD_SERVICE, device_path, "net.connman.iwd.Station",
	                           "GetOrderedNetworks", &error, &reply, "");
	if (r < 0) {
		set_err(err, err_len, "GetOrderedNetworks failed: %s", error.message ? error.message : strerror(-r));
		sd_bus_error_free(&error);
		return -1;
	}

	int count = 0;
	r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "(on)");
	if (r < 0) {
		sd_bus_message_unref(reply);
		set_err(err, err_len, "Malformed GetOrderedNetworks reply");
		return -1;
	}

	while (count < out_cap && sd_bus_message_enter_container(reply, SD_BUS_TYPE_STRUCT, "on") > 0) {
		const char *net_path;
		int16_t signal;
		sd_bus_message_read(reply, "on", &net_path, &signal);
		sd_bus_message_exit_container(reply);

		syn_iwd_network *n = &out[count];
		memset(n, 0, sizeof(*n));
		snprintf(n->object_path, sizeof(n->object_path), "%s", net_path);
		n->signal_strength = signal;

		/* Name/Type/Connected come from the network object's own
		 * Properties, not GetOrderedNetworks itself — one Properties.Get
		 * call per field would be three round-trips per network, so pull
		 * all three via GetAll in one call instead. */
		sd_bus_message *props_reply = NULL;
		sd_bus_error props_error = SD_BUS_ERROR_NULL;
		int pr = sd_bus_call_method(bus, IWD_SERVICE, net_path, "org.freedesktop.DBus.Properties",
		                            "GetAll", &props_error, &props_reply, "s", "net.connman.iwd.Network");
		if (pr >= 0) {
			sd_bus_message_enter_container(props_reply, SD_BUS_TYPE_ARRAY, "{sv}");
			while (sd_bus_message_enter_container(props_reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
				const char *prop_name;
				sd_bus_message_read(props_reply, "s", &prop_name);
				if (strcmp(prop_name, "Name") == 0) {
					sd_bus_message_enter_container(props_reply, SD_BUS_TYPE_VARIANT, "s");
					const char *name;
					sd_bus_message_read(props_reply, "s", &name);
					snprintf(n->name, sizeof(n->name), "%s", name);
					sd_bus_message_exit_container(props_reply);
				} else if (strcmp(prop_name, "Type") == 0) {
					sd_bus_message_enter_container(props_reply, SD_BUS_TYPE_VARIANT, "s");
					const char *type;
					sd_bus_message_read(props_reply, "s", &type);
					snprintf(n->type, sizeof(n->type), "%s", type);
					sd_bus_message_exit_container(props_reply);
				} else if (strcmp(prop_name, "Connected") == 0) {
					sd_bus_message_enter_container(props_reply, SD_BUS_TYPE_VARIANT, "b");
					int connected;
					sd_bus_message_read(props_reply, "b", &connected);
					n->connected = connected != 0;
					sd_bus_message_exit_container(props_reply);
				} else {
					sd_bus_message_skip(props_reply, "v");
				}
				sd_bus_message_exit_container(props_reply);
			}
			sd_bus_message_exit_container(props_reply);
		}
		sd_bus_message_unref(props_reply);
		sd_bus_error_free(&props_error);

		count++;
	}
	sd_bus_message_exit_container(reply);
	sd_bus_message_unref(reply);
	return count;
}

/* Bridges the Agent's RequestPassphrase D-Bus method call to the caller's
 * plain C callback — set up fresh by syn_iwd_connect() for the one
 * Connect() call it's servicing, then torn down again, rather than a
 * process-lifetime global agent. */
typedef struct {
	syn_iwd_password_cb cb;
	void *userdata;
	char ssid[256]; /* Connect()'s own network name, since RequestPassphrase's
	                  * own argument is an object path, not a human name */
} agent_context;

/* Confirmed empirically against a live iwd instance: RequestPassphrase's
 * real signature is (o path) -> (s passphrase) — not documented in any
 * locally shipped iwd file, so this comment is the record of how that was
 * verified rather than assumed. */
static int agent_request_passphrase(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
	agent_context *ctx = userdata;
	const char *path;
	int r = sd_bus_message_read(m, "o", &path);
	if (r < 0) {
		sd_bus_error_set_const(ret_error, "net.connman.iwd.Agent.Error.Failed", "bad RequestPassphrase args");
		return r;
	}

	char passphrase[256] = {0};
	if (!ctx->cb || !ctx->cb(ctx->ssid, passphrase, sizeof(passphrase), ctx->userdata)) {
		sd_bus_error_set_const(ret_error, "net.connman.iwd.Agent.Error.Canceled", "cancelled by user");
		return -ECANCELED;
	}

	sd_bus_message *reply = NULL;
	r = sd_bus_message_new_method_return(m, &reply);
	if (r < 0) {
		return r;
	}
	sd_bus_message_append(reply, "s", passphrase);
	r = sd_bus_send(NULL, reply, NULL);
	sd_bus_message_unref(reply);
	memset(passphrase, 0, sizeof(passphrase));
	return r;
}

static const sd_bus_vtable agent_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("RequestPassphrase", "o", "s", agent_request_passphrase, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END
};

bool syn_iwd_connect(sd_bus *bus, const char *network_path, syn_iwd_password_cb cb, void *userdata, char *err, size_t err_len) {
	agent_context ctx = {.cb = cb, .userdata = userdata};

	/* Connect()'s failure/cancel paths (and RequestPassphrase itself) only
	 * ever get the object path, not the SSID — fetch Name once up front so
	 * the callback can show the user something readable. */
	{
		char *name_reply = NULL;
		int nr = sd_bus_get_property_string(bus, IWD_SERVICE, network_path, "net.connman.iwd.Network",
		                                     "Name", NULL, &name_reply);
		if (nr >= 0 && name_reply) {
			snprintf(ctx.ssid, sizeof(ctx.ssid), "%s", name_reply);
			free(name_reply);
		}
	}

	sd_bus_slot *slot = NULL;
	int r = sd_bus_add_object_vtable(bus, &slot, AGENT_PATH, AGENT_INTERFACE, agent_vtable, &ctx);
	if (r < 0) {
		set_err(err, err_len, "Could not export local D-Bus agent: %s", strerror(-r));
		return false;
	}

	sd_bus_error reg_error = SD_BUS_ERROR_NULL;
	r = sd_bus_call_method(bus, IWD_SERVICE, "/net/connman/iwd", "net.connman.iwd.AgentManager",
	                       "RegisterAgent", &reg_error, NULL, "o", AGENT_PATH);
	if (r < 0) {
		set_err(err, err_len, "RegisterAgent failed: %s", reg_error.message ? reg_error.message : strerror(-r));
		sd_bus_error_free(&reg_error);
		sd_bus_slot_unref(slot);
		return false;
	}

	sd_bus_error connect_error = SD_BUS_ERROR_NULL;
	r = sd_bus_call_method(bus, IWD_SERVICE, network_path, "net.connman.iwd.Network",
	                       "Connect", &connect_error, NULL, "");
	bool ok = (r >= 0);
	if (!ok) {
		set_err(err, err_len, "%s", connect_error.message ? connect_error.message : strerror(-r));
	}
	sd_bus_error_free(&connect_error);

	sd_bus_error unreg_error = SD_BUS_ERROR_NULL;
	sd_bus_call_method(bus, IWD_SERVICE, "/net/connman/iwd", "net.connman.iwd.AgentManager",
	                   "UnregisterAgent", &unreg_error, NULL, "o", AGENT_PATH);
	sd_bus_error_free(&unreg_error);
	sd_bus_slot_unref(slot);

	return ok;
}

int syn_iwd_signal_bars(int16_t signal_strength) {
	/* signal_strength is dBm * 100 (e.g. -6200 == -62.00 dBm). Same rough
	 * thresholds iwctl's own asterisk column and NetworkManager use. */
	int dbm = signal_strength / 100;
	if (dbm >= -50) return 4;
	if (dbm >= -60) return 3;
	if (dbm >= -70) return 2;
	if (dbm >= -80) return 1;
	return 0;
}

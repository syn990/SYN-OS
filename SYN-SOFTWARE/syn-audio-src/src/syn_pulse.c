/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-AUDIO (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_pulse.h"

#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* Plain single-threaded pa_mainloop, iterated synchronously by whichever
 * function needs a reply — no pa_threaded_mainloop here. This process is
 * a one-shot CLI call or a keypress-driven TUI loop, never concurrent
 * work, so a background mainloop thread (and its lock/condvar pairs on
 * every single call) was pure overhead: measured ~25ms per connect vs.
 * ~3ms for a plain pa_mainloop doing the same handshake, an 8x difference
 * that was clearly visible on every pipe-menu render and waybar click. */
struct syn_pulse {
	pa_mainloop *loop;
	pa_mainloop_api *api;
	pa_context *ctx;
};

static void set_err(char *err, size_t err_len, const char *fmt, ...) {
	if (!err || err_len == 0) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, err_len, fmt, ap);
	va_end(ap);
}

/* Runs pa_mainloop_iterate() until *done_flag is set — every op_wait-style
 * callback below sets its own done flag directly, so callers just point
 * this at that flag instead of going through a signal/wait handshake. */
static void iterate_until(pa_mainloop *loop, const bool *done_flag) {
	while (!*done_flag) {
		pa_mainloop_iterate(loop, 1, NULL);
	}
}

typedef struct {
	bool done;
	int success;
} op_wait;

static void op_wait_init(op_wait *w) {
	w->done = false;
	w->success = 0;
}

static void op_wait_finish(op_wait *w, int success) {
	w->success = success;
	w->done = true;
}

/* Blocks (iterating the mainloop) until `w` is marked done, then releases
 * the operation. */
static void op_wait_block(pa_mainloop *loop, op_wait *w, pa_operation *o) {
	if (!o) {
		w->done = true;
		w->success = 0;
		return;
	}
	iterate_until(loop, &w->done);
	pa_operation_unref(o);
}

syn_pulse *syn_pulse_open(char *err, size_t err_len) {
	syn_pulse *p = calloc(1, sizeof(*p));
	if (!p) {
		set_err(err, err_len, "out of memory");
		return NULL;
	}

	p->loop = pa_mainloop_new();
	if (!p->loop) {
		set_err(err, err_len, "failed to create mainloop");
		free(p);
		return NULL;
	}
	p->api = pa_mainloop_get_api(p->loop);

	p->ctx = pa_context_new(p->api, "syn-audio");
	if (!p->ctx) {
		set_err(err, err_len, "failed to create context");
		pa_mainloop_free(p->loop);
		free(p);
		return NULL;
	}

	if (pa_context_connect(p->ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		set_err(err, err_len, "failed to connect: %s", pa_strerror(pa_context_errno(p->ctx)));
		pa_context_unref(p->ctx);
		pa_mainloop_free(p->loop);
		free(p);
		return NULL;
	}

	pa_context_state_t state;
	while ((state = pa_context_get_state(p->ctx)) != PA_CONTEXT_READY) {
		if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
			set_err(err, err_len, "connection failed (is pipewire-pulse/pulseaudio running?)");
			pa_context_unref(p->ctx);
			pa_mainloop_free(p->loop);
			free(p);
			return NULL;
		}
		pa_mainloop_iterate(p->loop, 1, NULL);
	}

	return p;
}

void syn_pulse_close(syn_pulse *p) {
	if (!p) return;
	pa_context_disconnect(p->ctx);
	pa_context_unref(p->ctx);
	pa_mainloop_free(p->loop);
	free(p);
}

static int volume_to_pct(const pa_cvolume *v) {
	return (int)((pa_cvolume_avg(v) * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
}

/* ---- default sink/source name lookup ---------------------------------- */

typedef struct {
	op_wait w;
	char sink[256];
	char source[256];
} server_info_ctx;

static void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
	(void)c;
	server_info_ctx *sc = (server_info_ctx *)userdata;
	snprintf(sc->sink, sizeof(sc->sink), "%s", i->default_sink_name ? i->default_sink_name : "");
	snprintf(sc->source, sizeof(sc->source), "%s", i->default_source_name ? i->default_source_name : "");
	op_wait_finish(&sc->w, 1);
}

static bool fetch_default_names(syn_pulse *p, char *default_sink, size_t sink_len,
		char *default_source, size_t source_len, char *err, size_t err_len) {
	server_info_ctx sc;
	op_wait_init(&sc.w);
	sc.sink[0] = '\0';
	sc.source[0] = '\0';

	pa_operation *o = pa_context_get_server_info(p->ctx, server_info_cb, &sc);
	op_wait_block(p->loop, &sc.w, o);

	if (!sc.w.success) {
		set_err(err, err_len, "failed to query server info: %s", pa_strerror(pa_context_errno(p->ctx)));
		return false;
	}
	if (default_sink) snprintf(default_sink, sink_len, "%s", sc.sink);
	if (default_source) snprintf(default_source, source_len, "%s", sc.source);
	return true;
}

/* ---- listing ---------------------------------------------------------- */

typedef struct {
	op_wait w;
	syn_pulse_device *out;
	int cap;
	int count;
	const char *default_name;
} list_ctx;

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
	(void)c;
	list_ctx *lc = (list_ctx *)userdata;
	if (eol) {
		op_wait_finish(&lc->w, 1);
		return;
	}
	if (lc->count < lc->cap) {
		syn_pulse_device *d = &lc->out[lc->count];
		snprintf(d->name, sizeof(d->name), "%s", i->name);
		snprintf(d->description, sizeof(d->description), "%s", i->description ? i->description : i->name);
		d->index = i->index;
		d->muted = i->mute ? true : false;
		d->volume_pct = volume_to_pct(&i->volume);
		d->is_default = (strcmp(i->name, lc->default_name) == 0);
	}
	lc->count++;
}

static void source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
	(void)c;
	list_ctx *lc = (list_ctx *)userdata;
	if (eol) {
		op_wait_finish(&lc->w, 1);
		return;
	}
	/* Every sink has a paired ".monitor" source for loopback/"what you
	 * hear" recording — skip those so an "AUDIO INPUTS" list only shows
	 * real microphones/inputs. */
	if (i->monitor_of_sink != PA_INVALID_INDEX) {
		return;
	}
	if (lc->count < lc->cap) {
		syn_pulse_device *d = &lc->out[lc->count];
		snprintf(d->name, sizeof(d->name), "%s", i->name);
		snprintf(d->description, sizeof(d->description), "%s", i->description ? i->description : i->name);
		d->index = i->index;
		d->muted = i->mute ? true : false;
		d->volume_pct = volume_to_pct(&i->volume);
		d->is_default = (strcmp(i->name, lc->default_name) == 0);
	}
	lc->count++;
}

int syn_pulse_list_sinks(syn_pulse *p, syn_pulse_device *out, int out_cap, char *err, size_t err_len) {
	char default_sink[256];
	if (!fetch_default_names(p, default_sink, sizeof(default_sink), NULL, 0, err, err_len)) {
		return -1;
	}

	list_ctx lc;
	op_wait_init(&lc.w);
	lc.out = out;
	lc.cap = out_cap;
	lc.count = 0;
	lc.default_name = default_sink;

	pa_operation *o = pa_context_get_sink_info_list(p->ctx, sink_info_cb, &lc);
	op_wait_block(p->loop, &lc.w, o);

	if (!lc.w.success) {
		set_err(err, err_len, "failed to list sinks: %s", pa_strerror(pa_context_errno(p->ctx)));
		return -1;
	}
	return lc.count;
}

int syn_pulse_list_sources(syn_pulse *p, syn_pulse_device *out, int out_cap, char *err, size_t err_len) {
	char default_source[256];
	if (!fetch_default_names(p, NULL, 0, default_source, sizeof(default_source), err, err_len)) {
		return -1;
	}

	list_ctx lc;
	op_wait_init(&lc.w);
	lc.out = out;
	lc.cap = out_cap;
	lc.count = 0;
	lc.default_name = default_source;

	pa_operation *o = pa_context_get_source_info_list(p->ctx, source_info_cb, &lc);
	op_wait_block(p->loop, &lc.w, o);

	if (!lc.w.success) {
		set_err(err, err_len, "failed to list sources: %s", pa_strerror(pa_context_errno(p->ctx)));
		return -1;
	}
	return lc.count;
}

/* ---- generic success-only ops (set-default, mute, volume) ------------- */

static void success_cb(pa_context *c, int success, void *userdata) {
	(void)c;
	op_wait_finish((op_wait *)userdata, success);
}

bool syn_pulse_set_default_sink(syn_pulse *p, const char *name, char *err, size_t err_len) {
	op_wait w;
	op_wait_init(&w);
	pa_operation *o = pa_context_set_default_sink(p->ctx, name, success_cb, &w);
	op_wait_block(p->loop, &w, o);
	if (!w.success) {
		set_err(err, err_len, "failed to set default sink: %s", pa_strerror(pa_context_errno(p->ctx)));
	}
	return w.success != 0;
}

bool syn_pulse_set_default_source(syn_pulse *p, const char *name, char *err, size_t err_len) {
	op_wait w;
	op_wait_init(&w);
	pa_operation *o = pa_context_set_default_source(p->ctx, name, success_cb, &w);
	op_wait_block(p->loop, &w, o);
	if (!w.success) {
		set_err(err, err_len, "failed to set default source: %s", pa_strerror(pa_context_errno(p->ctx)));
	}
	return w.success != 0;
}

bool syn_pulse_set_sink_mute(syn_pulse *p, const char *name, bool mute, char *err, size_t err_len) {
	op_wait w;
	op_wait_init(&w);
	pa_operation *o = pa_context_set_sink_mute_by_name(p->ctx, name, mute ? 1 : 0, success_cb, &w);
	op_wait_block(p->loop, &w, o);
	if (!w.success) {
		set_err(err, err_len, "failed to set sink mute: %s", pa_strerror(pa_context_errno(p->ctx)));
	}
	return w.success != 0;
}

bool syn_pulse_set_source_mute(syn_pulse *p, const char *name, bool mute, char *err, size_t err_len) {
	op_wait w;
	op_wait_init(&w);
	pa_operation *o = pa_context_set_source_mute_by_name(p->ctx, name, mute ? 1 : 0, success_cb, &w);
	op_wait_block(p->loop, &w, o);
	if (!w.success) {
		set_err(err, err_len, "failed to set source mute: %s", pa_strerror(pa_context_errno(p->ctx)));
	}
	return w.success != 0;
}

bool syn_pulse_toggle_sink_mute(syn_pulse *p, const char *name, char *err, size_t err_len) {
	syn_pulse_device devices[64];
	int count = syn_pulse_list_sinks(p, devices, 64, err, err_len);
	if (count < 0) return false;
	for (int i = 0; i < count && i < 64; i++) {
		if (strcmp(devices[i].name, name) == 0) {
			return syn_pulse_set_sink_mute(p, name, !devices[i].muted, err, err_len);
		}
	}
	set_err(err, err_len, "sink not found: %s", name);
	return false;
}

bool syn_pulse_toggle_source_mute(syn_pulse *p, const char *name, char *err, size_t err_len) {
	syn_pulse_device devices[64];
	int count = syn_pulse_list_sources(p, devices, 64, err, err_len);
	if (count < 0) return false;
	for (int i = 0; i < count && i < 64; i++) {
		if (strcmp(devices[i].name, name) == 0) {
			return syn_pulse_set_source_mute(p, name, !devices[i].muted, err, err_len);
		}
	}
	set_err(err, err_len, "source not found: %s", name);
	return false;
}

static void pct_to_cvolume(pa_cvolume *out, uint8_t channels, int pct) {
	if (pct < 0) pct = 0;
	pa_volume_t vol = (pa_volume_t)((int64_t)PA_VOLUME_NORM * pct / 100);
	if (vol > PA_VOLUME_MAX) vol = PA_VOLUME_MAX;
	pa_cvolume_set(out, channels, vol);
}

/* Sink/source's real pa_cvolume (channel count + balance) is needed to
 * scale volume correctly rather than assume stereo — these two callbacks
 * grab it via a fresh by-name lookup rather than trusting a cached
 * syn_pulse_device (which only stores a flattened average percentage). */
typedef struct {
	op_wait w;
	pa_cvolume vol;
	bool found;
} vol_ctx;

static void sink_vol_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
	(void)c;
	vol_ctx *v = (vol_ctx *)userdata;
	if (eol) {
		op_wait_finish(&v->w, 1);
		return;
	}
	v->vol = i->volume;
	v->found = true;
}

static void source_vol_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata) {
	(void)c;
	vol_ctx *v = (vol_ctx *)userdata;
	if (eol) {
		op_wait_finish(&v->w, 1);
		return;
	}
	v->vol = i->volume;
	v->found = true;
}

bool syn_pulse_set_sink_volume(syn_pulse *p, const char *name, int pct, char *err, size_t err_len) {
	vol_ctx vc;
	op_wait_init(&vc.w);
	vc.found = false;

	pa_operation *o = pa_context_get_sink_info_by_name(p->ctx, name, sink_vol_cb, &vc);
	op_wait_block(p->loop, &vc.w, o);

	if (!vc.found) {
		set_err(err, err_len, "sink not found: %s", name);
		return false;
	}

	pa_cvolume newvol;
	pct_to_cvolume(&newvol, vc.vol.channels, pct);

	op_wait w;
	op_wait_init(&w);
	pa_operation *o2 = pa_context_set_sink_volume_by_name(p->ctx, name, &newvol, success_cb, &w);
	op_wait_block(p->loop, &w, o2);

	if (!w.success) {
		set_err(err, err_len, "failed to set sink volume: %s", pa_strerror(pa_context_errno(p->ctx)));
	}
	return w.success != 0;
}

bool syn_pulse_set_source_volume(syn_pulse *p, const char *name, int pct, char *err, size_t err_len) {
	vol_ctx vc;
	op_wait_init(&vc.w);
	vc.found = false;

	pa_operation *o = pa_context_get_source_info_by_name(p->ctx, name, source_vol_cb, &vc);
	op_wait_block(p->loop, &vc.w, o);

	if (!vc.found) {
		set_err(err, err_len, "source not found: %s", name);
		return false;
	}

	pa_cvolume newvol;
	pct_to_cvolume(&newvol, vc.vol.channels, pct);

	op_wait w;
	op_wait_init(&w);
	pa_operation *o2 = pa_context_set_source_volume_by_name(p->ctx, name, &newvol, success_cb, &w);
	op_wait_block(p->loop, &w, o2);

	if (!w.success) {
		set_err(err, err_len, "failed to set source volume: %s", pa_strerror(pa_context_errno(p->ctx)));
	}
	return w.success != 0;
}

bool syn_pulse_adjust_sink_volume(syn_pulse *p, const char *name, int delta_pct, char *err, size_t err_len) {
	syn_pulse_device devices[64];
	int count = syn_pulse_list_sinks(p, devices, 64, err, err_len);
	if (count < 0) return false;
	for (int i = 0; i < count && i < 64; i++) {
		if (strcmp(devices[i].name, name) == 0) {
			int newpct = devices[i].volume_pct + delta_pct;
			if (newpct < 0) newpct = 0;
			return syn_pulse_set_sink_volume(p, name, newpct, err, err_len);
		}
	}
	set_err(err, err_len, "sink not found: %s", name);
	return false;
}

bool syn_pulse_adjust_source_volume(syn_pulse *p, const char *name, int delta_pct, char *err, size_t err_len) {
	syn_pulse_device devices[64];
	int count = syn_pulse_list_sources(p, devices, 64, err, err_len);
	if (count < 0) return false;
	for (int i = 0; i < count && i < 64; i++) {
		if (strcmp(devices[i].name, name) == 0) {
			int newpct = devices[i].volume_pct + delta_pct;
			if (newpct < 0) newpct = 0;
			return syn_pulse_set_source_volume(p, name, newpct, err, err_len);
		}
	}
	set_err(err, err_len, "source not found: %s", name);
	return false;
}

/* ------------------------------------------------------------------------
 *   PulseAudio (libpulse) client: enumerates sinks/sources and drives
 *   set-default, mute/unmute, and volume changes directly against the
 *   pulse/pipewire-pulse server — no pactl subprocess, no text parsing.
 *   Every call here runs its own threaded mainloop iteration and blocks
 *   until the operation completes, so callers (CLI or TUI) never need to
 *   pump an event loop themselves.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-AUDIO (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_PULSE_H
#define SYN_PULSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	char name[256];        /* pactl-style sink/source name, e.g. "alsa_output.pci-0000_00_1f.3.analog-stereo" */
	char description[256]; /* human-readable, e.g. "Built-in Audio Analog Stereo" */
	uint32_t index;
	bool is_default;
	bool muted;
	int volume_pct;         /* average volume across channels, 0-100 (can exceed 100 if boosted) */
} syn_pulse_device;

/* Opaque handle wrapping a pa_threaded_mainloop + pa_context, connected and
 * ready by the time syn_pulse_open() returns. */
typedef struct syn_pulse syn_pulse;

/* Connects to the pulse/pipewire-pulse server. Returns NULL and fills *err
 * on failure (server not running, connection refused, etc). */
syn_pulse *syn_pulse_open(char *err, size_t err_len);
void syn_pulse_close(syn_pulse *p);

/* Fills *out (caller-allocated array of out_cap entries) with every sink
 * (playback device). Returns the real count (may exceed out_cap; excess
 * entries are silently dropped), or -1 on error. */
int syn_pulse_list_sinks(syn_pulse *p, syn_pulse_device *out, int out_cap, char *err, size_t err_len);

/* Same as syn_pulse_list_sinks() but for sources (recording devices). */
int syn_pulse_list_sources(syn_pulse *p, syn_pulse_device *out, int out_cap, char *err, size_t err_len);

bool syn_pulse_set_default_sink(syn_pulse *p, const char *name, char *err, size_t err_len);
bool syn_pulse_set_default_source(syn_pulse *p, const char *name, char *err, size_t err_len);

bool syn_pulse_set_sink_mute(syn_pulse *p, const char *name, bool mute, char *err, size_t err_len);
bool syn_pulse_set_source_mute(syn_pulse *p, const char *name, bool mute, char *err, size_t err_len);
bool syn_pulse_toggle_sink_mute(syn_pulse *p, const char *name, char *err, size_t err_len);
bool syn_pulse_toggle_source_mute(syn_pulse *p, const char *name, char *err, size_t err_len);

/* Sets absolute volume as a percentage (0-100; >100 allowed for boost,
 * clamped to PA_VOLUME_MAX). Applies the same percentage uniformly across
 * every channel — matches pactl's plain "set-sink-volume NAME N%" form. */
bool syn_pulse_set_sink_volume(syn_pulse *p, const char *name, int pct, char *err, size_t err_len);
bool syn_pulse_set_source_volume(syn_pulse *p, const char *name, int pct, char *err, size_t err_len);

/* Relative volume step in percentage points, positive or negative, clamped
 * to [0, 100-scaled PA_VOLUME_MAX]. Reads the device's current volume
 * first, so this is a single call rather than list-then-set for callers. */
bool syn_pulse_adjust_sink_volume(syn_pulse *p, const char *name, int delta_pct, char *err, size_t err_len);
bool syn_pulse_adjust_source_volume(syn_pulse *p, const char *name, int delta_pct, char *err, size_t err_len);

#endif

/* ------------------------------------------------------------------------
 *   ~/.config/syn-cast/config.json (or $SYN_CAST_CONFIG). Every key is
 *   optional except the server and a token or invite; README.md lists
 *   them all.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_CONFIG_H
#define SYN_CAST_CONFIG_H

#include <stdbool.h>

#define CFG_MAX_CHANNELS 16

typedef struct {
	char path[1024];
	char server[512];       /* wss://HOST:PORT/cast/v1 */
	char member[128];       /* the member id the server answered hello with */
	char token[2048];       /* the member token (sct1....), kept after an invite */
	char invite[256];       /* a one-time invite code; spent on the first hello */
	char device[40];        /* this session's device name (window) */
	char cert_path[1024];   /* the server's own certificate, pinned; "" trusts the system store */
	char channels[CFG_MAX_CHANNELS][128];
	int n_channels;         /* 0: every channel the member belongs to */

	bool ptt_toggle;        /* "ptt_mode": "toggle" (default "hold") */
	bool auto_play;
	bool noise_suppression;
	bool vox;
	double vox_threshold;   /* rnnoise voice probability, 0..1 */
	double mic_gain_db;
	double play_gain_db;
	int max_talk_seconds;
	bool notify;            /* notify-send for texts when unfocused */
	bool tones;             /* cues through syn-bar-core */

	int history_days;
	int history_mb;
	char font[256];
	int width, height;

	char data_dir[1024];    /* ~/.local/share/syn-cast */
} config;

/* Fills defaults, then reads the file. Returns 0, 1 when the file does not
 * exist (defaults only), -1 on a parse error (err says where). */
int config_load(config *c, char *err, int errn);
/* A server and a token or an invite: enough to say hello. */
bool config_has_account(const config *c);
/* Writes the account keys back (setup, and the token an invite earned),
 * mode 0600. A spent invite is removed. */
int config_save_account(const config *c);
const char *config_default_path(char *out, int cap);

#endif

/* ------------------------------------------------------------------------
 *   ~/.config/syn-cast/config.json (or $SYN_CAST_CONFIG). Every key is
 *   optional except the account; README.md lists them all.
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
	char server[512];       /* wss://zello.io/ws */
	char username[128];
	char password[256];
	char issuer[512];       /* developer issuer: mints auth_token with key */
	char key_path[1024];
	char auth_token[2048];  /* a fixed token instead of issuer + key */
	char channels[CFG_MAX_CHANNELS][128];
	int n_channels;

	bool ptt_toggle;        /* "ptt_mode": "toggle" (default "hold") */
	bool auto_play;
	bool noise_suppression;
	bool vox;
	double vox_threshold;   /* rnnoise voice probability, 0..1 */
	double mic_gain_db;
	double play_gain_db;
	int max_talk_seconds;
	bool transcriptions;
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
bool config_has_account(const config *c);
/* SYN-CASTER's Zello account (the "username" in ~/.config/syn-caster/crew/
 * config.json), or "" if there is none. Zello allows one session per
 * account: SYN-CAST on the crew's account would knock the crew off. */
const char *config_bot_username(char *out, int cap);
bool config_is_bot_account(const char *username);
/* Writes the account keys back (syn-cast setup), mode 0600. */
int config_save_account(const config *c);
const char *config_default_path(char *out, int cap);

#endif

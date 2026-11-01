/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_CONFIG_H
#define SYN_MAIL_CONFIG_H
#include <stddef.h>

enum auth_kind { AUTH_PASSWORD, AUTH_OAUTH };

typedef struct {
	char name[64];      /* short name: ionos, google */
	char address[128];  /* the mailbox, also the login */
	char display[128];  /* the From name, may be empty */
	char imap_host[128];
	int imap_port;      /* 993: TLS from the start */
	char smtp_host[128];
	int smtp_port;      /* 465: TLS from the start; 587: STARTTLS */
	enum auth_kind auth;
} Account;

typedef struct {
	Account *acc;
	int n;
} Config;

/* ~/.config/syn-mail/config.json, or $SYN_MAIL_CONFIG; a missing file is an empty config */
int config_load(Config *c, char *err, size_t errn);
/* written whole, mode 600, its folder 700 */
int config_save(const Config *c, char *err, size_t errn);
Account *config_find(Config *c, const char *name);
/* fills host, ports and sign-in for a preset: google, ionos; -1 if unknown */
int config_preset(Account *a, const char *preset);
void config_free(Config *c);
const char *config_path(void);
#endif

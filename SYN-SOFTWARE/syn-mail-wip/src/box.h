/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_BOX_H
#define SYN_MAIL_BOX_H
/* The mailbox model the command line and the window share (slice 2, G223): folders, a page of headers, one body.
 * Everything here reads with EXAMINE and BODY.PEEK, so looking never marks a message read. */
#include "config.h"
#include "imap.h"

#include <time.h>

typedef struct {
	char name[256]; /* for people: modified UTF-7 decoded to UTF-8 */
	char raw[256];  /* as the server names it, for EXAMINE */
	int noselect;
} Folder;

typedef struct {
	unsigned long uid;
	long seq;
	int seen;
	time_t when; /* 0 if the Date header did not parse */
	char from[160];
	char from_addr[128];
	char subject[256];
	char date[64];
	char msgid[256];
} Header;

/* a page of a folder, newest first */
typedef struct {
	Header *h;
	int n;
	long exists, unseen;
} Page;

/* open and sign in; NULL with err set */
Imap *box_connect(const Account *a, char *err, size_t errn);
/* every folder (LIST "" "*"), INBOX first; 0 on success */
int box_folders(Imap *im, Folder **out, int *n, char *err, size_t errn);
/* skip messages from the newest, then up to count of them, newest first */
int box_page(Imap *im, const char *raw, long skip, int count, Page *out, char *err, size_t errn);
void page_free(Page *p);
/* the whole message by UID */
int box_body(Imap *im, const char *raw, unsigned long uid, Buf *out, char *err, size_t errn);

/* modified UTF-7 (RFC 3501 5.1.3) to UTF-8 */
void box_mutf7(const char *in, char *out, size_t n);
/* "Sat, 31 Oct 2026 10:00:00 +0000" to a time, 0 if it does not parse */
time_t box_date(const char *s);
/* a short date for a list: "14:02" today, "31 Oct" this year, "31 Oct 2025" before */
void box_short_date(time_t t, time_t now, char *out, size_t n);
/* whitespace flattened to spaces and cut to max bytes on a UTF-8 boundary */
void box_one_line(Buf *b, size_t max);
#endif

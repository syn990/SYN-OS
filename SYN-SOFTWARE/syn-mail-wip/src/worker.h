/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_WORKER_H
#define SYN_MAIL_WORKER_H
/* One thread per account keeps its IMAP connection (reconnecting when the server drops it) and runs the window's
 * jobs in order, so the window never waits on the network. Results come back on a pipe the window polls. */
#include "box.h"
#include "config.h"

enum { JOB_FOLDERS, JOB_PAGE, JOB_BODY, JOB_SEND };

typedef struct Job {
	int kind, acc;
	long gen; /* the window's request number: a result for an older one is dropped */
	char folder[256];
	long skip;
	int count;
	unsigned long uid;
	char *to, *subject, *body, *reply_to; /* JOB_SEND, owned by the job */
	struct Job *next;
} Job;

typedef struct Result {
	int kind, acc, ok;
	long gen;
	char err[512];
	char folder[256];
	long skip;
	unsigned long uid;
	Folder *f;
	int nf;
	Page page;
	Buf body; /* the raw message (JOB_BODY) */
	char to[256];
	struct Result *next;
} Result;

/* starts a worker per account of c (c must outlive the workers); 0 on success */
int workers_start(const Config *c);
/* queues a copy of j (strings are taken over and freed by the worker) */
void workers_push(const Job *j);
/* everything finished since the last call, oldest first, or NULL */
Result *workers_take(void);
void result_free(Result *r);
/* readable when results are waiting; drain with workers_take */
int workers_fd(void);
void workers_stop(void);
#endif

/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "worker.h"
#include "msg.h"
#include "smtp.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	const Account *a;
	pthread_t th;
	Job *head, *tail;
	Imap *im;
	int started;
} Worker;

static struct {
	Worker *w;
	int n;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	int stop;
	Result *head, *tail;
	int pipe[2];
} W = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .pipe = {-1, -1}};

static void job_free(Job *j) {
	free(j->to);
	free(j->subject);
	if (j->body) {
		memset(j->body, 0, strlen(j->body));
	}
	free(j->body);
	free(j->reply_to);
	free(j);
}

void result_free(Result *r) {
	while (r) {
		Result *n = r->next;
		free(r->f);
		page_free(&r->page);
		buf_free(&r->body);
		free(r);
		r = n;
	}
}

static void post(Result *r) {
	pthread_mutex_lock(&W.mu);
	if (W.tail) W.tail->next = r;
	else W.head = r;
	W.tail = r;
	pthread_mutex_unlock(&W.mu);
	char b = 1;
	if (write(W.pipe[1], &b, 1) < 0) {
		/* the pipe is full: the window is already behind and will drain everything */
	}
}

/* a queued job made pointless by a newer one of the same kind (only reads; sends always run) */
static int superseded(const Job *j) {
	if (j->kind == JOB_SEND) return 0;
	for (const Job *k = j->next; k; k = k->next) {
		if (k->kind == j->kind && (j->kind != JOB_PAGE || k->skip == j->skip)) return 1;
	}
	return 0;
}

static int run_read(Worker *w, const Job *j, Result *r) {
	if (!w->im) {
		w->im = box_connect(w->a, r->err, sizeof r->err);
		if (!w->im) return -1;
	}
	switch (j->kind) {
	case JOB_FOLDERS: return box_folders(w->im, &r->f, &r->nf, r->err, sizeof r->err);
	case JOB_PAGE: return box_page(w->im, j->folder, j->skip, j->count, &r->page, r->err, sizeof r->err);
	case JOB_BODY: return box_body(w->im, j->folder, j->uid, &r->body, r->err, sizeof r->err);
	}
	return -1;
}

static void run(Worker *w, Job *j) {
	Result *r = calloc(1, sizeof *r);
	r->kind = j->kind;
	r->acc = j->acc;
	r->gen = j->gen;
	r->skip = j->skip;
	r->uid = j->uid;
	str_copy(r->folder, sizeof r->folder, j->folder);
	if (j->kind == JOB_SEND) {
		str_copy(r->to, sizeof r->to, j->to);
		char mid[160];
		msg_id(w->a->address, mid, sizeof mid);
		Buf m = {0};
		/* the window's mail goes as the same signed company letter as `syn-mail send` (G265) */
		MsgSign sign = {.name = *w->a->display ? w->a->display : w->a->address, .title = w->a->title,
			.company = LETTER_COMPANY, .address = w->a->address};
		if (msg_build_letter(w->a->display, w->a->address, j->to, j->subject, j->body, mid, j->reply_to, time(NULL),
			NULL, 0, &sign, &m) != 0) {
			str_copy(r->err, sizeof r->err, "the message could not be built");
		} else {
			r->ok = smtp_send(w->a, j->to, m.p, r->err, sizeof r->err) == 0;
		}
		buf_wipe(&m);
	} else {
		int rc = run_read(w, j, r);
		if (rc != 0 && w->im) {
			/* the server may have dropped an idle connection: once more on a fresh one */
			imap_close(w->im);
			w->im = NULL;
			free(r->f);
			r->f = NULL;
			page_free(&r->page);
			buf_free(&r->body);
			r->err[0] = 0;
			rc = run_read(w, j, r);
		}
		r->ok = rc == 0;
		if (!r->ok && w->im) {
			imap_close(w->im);
			w->im = NULL;
		}
	}
	post(r);
}

static void *loop(void *arg) {
	Worker *w = arg;
	for (;;) {
		pthread_mutex_lock(&W.mu);
		while (!W.stop && !w->head) pthread_cond_wait(&W.cv, &W.mu);
		if (W.stop) {
			pthread_mutex_unlock(&W.mu);
			break;
		}
		Job *j = w->head;
		w->head = j->next;
		if (!w->head) w->tail = NULL;
		int skip = superseded(j);
		pthread_mutex_unlock(&W.mu);
		j->next = NULL;
		if (!skip) run(w, j);
		job_free(j);
	}
	imap_close(w->im);
	w->im = NULL;
	return NULL;
}

int workers_start(const Config *c) {
	if (pipe2(W.pipe, O_CLOEXEC | O_NONBLOCK) != 0) return -1;
	W.n = c->n;
	W.w = calloc((size_t)(c->n ? c->n : 1), sizeof *W.w);
	for (int i = 0; i < c->n; i++) {
		W.w[i].a = &c->acc[i];
		if (pthread_create(&W.w[i].th, NULL, loop, &W.w[i]) != 0) return -1;
		W.w[i].started = 1;
	}
	return 0;
}

void workers_push(const Job *src) {
	if (src->acc < 0 || src->acc >= W.n) return;
	Job *j = malloc(sizeof *j);
	*j = *src;
	j->next = NULL;
	pthread_mutex_lock(&W.mu);
	Worker *w = &W.w[j->acc];
	if (w->tail) w->tail->next = j;
	else w->head = j;
	w->tail = j;
	pthread_cond_broadcast(&W.cv);
	pthread_mutex_unlock(&W.mu);
}

Result *workers_take(void) {
	char b[256];
	while (read(W.pipe[0], b, sizeof b) > 0) {
	}
	pthread_mutex_lock(&W.mu);
	Result *r = W.head;
	W.head = W.tail = NULL;
	pthread_mutex_unlock(&W.mu);
	return r;
}

int workers_fd(void) {
	return W.pipe[0];
}

void workers_stop(void) {
	pthread_mutex_lock(&W.mu);
	W.stop = 1;
	pthread_cond_broadcast(&W.cv);
	pthread_mutex_unlock(&W.mu);
	int left = 0;
	for (int i = 0; i < W.n; i++) {
		if (W.w[i].started) {
			/* a worker deep in a slow server call is left to the process exit */
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 2;
			if (pthread_timedjoin_np(W.w[i].th, NULL, &ts) != 0) {
				pthread_detach(W.w[i].th);
				left = 1;
				continue;
			}
		}
		for (Job *j = W.w[i].head; j;) {
			Job *n = j->next;
			job_free(j);
			j = n;
		}
	}
	if (left) {
		/* still in use by a detached worker until the process ends */
		return;
	}
	free(W.w);
	W.w = NULL;
	W.n = 0;
	result_free(W.head);
	W.head = W.tail = NULL;
	if (W.pipe[0] >= 0) close(W.pipe[0]);
	if (W.pipe[1] >= 0) close(W.pipe[1]);
	W.pipe[0] = W.pipe[1] = -1;
}

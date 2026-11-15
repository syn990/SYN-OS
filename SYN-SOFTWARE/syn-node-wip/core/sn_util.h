/* ------------------------------------------------------------------------
 *   Small shared helpers for libsynnode: hex, little-endian packing,
 *   growable byte buffers, files written atomically with a fixed mode,
 *   the state directory, time, and logging. No crypto here.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SN_UTIL_H
#define SN_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SN_DEFAULT_PORT 47900
#define SN_VERSION 1

/* ---- hex ---- */
void sn_hex(const uint8_t *in, size_t len, char *out); /* out: 2*len+1 */
bool sn_unhex(const char *in, uint8_t *out, size_t len);

/* ---- little-endian packing ---- */
void sn_put16(uint8_t *p, uint16_t v);
void sn_put32(uint8_t *p, uint32_t v);
void sn_put64(uint8_t *p, uint64_t v);
uint16_t sn_get16(const uint8_t *p);
uint32_t sn_get32(const uint8_t *p);
uint64_t sn_get64(const uint8_t *p);

/* ---- growable buffer ---- */
typedef struct {
	uint8_t *data;
	size_t len;
	size_t cap;
} sn_buf;

void sn_buf_free(sn_buf *b);
bool sn_buf_append(sn_buf *b, const void *data, size_t len);
void sn_buf_consume(sn_buf *b, size_t n); /* drop n bytes from the front */

/* ---- files ---- */
/* The state directory: $SN_HOME, else $XDG_DATA_HOME/syn-node, else
 * ~/.local/share/syn-node. Created 0700 on first use. */
bool sn_state_dir(char *out, size_t out_size);
bool sn_state_path(const char *name, char *out, size_t out_size);
/* Writes to name.tmp then renames, so a crash never leaves half a file. */
bool sn_write_file(const char *path, const void *data, size_t len, unsigned mode);
/* Reads a whole file (max cap bytes). Caller frees *out. */
bool sn_read_file(const char *path, uint8_t **out, size_t *len, size_t cap);

/* The local control socket: $SN_SOCK, else $XDG_RUNTIME_DIR/syn-node.sock. */
void sn_ctl_path(char *out, size_t out_size);
/* The node's listen port: $SN_PORT, else 47900. */
uint16_t sn_port(void);

/* ---- time, logging ---- */
uint64_t sn_now_ms(void); /* monotonic */
uint64_t sn_unix_time(void);
void sn_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Copies a node name into a fixed 32-byte field: printable ASCII only,
 * '|' and spaces become '-', NUL-padded. */
void sn_name_field(const char *in, char out[32]);

#endif

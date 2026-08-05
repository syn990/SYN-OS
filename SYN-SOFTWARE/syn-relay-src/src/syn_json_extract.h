/* ------------------------------------------------------------------------
 *   Extremely minimal JSON field extraction — not a parser, just enough
 *   to pull known top-level numeric/string fields out of the stats
 *   server role's fixed-shape STATS/LIST_APPS replies. No object
 *   nesting beyond one level deep is handled generically;
 *   "sunshine":{"running":..} is special-cased since it's the only
 *   nested field the client needs. Reasonable given these replies are
 *   always this repo's own fixed, known shape — a full JSON library
 *   would be pure overhead.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_JSON_EXTRACT_H
#define SYN_JSON_EXTRACT_H

#include <stdbool.h>
#include <stddef.h>

/* Finds "key":value (numeric, or `null`) in `json` and parses it as a
 * double into *out. Returns false if the key is absent or its value is
 * `null`. */
bool syn_json_number(const char *json, const char *key, double *out);

/* Finds "key":"value" (string) in `json` and copies the unescaped-enough
 * (quotes/backslashes only) value into `out`. Returns false if absent. */
bool syn_json_string(const char *json, const char *key, char *out, size_t out_size);

/* Finds "key":true|false in `json`. Returns the parsed bool via *out,
 * false return value means the key was absent (out left unchanged). */
bool syn_json_bool(const char *json, const char *key, bool *out);

#endif

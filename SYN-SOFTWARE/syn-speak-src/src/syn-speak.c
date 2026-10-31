/* syn-speak: Piper voices in, 16 kHz mono signed 16-bit PCM out. Local and free: sherpa-onnx running the Piper VITS
 * models that SYN-TREK uses, its sound layer with the game taken out. No API key, no budget. docs/SYN-SPEAK.md has the
 * vocabulary, the VOICE manifest and the wire format.
 *
 * Three ways to run it:
 *   syn-speak <speaker-id> <text...>     one line in vctk, the default voice bank, framed PCM to stdout, then exit
 *   syn-speak --serve                    serves requests on stdin: each line "<voice>\t<speaker-id>\t<text>", or
 *                                        "<voice>\t<speaker-id>\t<text>\t<length>" for a slower or faster pace, is
 *                                        answered with framed audio, a 4-byte little-endian length then PCM, ending in
 *                                        a zero-length frame. Audio goes out as each sentence is ready. A line that
 *                                        cannot be spoken gets the zero-length frame alone, and the reason on stderr.
 *                                        <length> scales the voice's own pace (its VOICE inference length): 1.08 is 8%
 *                                        slower, 0.85 is 15% faster, LENGTH_MIN to LENGTH_MAX; the text has no tab.
 *   syn-speak --list                     one TSV row per voice in the store, without loading any model
 * Voices: the voice store holds one folder per voice, named by its id (vctk, libritts_r, data, m141), and the folder's
 * VOICE manifest says what it is, a voice bank (many speakers) or an own voice (one), and which files to load. A folder
 * without VOICE is still read the old way (MODELS below, else <id>/<id>.onnx). A voice loads on its first line and
 * stays loaded, except that only SYN_SPEAK_MAX_OWN own voices are kept: the least recently used goes first.
 * env: SYN_SPEAK_VOICES (the voice store, default $XDG_DATA_HOME/syn-speak/voices, so ~/.local/share/syn-speak/voices),
 *      SYN_SPEAK_MAX_OWN (default 6)
 * SYN-OS carries a byte-identical copy at SYN-SOFTWARE/syn-speak-src/src/syn-speak.c (its package for the ISO, with
 * sherpa-onnx fetched by CMake): a change here is copied there, never edited there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "sherpa-onnx/c-api/c-api.h"

#define OUT_RATE 16000

/* output bytes collected for the current chunk, sent as one frame */
static unsigned char *out_buf = NULL;
static size_t out_len = 0, out_cap = 0;

static void out_put16(int16_t s) {
    if (out_len + 2 > out_cap) {
        out_cap = (out_len + 2) * 2 + 4096;
        out_buf = realloc(out_buf, out_cap);
    }
    out_buf[out_len++] = (unsigned char)(s & 0xff);
    out_buf[out_len++] = (unsigned char)((s >> 8) & 0xff);
}

static void write_frame(const void *data, size_t n) {
    unsigned char h[4] = { (unsigned char)n, (unsigned char)(n >> 8), (unsigned char)(n >> 16), (unsigned char)(n >> 24) };
    fwrite(h, 1, 4, stdout);
    if (n) fwrite(data, 1, n, stdout);
    fflush(stdout);
}

/* Streaming resampler: the model's rate in, OUT_RATE out, continuous across chunks. */
static float *in_buf = NULL;
static long in_len = 0, in_cap = 0, base = 0, next_out = 0;
static double step_in = 1.0;

static void resample_reset(double rate_in) {
    in_len = 0; base = 0; next_out = 0;
    step_in = rate_in / OUT_RATE;
}

static void resample_push(const float *samples, int32_t n) {
    if (in_len + n > in_cap) {
        in_cap = (in_len + n) * 2 + 4096;
        in_buf = realloc(in_buf, sizeof(float) * (size_t)in_cap);
    }
    memcpy(in_buf + in_len, samples, sizeof(float) * (size_t)n);
    in_len += n;
    for (;;) {  /* every output sample whose two input neighbours have arrived */
        double pos = next_out * step_in;
        long i = (long)pos;
        if (i + 1 >= base + in_len) break;
        double frac = pos - i;
        double v = in_buf[i - base] * (1.0 - frac) + in_buf[i + 1 - base] * frac;
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        out_put16((int16_t)(v * 32767.0));
        next_out++;
    }
    long keep_from = (long)(next_out * step_in);
    if (keep_from > base) {
        long drop = keep_from - base;
        memmove(in_buf, in_buf + drop, sizeof(float) * (size_t)(in_len - drop));
        in_len -= drop;
        base += drop;
    }
}

/* Called by sherpa as each piece of audio is ready: send it on at once. */
static int32_t on_chunk(const float *samples, int32_t n, float progress, void *arg) {
    (void)progress; (void)arg;
    out_len = 0;
    resample_push(samples, n);
    if (out_len) write_frame(out_buf, out_len);
    return 1;
}

/* The old names, for a folder without VOICE only: name -> model file in <voices>/<dir>/. VOICE replaces them. */
typedef struct { const char *name, *dir, *onnx; } VoiceModel;
static const VoiceModel MODELS[] = {
    { "vctk", "vctk", "en_GB-vctk-medium.onnx" },                    /* voice bank: 109 British speakers, the default */
    { "libritts", "libritts_r", "en_US-libritts_r-medium.onnx" },    /* voice bank: 904 LibriTTS-R speakers, old name */
    { "libritts_r", "libritts_r", "en_US-libritts_r-medium.onnx" },  /* the same bank by its folder name */
    { "data", "data", "data.onnx" },                                 /* own voice: Commander Data (SYN-TREK) */
};
#define NMODELS ((int)(sizeof MODELS / sizeof MODELS[0]))

#define MAX_VOICES 64   /* folders in the voice store */
#define MAX_OWN 32      /* the most own voices SYN_SPEAK_MAX_OWN may keep loaded */
#define RETRY_S 60      /* a voice that would not load is not tried again for this long */
#define KV_MAX 64       /* keys in one VOICE */
#define LENGTH_MIN 0.5  /* a line's length field: twice as fast at most */
#define LENGTH_MAX 2.0  /* and half as fast */

/* One folder of the voice store: what its VOICE says, and its model once loaded. */
typedef struct {
    char id[33];                  /* the folder name, which is the voice id */
    int present;                  /* 0 once the folder is gone, or when it holds no voice */
    int manifest;                 /* 1: described by VOICE; 0: read the old way */
    char refused[256];            /* why its VOICE is refused; empty when the voice may be used */
    struct stat vst;              /* VOICE as last read, to notice an edit */
    char set[16], speakers_s[16], display[128], subject[32], consent[32], allow[64], characters[256], aliases[256];
    int speakers;                 /* 0 when no VOICE says */
    char onnx[256], tokens[256], espeak_data[256];
    float noise_scale, noise_w, length_scale;
    const SherpaOnnxOfflineTts *tts;
    int own;                      /* a loaded one-speaker model, counted against SYN_SPEAK_MAX_OWN */
    time_t failed_at;             /* the last load that failed, 0 for none */
    char failed[1200];            /* and why */
    unsigned long used;           /* when it was last asked for, to unload the least recently used own voice */
} Voice;
static Voice voices[MAX_VOICES];
static int nvoices;
static unsigned long ticks;

static const char *voices_root(void) {
    const char *dir = getenv("SYN_SPEAK_VOICES");
    if (dir && *dir) return dir;
    /* the store: $XDG_DATA_HOME/syn-speak/voices, so ~/.local/share/syn-speak/voices */
    static char path[4096];
    const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (data && *data) snprintf(path, sizeof path, "%s/syn-speak/voices", data);
    else if (home && *home) snprintf(path, sizeof path, "%s/.local/share/syn-speak/voices", home);
    else return "vendor/voices";
    return path;
}

/* How many own voices stay loaded: SYN_SPEAK_MAX_OWN, 1 to MAX_OWN, default 6. */
static int max_own(void) {
    static int n;
    if (n) return n;
    n = 6;
    const char *e = getenv("SYN_SPEAK_MAX_OWN");
    if (e && *e) {
        char *end;
        long v = strtol(e, &end, 10);
        if (*end || v < 1 || v > MAX_OWN) fprintf(stderr, "syn-speak: SYN_SPEAK_MAX_OWN %s is not 1 to %d; using 6\n", e, MAX_OWN);
        else n = (int)v;
    }
    return n;
}

/* A voice name: 1 to 32 of a-z, 0-9, _ and -, not starting with - (it is a folder name). */
static int own_name_ok(const char *name) {
    size_t n = strlen(name);
    if (n == 0 || n > 32 || name[0] == '-') return 0;
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return 0;
    return 1;
}

/* Is `word` one of the space-separated words of `list`? */
static int has_word(const char *list, const char *word) {
    size_t n = strlen(word);
    for (const char *p = list; n && *p; ) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == n && !strncmp(p, word, n)) return 1;
        p = e;
    }
    return 0;
}

/* The first reason a VOICE is refused is the one kept. */
static void refuse(Voice *v, const char *fmt, ...) {
    if (v->refused[0]) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v->refused, sizeof v->refused, fmt, ap);
    va_end(ap);
}

static const char *const KNOWN_KEYS[] = {
    "format", "id", "set", "engine", "onnx", "speakers", "allow", "census", "tier", "subject", "characters", "provenance",
    "consent", "display", "aliases", "config", "tokens", "espeak", "espeak_data", "rate", "quality", "inference",
    "licence", "redistribute", "source_detail", "installed", "corpus", "who", "handle", "species", "base", "trainer",
    "epoch", "checkpoint", "score", "delete_on_request",
};

typedef struct { char key[32]; char val[1024]; } Kv;

static const char *kv_get(const Kv *kv, int n, const char *key) {
    for (int i = 0; i < n; i++)
        if (!strcmp(kv[i].key, key)) return kv[i].val;
    return NULL;
}

/* Copy a key's value into a field of v; a value too long for it refuses the voice rather than being cut short. */
static void take(Voice *v, char *dst, size_t size, const Kv *kv, int n, const char *key) {
    const char *s = kv_get(kv, n, key);
    if (!s) return;
    size_t l = strlen(s);
    if (l >= size) refuse(v, "%s is longer than %zu characters", key, size - 1);
    else memcpy(dst, s, l + 1);
}
#define TAKE(field, key) take(v, v->field, sizeof v->field, kv, n, key)

/* Read <voices>/<id>/ into v (zeroed first): its VOICE, or the old way without one. v->present says whether the
 * folder holds a voice at all; v->refused why its VOICE may not be used. */
static void voice_read(const char *id, Voice *v) {
    memset(v, 0, sizeof *v);
    snprintf(v->id, sizeof v->id, "%s", id);
    snprintf(v->display, sizeof v->display, "%s", id);
    snprintf(v->tokens, sizeof v->tokens, "tokens.txt");
    snprintf(v->espeak_data, sizeof v->espeak_data, "../libritts_r/espeak-ng-data");  /* the eSpeak data every voice shares */
    v->noise_scale = 0.667f;
    v->noise_w = 0.8f;
    v->length_scale = 1.0f;

    char path[1024];
    snprintf(path, sizeof path, "%s/%s/VOICE", voices_root(), id);
    FILE *f = fopen(path, "r");
    if (!f && errno == ENOENT) {  /* no VOICE: a MODELS entry for this folder, else <id>.onnx */
        snprintf(v->onnx, sizeof v->onnx, "%s.onnx", id);
        for (int i = 0; i < NMODELS; i++) {
            if (strcmp(MODELS[i].dir, id) != 0) continue;
            snprintf(v->onnx, sizeof v->onnx, "%s", MODELS[i].onnx);
            if (strcmp(MODELS[i].name, id) != 0) {
                size_t l = strlen(v->aliases);
                snprintf(v->aliases + l, sizeof v->aliases - l, "%s%s", l ? " " : "", MODELS[i].name);
            }
        }
        snprintf(path, sizeof path, "%s/%s/%s", voices_root(), id, v->onnx);
        v->present = access(path, F_OK) == 0;
        return;
    }
    v->present = v->manifest = 1;
    if (!f) {
        refuse(v, "cannot read VOICE: %s", strerror(errno));
        return;
    }
    fstat(fileno(f), &v->vst);

    Kv *kv = calloc(KV_MAX, sizeof *kv);
    int n = 0, lineno = 0;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while (kv && (len = getline(&line, &cap, f)) >= 0) {
        lineno++;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t'))
            line[--len] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        char *k = p;
        while ((*p >= 'a' && *p <= 'z') || *p == '_') p++;
        if (p == k || (*p != ' ' && *p != '\t')) {
            refuse(v, "line %d is not \"key value\"", lineno);
            continue;
        }
        *p++ = 0;
        while (*p == ' ' || *p == '\t') p++;
        for (char *q = p; *q; q++)
            if (*q == '\t') *q = ' ';  /* a value never carries a tab into --list's columns */
        int known = 0;
        for (size_t i = 0; i < sizeof KNOWN_KEYS / sizeof KNOWN_KEYS[0]; i++)
            if (!strcmp(KNOWN_KEYS[i], k)) known = 1;
        if (!known) {  /* a newer manifest still loads */
            fprintf(stderr, "syn-speak: VOICE of %s: unknown key %s, ignored\n", id, k);
            continue;
        }
        if (kv_get(kv, n, k)) {
            refuse(v, "duplicate key %s", k);
            continue;
        }
        if (n == KV_MAX) {
            refuse(v, "more than %d keys", KV_MAX);
            continue;
        }
        snprintf(kv[n].key, sizeof kv[n].key, "%s", k);
        snprintf(kv[n].val, sizeof kv[n].val, "%s", p);
        n++;
    }
    free(line);
    fclose(f);
    if (!kv) {
        refuse(v, "out of memory");
        return;
    }

    TAKE(set, "set");
    TAKE(speakers_s, "speakers");
    TAKE(display, "display");
    TAKE(subject, "subject");
    TAKE(allow, "allow");
    TAKE(characters, "characters");
    TAKE(aliases, "aliases");
    TAKE(onnx, "onnx");
    TAKE(tokens, "tokens");
    TAKE(espeak_data, "espeak_data");
#undef TAKE
    const char *consent = kv_get(kv, n, "consent");  /* "pending 2026-10-26 a note": the state is the first word */
    if (consent) snprintf(v->consent, sizeof v->consent, "%.*s", (int)strcspn(consent, " "), consent);

    /* Withdrawn consent stops every use, whatever else the manifest says. */
    if (!strcmp(v->consent, "withdrawn")) refuse(v, "consent withdrawn");
    const char *fmt = kv_get(kv, n, "format");
    if (fmt) {
        char *end;
        long fv = strtol(fmt, &end, 10);
        if (*end || fv < 1) refuse(v, "format %s is not a number", fmt);
        else if (fv > 1) refuse(v, "format %ld is newer than this syn-speak reads (1)", fv);
    }
    static const char *const REQUIRED[] = { "format", "id", "set", "engine", "onnx", "speakers", "allow" };
    for (size_t i = 0; i < sizeof REQUIRED / sizeof REQUIRED[0]; i++)
        if (!kv_get(kv, n, REQUIRED[i])) refuse(v, "missing key %s", REQUIRED[i]);
    const char *vid = kv_get(kv, n, "id");
    if (vid && strcmp(vid, id) != 0) refuse(v, "id %s is not the folder name %s", vid, id);
    if (v->set[0] && !has_word("bank own", v->set)) refuse(v, "unknown set %s", v->set);
    const char *engine = kv_get(kv, n, "engine");
    if (engine && strcmp(engine, "piper") != 0) refuse(v, "unknown engine %s", engine);
    if (v->speakers_s[0]) {
        char *end;
        long sp = strtol(v->speakers_s, &end, 10);
        if (*end || sp < 1 || sp > 100000) refuse(v, "speakers %s is not a number", v->speakers_s);
        else v->speakers = (int)sp;
    }
    if (!strcmp(v->set, "bank") && v->speakers == 1) refuse(v, "set bank needs more than 1 speaker, and speakers is 1");
    if (!strcmp(v->set, "own") && v->speakers > 1) refuse(v, "set own needs exactly 1 speaker, and speakers is %d", v->speakers);
    static const char *const BANK_KEYS[] = { "census", "tier" };
    static const char *const OWN_KEYS[] = { "subject", "characters", "provenance", "consent" };
    if (!strcmp(v->set, "bank"))
        for (size_t i = 0; i < sizeof BANK_KEYS / sizeof BANK_KEYS[0]; i++)
            if (!kv_get(kv, n, BANK_KEYS[i])) refuse(v, "missing key %s", BANK_KEYS[i]);
    if (!strcmp(v->set, "own"))
        for (size_t i = 0; i < sizeof OWN_KEYS / sizeof OWN_KEYS[0]; i++)
            if (!kv_get(kv, n, OWN_KEYS[i])) refuse(v, "missing key %s", OWN_KEYS[i]);
    if (v->subject[0] && !has_word("person character", v->subject)) refuse(v, "unknown subject %s", v->subject);
    const char *prov = kv_get(kv, n, "provenance");
    if (prov && !has_word("recordings elevenlabs-synth elevenlabs-history upstream", prov))
        refuse(v, "unknown provenance %s", prov);
    if (consent && !has_word("given pending withdrawn n/a", v->consent)) refuse(v, "unknown consent %s", v->consent);
    const char *inf = kv_get(kv, n, "inference");
    if (inf) {
        int used = 0;
        if (sscanf(inf, "%f %f %f %n", &v->noise_scale, &v->noise_w, &v->length_scale, &used) != 3 || inf[used])
            refuse(v, "inference %s is not three numbers", inf);
    }

    /* Labels and lists that only warn: SYN-CASTER and claude-voice enforce allow and characters. */
    const char *tier = kv_get(kv, n, "tier");
    if (tier && !has_word("FINE LIGHT", tier)) fprintf(stderr, "syn-speak: VOICE of %s: unknown tier %s\n", id, tier);
    for (const char *p = v->allow; *p; ) {
        while (*p == ' ') p++;
        size_t wl = strcspn(p, " ");
        char w[64];
        snprintf(w, sizeof w, "%.*s", (int)wl, p);
        if (wl && !has_word("channel desk trek", w)) fprintf(stderr, "syn-speak: VOICE of %s: unknown allow %s\n", id, w);
        p += wl;
    }
    free(kv);
}

static void voice_unload(Voice *v) {
    if (v->tts) SherpaOnnxDestroyOfflineTts(v->tts);
    v->tts = NULL;
    v->own = 0;
}

/* Read v's folder again: its model is unloaded and a failed load forgotten. */
static void voice_refresh(Voice *v) {
    if (v->tts) fprintf(stderr, "syn-speak: voice %s changed on disk; unloaded\n", v->id);
    voice_unload(v);
    Voice fresh;
    voice_read(v->id, &fresh);
    *v = fresh;
}

static int same_file(const struct stat *a, const struct stat *b) {
    return a->st_ino == b->st_ino && a->st_size == b->st_size &&
           a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec;
}

/* Read v again if its VOICE was added, edited or removed since (a folder that held no voice is always looked at again). */
static void voice_check(Voice *v) {
    char path[1024];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s/VOICE", voices_root(), v->id);
    int has = stat(path, &st) == 0;
    if (v->present && has == v->manifest && (!has || same_file(&st, &v->vst))) return;
    voice_refresh(v);
}

/* The voice a name means: its id first, then an alias. */
static Voice *voice_named(const char *name) {
    for (int i = 0; i < nvoices; i++)
        if (voices[i].present && !strcmp(voices[i].id, name)) return &voices[i];
    for (int i = 0; i < nvoices; i++)
        if (voices[i].present && has_word(voices[i].aliases, name)) return &voices[i];
    return NULL;
}

/* Build the index of the voice store, or bring it up to date: new folders are read, edited ones read again, and the
 * ones gone are dropped with their models. */
static void voices_scan(void) {
    struct dirent **list = NULL;
    int n = scandir(voices_root(), &list, NULL, alphasort);
    if (n < 0) {
        fprintf(stderr, "syn-speak: cannot read the voice store %s: %s\n", voices_root(), strerror(errno));
        n = 0;
    }
    char seen[MAX_VOICES] = { 0 };
    for (int i = 0; i < n; i++) {
        const char *name = list[i]->d_name;
        char path[1024];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", voices_root(), name);
        if (own_name_ok(name) && name[0] != '_' && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            int j = 0;
            while (j < nvoices && strcmp(voices[j].id, name) != 0) j++;
            if (j < nvoices) voice_check(&voices[j]);
            else if (nvoices < MAX_VOICES) voice_read(name, &voices[nvoices++]);
            else fprintf(stderr, "syn-speak: more than %d folders in %s; %s is left out\n", MAX_VOICES, voices_root(), name);
            if (j < MAX_VOICES) seen[j] = 1;
        }
        free(list[i]);
    }
    free(list);
    for (int i = 0; i < nvoices; i++)
        if (!seen[i] && voices[i].present) {
            voice_unload(&voices[i]);
            voices[i].present = 0;
        }
    for (int i = 0; i < nvoices; i++) {  /* an alias that is already another voice's name is ignored, and says so */
        for (const char *p = voices[i].aliases; voices[i].present && *p; ) {
            while (*p == ' ') p++;
            size_t wl = strcspn(p, " ");
            char w[64];
            snprintf(w, sizeof w, "%.*s", (int)wl, p);
            Voice *o = wl ? voice_named(w) : NULL;
            if (o && o != &voices[i]) fprintf(stderr, "syn-speak: alias %s of %s is already a name of %s; ignored\n", w, voices[i].id, o->id);
            p += wl;
        }
    }
}

/* The voice for a requested name, its VOICE read again if edited. An unknown name scans the store once more, so a
 * voice installed since needs no restart. */
static Voice *find_voice(const char *name) {
    Voice *v = voice_named(name);
    if (v) voice_check(v);
    if (!v || !v->present || (strcmp(v->id, name) != 0 && !has_word(v->aliases, name))) {
        voices_scan();
        v = voice_named(name);
    }
    return v;
}

/* A file of v's: relative to its folder unless it is an absolute path. */
static void voice_path(char *out, size_t n, const Voice *v, const char *file) {
    if (file[0] == '/') snprintf(out, n, "%s", file);
    else snprintf(out, n, "%s/%s/%s", voices_root(), v->id, file);
}

/* Keep at most SYN_SPEAK_MAX_OWN own voices loaded: the least recently used others go. Voice banks always stay. */
static void own_cap(const Voice *keep) {
    for (;;) {
        int n = 0;
        Voice *lru = NULL;
        for (int i = 0; i < nvoices; i++) {
            Voice *v = &voices[i];
            if (!v->tts || !v->own) continue;
            n++;
            if (v != keep && (!lru || v->used < lru->used)) lru = v;
        }
        if (n <= max_own() || !lru) return;
        fprintf(stderr, "syn-speak: unloading own voice %s, the least recently used (SYN_SPEAK_MAX_OWN %d)\n", lru->id, max_own());
        voice_unload(lru);
    }
}

/* The model for a voice name (its id or an alias), loaded on first use, and its own length (VOICE's inference) into
 * *length. NULL, with the reason on stderr, when the name is unknown, its VOICE is refused, or the model would not load
 * (not tried again for RETRY_S seconds). */
static const SherpaOnnxOfflineTts *load_voice(const char *name, float *length) {
    if (!own_name_ok(name)) {
        fprintf(stderr, "syn-speak: unknown voice %s (not a voice name)\n", name);
        return NULL;
    }
    Voice *v = find_voice(name);
    if (!v) {
        fprintf(stderr, "syn-speak: unknown voice %s (no folder or alias of that name in %s)\n", name, voices_root());
        return NULL;
    }
    if (v->refused[0]) {
        fprintf(stderr, "syn-speak: voice %s refused: %s\n", v->id, v->refused);
        return NULL;
    }
    v->used = ++ticks;
    *length = v->length_scale;
    if (v->tts) return v->tts;
    time_t now = time(NULL);
    if (v->failed_at && now - v->failed_at < RETRY_S) {
        fprintf(stderr, "syn-speak: voice %s: %s (%ld s ago; tried again after %d s)\n", v->id, v->failed,
                (long)(now - v->failed_at), RETRY_S);
        return NULL;
    }

    char model[1024], tokens[1024], data[1024];
    voice_path(model, sizeof model, v, v->onnx);
    voice_path(tokens, sizeof tokens, v, v->tokens);
    voice_path(data, sizeof data, v, v->espeak_data);
    SherpaOnnxOfflineTtsConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.model.vits.model = model;
    cfg.model.vits.tokens = tokens;
    cfg.model.vits.data_dir = data;
    cfg.model.vits.noise_scale = v->noise_scale;
    cfg.model.vits.noise_scale_w = v->noise_w;
    cfg.model.vits.length_scale = 1.0f;  /* the voice's own length goes with each line (speak_line), so one model serves every pace */
    cfg.model.num_threads = 2;
    cfg.model.provider = "cpu";
    cfg.max_num_sentences = 1;  /* one sentence at a time: the first words come out sooner */
    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&cfg);
    int count = tts ? SherpaOnnxOfflineTtsNumSpeakers(tts) : 0;
    if (!tts) snprintf(v->failed, sizeof v->failed, "cannot load voice model %s", model);
    else if (v->speakers && count != v->speakers) {  /* the manifest must describe the model it names */
        SherpaOnnxDestroyOfflineTts(tts);
        tts = NULL;
        snprintf(v->failed, sizeof v->failed, "refused: the model has %d speakers, VOICE says %d", count, v->speakers);
    }
    if (!tts) {
        v->failed_at = now;
        fprintf(stderr, "syn-speak: voice %s: %s\n", v->id, v->failed);
        return NULL;
    }
    v->failed_at = 0;
    v->tts = tts;
    v->own = v->manifest ? !strcmp(v->set, "own") : count == 1;
    if (v->own) own_cap(v);
    return tts;
}

/* Speak one line at `length` times the voice's own length (1 for its own pace); audio frames go to stdout as they are
 * made, then the zero-length frame. Returns 0 on success. */
static int speak_line(const SherpaOnnxOfflineTts *tts, float own_length, long sid, const char *text, double length) {
    if (sid < 0 || sid >= SherpaOnnxOfflineTtsNumSpeakers(tts)) {
        fprintf(stderr, "syn-speak: speaker %ld out of range (the voice has %d)\n", sid, SherpaOnnxOfflineTtsNumSpeakers(tts));
        write_frame(NULL, 0);
        return 2;
    }
    SherpaOnnxGenerationConfig gc;
    memset(&gc, 0, sizeof gc);
    gc.sid = (int32_t)sid;
    double scale = own_length * length;
    gc.speed = scale > 0 ? (float)(1.0 / scale) : 1.0f;  /* sherpa's speed is the inverse of VITS's length scale */
    resample_reset((double)SherpaOnnxOfflineTtsSampleRate(tts));
    const SherpaOnnxGeneratedAudio *a = SherpaOnnxOfflineTtsGenerateWithConfig(tts, text, &gc, on_chunk, NULL);
    if (!a) {
        fprintf(stderr, "syn-speak: no audio generated\n");
        write_frame(NULL, 0);
        return 1;
    }
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(a);
    write_frame(NULL, 0);  /* end of this line */
    return 0;
}

/* A speaker id, digits only; -1 for anything else. */
static long speaker_id(const char *s) {
    char *end;
    long sid = strtol(s, &end, 10);
    return end == s || *end || sid < 0 ? -1 : sid;
}

/* A line's length field, LENGTH_MIN to LENGTH_MAX; -1 for anything else. */
static double length_of(const char *s) {
    char *end;
    double k = strtod(s, &end);
    return end == s || *end || !(k >= LENGTH_MIN && k <= LENGTH_MAX) ? -1 : k;
}

static int serve(void) {
    voices_scan();  /* the index of the voice store, built at start */
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, stdin)) > 0) {
        if (line[n - 1] == '\n') line[--n] = 0;
        char *t1 = strchr(line, '\t');
        char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        char *t3 = t2 ? strchr(t2 + 1, '\t') : NULL;  /* the optional fourth field, the length */
        if (!t2) {
            fprintf(stderr, "syn-speak: a request is \"<voice>\\t<speaker-id>\\t<text>\" or \"...\\t<text>\\t<length>\"\n");
            write_frame(NULL, 0);
            continue;
        }
        *t1 = 0;
        *t2 = 0;
        if (t3) *t3 = 0;
        long sid = speaker_id(t1 + 1);
        if (sid < 0) {
            fprintf(stderr, "syn-speak: speaker %s is not a number\n", t1 + 1);
            write_frame(NULL, 0);
            continue;
        }
        double length = t3 ? length_of(t3 + 1) : 1.0;
        if (length < 0) {
            fprintf(stderr, "syn-speak: length %s is not a number from %.1f to %.1f\n", t3 + 1, LENGTH_MIN, LENGTH_MAX);
            write_frame(NULL, 0);
            continue;
        }
        float own = 1.0f;
        const SherpaOnnxOfflineTts *tts = load_voice(line, &own);
        if (tts) speak_line(tts, own, sid, t2 + 1, length);
        else write_frame(NULL, 0);
    }
    free(line);
    free(in_buf);
    free(out_buf);
    return 0;
}

static const char *or_dash(const char *s) { return *s ? s : "-"; }

/* One TSV row per voice, loading no model: id, set, speakers, display, subject, consent, allow, characters, aliases,
 * status (ok, refused: reason, or no VOICE). An empty field is "-". */
static int list_voices(void) {
    voices_scan();
    for (int i = 0; i < nvoices; i++) {
        const Voice *v = &voices[i];
        if (!v->present) continue;
        char status[300];
        if (!v->manifest) snprintf(status, sizeof status, "no VOICE");
        else if (v->refused[0]) snprintf(status, sizeof status, "refused: %s", v->refused);
        else snprintf(status, sizeof status, "ok");
        printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", v->id, or_dash(v->set), or_dash(v->speakers_s),
               or_dash(v->display), or_dash(v->subject), or_dash(v->consent), or_dash(v->allow),
               or_dash(v->characters), or_dash(v->aliases), status);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--serve")) return serve();
    if (argc == 2 && !strcmp(argv[1], "--list")) return list_voices();
    if (argc < 3) {
        fprintf(stderr, "usage: %s <speaker-id> <text...>\n       %s --serve\n       %s --list\n", argv[0], argv[0], argv[0]);
        return 2;
    }
    long sid = speaker_id(argv[1]);
    if (sid < 0) {
        fprintf(stderr, "syn-speak: speaker %s is not a number\n", argv[1]);
        return 2;
    }
    char text[8192] = "";
    for (int i = 2; i < argc; i++) {
        strncat(text, argv[i], sizeof text - strlen(text) - 2);
        if (i + 1 < argc) strncat(text, " ", sizeof text - strlen(text) - 1);
    }
    voices_scan();
    float own = 1.0f;
    const SherpaOnnxOfflineTts *tts = load_voice("vctk", &own);
    if (!tts) return 1;
    int rc = speak_line(tts, own, sid, text, 1.0);
    free(in_buf);
    free(out_buf);
    return rc;
}

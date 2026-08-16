/*
 * rss_config.c — Raptor Streaming System INI config parser
 *
 * Simple [section] / key = value parser. Linked list storage,
 * case-insensitive section and key lookup, inline comment stripping.
 *
 * Saving edits the existing file line by line instead of reserializing
 * the parsed state: only lines owned by runtime-modified (dirty) keys
 * are touched, so comments, formatting, and other daemons' keys all
 * survive every save. A save with nothing dirty does not write at all.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

/* ------------------------------------------------------------------ */
/* Internal data structures                                            */
/* ------------------------------------------------------------------ */

#define MAX_LINE 512
#define MAX_SECN 64
#define MAX_KEY 64
#define MAX_VAL 256

typedef struct rss_config_entry {
    char key[MAX_KEY];
    char value[MAX_VAL];
    bool dirty;     /* modified at runtime via set_str/set_int */
    bool defaulted; /* stored by a getter's miss path purely so
                     * config-get-section can display the resolved
                     * value. Display-only: value lookups treat the
                     * entry as absent, so one call site's fallback can
                     * never masquerade as configuration to another
                     * call site with a different fallback (rvd once
                     * cached 1920x1080 this way before the sensor was
                     * known, and every 720p camera upscaled). */
    struct rss_config_entry *next;
} rss_config_entry_t;

typedef struct rss_config_section {
    char name[MAX_SECN];
    rss_config_entry_t *entries;
    struct rss_config_section *next;
} rss_config_section_t;

struct rss_config {
    rss_config_section_t *sections;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Find or create a section. Empty name ("") is the global section. */
static rss_config_section_t *find_or_create_section(rss_config_t *cfg, const char *name)
{
    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next) {
        if (strcasecmp(s->name, name) == 0)
            return s;
    }

    s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    rss_strlcpy(s->name, name, sizeof(s->name));
    s->next = cfg->sections;
    cfg->sections = s;
    return s;
}

static void add_entry_ex(rss_config_section_t *sec, const char *key, const char *value, bool dirty,
                         bool defaulted)
{
    /* Overwrite if key already exists */
    rss_config_entry_t *e;
    for (e = sec->entries; e; e = e->next) {
        if (strcasecmp(e->key, key) == 0) {
            if (rss_strlcpy(e->value, value, sizeof(e->value)) >= sizeof(e->value))
                RSS_WARN("config: value truncated for key '%s' (max %d)", key, MAX_VAL - 1);
            e->dirty = e->dirty || dirty;
            /* A real write (file entry or set_*) permanently clears
             * the display-only mark; a default refresh keeps it. */
            e->defaulted = e->defaulted && defaulted;
            return;
        }
    }

    e = calloc(1, sizeof(*e));
    if (!e) {
        RSS_WARN("config: alloc failed for key '%s'", key);
        return;
    }
    rss_strlcpy(e->key, key, sizeof(e->key));
    if (rss_strlcpy(e->value, value, sizeof(e->value)) >= sizeof(e->value))
        RSS_WARN("config: value truncated for key '%s' (max %d)", key, MAX_VAL - 1);
    e->dirty = dirty;
    e->defaulted = defaulted;
    e->next = sec->entries;
    sec->entries = e;
}

static void add_entry(rss_config_section_t *sec, const char *key, const char *value)
{
    add_entry_ex(sec, key, value, false, false);
}

static void add_entry_default(rss_config_section_t *sec, const char *key, const char *value)
{
    add_entry_ex(sec, key, value, false, true);
}

/* Find the '#' that starts an inline comment: ' #' or '\t#'.
 * Does NOT handle quoted values — "foo # bar" is a comment at #.
 * Config files currently use no quoted values. */
static const char *find_inline_comment(const char *s)
{
    const char *p = s;
    while ((p = strchr(p, '#')) != NULL) {
        if (p > s && (*(p - 1) == ' ' || *(p - 1) == '\t'))
            return p;
        p++;
    }
    return NULL;
}

static void strip_inline_comment(char *s)
{
    const char *p = find_inline_comment(s);
    if (!p)
        return;
    /* Trim trailing whitespace before the comment marker */
    char *end = s + (p - s) - 1;
    while (end > s && (*end == ' ' || *end == '\t'))
        end--;
    *(end + 1) = '\0';
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

rss_config_t *rss_config_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    rss_config_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        fclose(fp);
        return NULL;
    }

    char current_section[MAX_SECN] = "";
    char line[MAX_LINE];
    int lineno = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        lineno++;

        /*
         * A line that fills the buffer without its newline would
         * otherwise be split: the head silently parsed with a
         * truncated value, the tail parsed as a bogus second line.
         * Peek one char to tell "exactly fits" from "over-long";
         * over-long lines are rejected whole, loudly.
         */
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
            int ch = fgetc(fp);
            if (ch != '\n' && ch != EOF) {
                RSS_WARN("%s:%d: line too long (max %zu), ignored", path, lineno, sizeof(line) - 1);
                while ((ch = fgetc(fp)) != '\n' && ch != EOF)
                    ;
                continue;
            }
        }

        char *s = rss_trim(line);

        /* Skip empty lines and full-line comments */
        if (*s == '\0' || *s == '#' || *s == ';')
            continue;

        /* Section header: [name] */
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) {
                RSS_WARN("%s:%d: malformed section (no ']'): %s", path, lineno, s);
                continue;
            }
            *end = '\0';
            if (rss_strlcpy(current_section, rss_trim(s + 1), sizeof(current_section)) >=
                sizeof(current_section))
                RSS_WARN("%s:%d: section name truncated (max %d)", path, lineno, MAX_SECN - 1);
            continue;
        }

        /* Key = value */
        char *eq = strchr(s, '=');
        if (!eq) {
            RSS_WARN("%s:%d: malformed line (no '='): %s", path, lineno, s);
            continue;
        }

        *eq = '\0';
        char *key = rss_trim(s);
        char *val = rss_trim(eq + 1);

        /* Strip inline comments from value */
        strip_inline_comment(val);
        val = rss_trim(val);

        if (*key == '\0')
            continue;

        rss_config_section_t *sec = find_or_create_section(cfg, current_section);
        if (sec)
            add_entry(sec, key, val);
    }

    fclose(fp);
    return cfg;
}

void rss_config_free(rss_config_t *cfg)
{
    if (!cfg)
        return;

    rss_config_section_t *s = cfg->sections;
    while (s) {
        rss_config_section_t *ns = s->next;
        rss_config_entry_t *e = s->entries;
        while (e) {
            rss_config_entry_t *ne = e->next;
            free(e);
            e = ne;
        }
        free(s);
        s = ns;
    }
    free(cfg);
}

const char *rss_config_get_str(rss_config_t *cfg, const char *section, const char *key,
                               const char *default_val)
{
    if (!cfg || !key)
        return default_val;

    const char *sec_name = section ? section : "";

    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next) {
        if (strcasecmp(s->name, sec_name) != 0)
            continue;
        rss_config_entry_t *e;
        for (e = s->entries; e; e = e->next) {
            /* Display-only stored defaults are treated as absent, so
             * every caller resolves against its OWN fallback -- the
             * first reader's default must never become configuration
             * for a later reader that knows better (the later reader
             * often has the sensor-derived value). */
            if (strcasecmp(e->key, key) == 0 && !e->defaulted)
                return e->value;
        }
    }

    /* Auto-populate the default so config-get-section shows resolved
     * values. This mutates the config on read — acceptable because all
     * daemon access is single-threaded (init + ctrl handler both on
     * main thread via epoll). Not safe for concurrent readers on the
     * same config object. Only when default_val is non-NULL (callers
     * with NULL default probe for presence and want no storage). The
     * entry is marked display-only, never dirty: it is refreshed by
     * whichever reader ran last, is invisible to value lookups, and is
     * never written by a save. */
    if (default_val) {
        rss_config_section_t *ds = find_or_create_section(cfg, sec_name);
        if (ds) {
            add_entry_default(ds, key, default_val);
            rss_config_entry_t *de;
            for (de = ds->entries; de; de = de->next) {
                if (strcasecmp(de->key, key) == 0)
                    return de->value;
            }
        }
    }
    return default_val;
}

int rss_config_get_int(rss_config_t *cfg, const char *section, const char *key, int default_val)
{
    const char *val = rss_config_get_str(cfg, section, key, NULL);
    if (!val) {
        /* Display-only store so config-get-section shows it */
        if (cfg) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", default_val);
            rss_config_section_t *sec = find_or_create_section(cfg, section ? section : "");
            if (sec)
                add_entry_default(sec, key, buf);
        }
        return default_val;
    }

    char *end;
    long v = strtol(val, &end, 0);
    if (end == val)
        return default_val;
    return (int)v;
}

bool rss_config_get_bool(rss_config_t *cfg, const char *section, const char *key, bool default_val)
{
    const char *val = rss_config_get_str(cfg, section, key, NULL);
    if (!val) {
        /* Display-only store so config-get-section shows it */
        if (cfg) {
            rss_config_section_t *sec = find_or_create_section(cfg, section ? section : "");
            if (sec)
                add_entry_default(sec, key, default_val ? "true" : "false");
        }
        return default_val;
    }

    if (strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0 ||
        strcmp(val, "1") == 0)
        return true;

    if (strcasecmp(val, "false") == 0 || strcasecmp(val, "no") == 0 ||
        strcasecmp(val, "off") == 0 || strcmp(val, "0") == 0)
        return false;

    return default_val;
}

int rss_config_foreach(rss_config_t *cfg, const char *section,
                       void (*callback)(const char *key, const char *value, void *userdata),
                       void *userdata)
{
    if (!cfg || !callback)
        return 0;

    const char *sec_name = section ? section : "";
    int count = 0;

    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next) {
        if (strcasecmp(s->name, sec_name) != 0)
            continue;
        rss_config_entry_t *e;
        for (e = s->entries; e; e = e->next) {
            callback(e->key, e->value, userdata);
            count++;
        }
    }
    return count;
}

int rss_config_foreach_section(rss_config_t *cfg, const char *prefix,
                               void (*callback)(const char *section, void *userdata),
                               void *userdata)
{
    if (!cfg || !callback)
        return 0;

    int plen = prefix ? (int)strlen(prefix) : 0;
    int count = 0;

    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next) {
        if (plen > 0 && strncasecmp(s->name, prefix, plen) != 0)
            continue;
        callback(s->name, userdata);
        count++;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Config modification (running-config support)                        */
/* ------------------------------------------------------------------ */

void rss_config_set_str(rss_config_t *cfg, const char *section, const char *key, const char *value)
{
    if (!cfg || !key || !value)
        return;
    /* An empty section is not a place. The writer would emit the key above
     * the first [section] header, where no loader ever reads it again — a
     * JPEG channel with an unset cfg_sect persisted its quality into
     * exactly that oblivion. Refuse loudly; the parser still tolerates
     * such lines in existing files, this only stops creating new ones. */
    if (!section || !section[0]) {
        RSS_WARN("config: refusing to set '%s' without a section", key);
        return;
    }
    rss_config_section_t *sec = find_or_create_section(cfg, section);
    if (sec)
        add_entry_ex(sec, key, value, true, false);
}

void rss_config_set_int(rss_config_t *cfg, const char *section, const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    rss_config_set_str(cfg, section, key, buf);
}

void rss_config_set_bool(rss_config_t *cfg, const char *section, const char *key, bool value)
{
    rss_config_set_str(cfg, section, key, value ? "true" : "false");
}

bool rss_config_has_dirty(const rss_config_t *cfg)
{
    if (!cfg)
        return false;
    const rss_config_section_t *s;
    const rss_config_entry_t *e;
    for (s = cfg->sections; s; s = s->next)
        for (e = s->entries; e; e = e->next)
            if (e->dirty)
                return true;
    return false;
}

/* Write a config to a file (no merging) */
static int config_write(rss_config_t *cfg, const char *path)
{
    /* Count sections to reverse linked-list order (restore file order) */
    int nsec = 0;
    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next)
        nsec++;

    if (nsec == 0)
        return rss_write_file_atomic(path, "", 0);

    /* Collect section pointers for reverse traversal */
    rss_config_section_t **secs = malloc(nsec * sizeof(*secs));
    if (!secs)
        return -1;

    int i = 0;
    for (s = cfg->sections; s; s = s->next)
        secs[i++] = s;

    /* Build output into a dynamic buffer */
    int buf_size = 4096;
    char *buf = malloc(buf_size);
    if (!buf) {
        free(secs);
        return -1;
    }
    int off = 0;
    int ret = -1;

    for (i = nsec - 1; i >= 0; i--) {
        s = secs[i];

        /* Count entries for reverse traversal. Display-only stored
         * defaults never reach the file: writing them would freeze one
         * boot's fallbacks as configuration for every later boot. */
        int nent = 0;
        rss_config_entry_t *e;
        for (e = s->entries; e; e = e->next)
            if (!e->defaulted)
                nent++;

        if (nent == 0)
            continue;

        rss_config_entry_t **ents = malloc(nent * sizeof(*ents));
        if (!ents)
            goto out;

        int j = 0;
        for (e = s->entries; e; e = e->next)
            if (!e->defaulted)
                ents[j++] = e;

        /* Section header (skip for global section) */
        if (s->name[0] != '\0') {
            int need = off + (int)strlen(s->name) + 4;
            if (need > buf_size) {
                int new_size = need + 4096;
                char *nb = realloc(buf, new_size);
                if (!nb) {
                    free(ents);
                    goto out;
                }
                buf = nb;
                buf_size = new_size;
            }
            if (off > 0)
                buf[off++] = '\n'; /* blank line between sections */
            off += snprintf(buf + off, buf_size - off, "[%s]\n", s->name);
        }

        /* Entries in original order */
        for (j = nent - 1; j >= 0; j--) {
            int need = off + (int)strlen(ents[j]->key) + (int)strlen(ents[j]->value) + 8;
            if (need > buf_size) {
                int new_size = need + 4096;
                char *nb = realloc(buf, new_size);
                if (!nb) {
                    free(ents);
                    goto out;
                }
                buf = nb;
                buf_size = new_size;
            }
            off += snprintf(buf + off, buf_size - off, "%s = %s\n", ents[j]->key, ents[j]->value);
        }

        free(ents);
    }

    ret = rss_write_file_atomic(path, buf, off);

out:
    free(secs);
    free(buf);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Comment-preserving surgical save                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int kind;           /* 0 = inert, 1 = section header, 2 = key line */
    bool nonblank;      /* anything visible on the line */
    char sec[MAX_SECN]; /* kind 1: parsed section name */
    char key[MAX_KEY];  /* kind 2: the key as the parser sees it */
    int key_end;        /* kind 2: raw offset just past the key token */
    int com_off;        /* kind 2: raw offset of the inline comment run, or -1 */
} line_info_t;

/* Mirror rss_config_load's rules exactly: a line the parser would not
 * bind to a key or section must never be edited. */
static void classify_line(const char *raw, int len, line_info_t *li)
{
    li->kind = 0;
    li->nonblank = false;
    li->com_off = -1;

    for (int i = 0; i < len; i++) {
        if (!isspace((unsigned char)raw[i])) {
            li->nonblank = true;
            break;
        }
    }

    /* The parser rejects over-long lines whole */
    if (len >= MAX_LINE)
        return;

    char copy[MAX_LINE];
    memcpy(copy, raw, (size_t)len);
    copy[len] = '\0';

    char *s = rss_trim(copy);
    if (*s == '\0' || *s == '#' || *s == ';')
        return;

    if (*s == '[') {
        char *end = strchr(s, ']');
        if (!end)
            return;
        *end = '\0';
        rss_strlcpy(li->sec, rss_trim(s + 1), sizeof(li->sec));
        li->kind = 1;
        return;
    }

    char *eq = strchr(s, '=');
    if (!eq)
        return;

    *eq = '\0';
    char *key = rss_trim(s);
    if (*key == '\0')
        return;

    li->kind = 2;
    li->key_end = (int)(key - copy) + (int)strlen(key);
    rss_strlcpy(li->key, key, sizeof(li->key));

    /* Note where an inline comment starts (including its alignment
     * whitespace) so a replacement line can carry it over. */
    char *val = rss_trim(eq + 1);
    const char *com = find_inline_comment(val);
    if (com) {
        while (com > copy && (*(com - 1) == ' ' || *(com - 1) == '\t'))
            com--;
        li->com_off = (int)(com - copy);
    }
}

typedef struct {
    const char *sec; /* section name, entry spelling */
    rss_config_entry_t *ent;
    int replace_line; /* line whose key this entry replaces, or -1 */
    int rep_key_end;  /* that line's key_end */
    int rep_com_off;  /* that line's com_off */
    int append_line;  /* insert after this line, or -1 = top of file */
    bool sec_in_file;
    bool done;
} dirty_ref_t;

/* Collect dirty entries in original set order (both lists prepend, so
 * reversing the flat walk restores it). Returns the count, -1 on OOM. */
static int collect_dirty(rss_config_t *cfg, dirty_ref_t **out)
{
    int ndirty = 0;
    rss_config_section_t *s;
    rss_config_entry_t *e;

    *out = NULL;
    for (s = cfg->sections; s; s = s->next)
        for (e = s->entries; e; e = e->next)
            if (e->dirty)
                ndirty++;
    if (ndirty == 0)
        return 0;

    dirty_ref_t *dr = calloc((size_t)ndirty, sizeof(*dr));
    if (!dr)
        return -1;

    int d = 0;
    for (s = cfg->sections; s; s = s->next) {
        for (e = s->entries; e; e = e->next) {
            if (!e->dirty)
                continue;
            dr[d].sec = s->name;
            dr[d].ent = e;
            dr[d].replace_line = -1;
            dr[d].append_line = -1;
            d++;
        }
    }
    for (int i = 0, j = ndirty - 1; i < j; i++, j--) {
        dirty_ref_t tmp = dr[i];
        dr[i] = dr[j];
        dr[j] = tmp;
    }

    *out = dr;
    return ndirty;
}

/* Rewrite the file with only the dirty entries edited in: the last key
 * line that matches in its section is regenerated in place (keeping its
 * inline comment), new keys land at the end of their section's content,
 * new sections at end of file. Every other line — comments, blanks,
 * even lines the parser rejects — is emitted verbatim. */
static int config_write_surgical(const char *text, int tsize, dirty_ref_t *dr, int ndirty,
                                 const char *path)
{
    int nlines = 0;
    for (int i = 0; i < tsize; i++)
        if (text[i] == '\n')
            nlines++;
    if (tsize > 0 && text[tsize - 1] != '\n')
        nlines++;

    int *loff = NULL, *llen = NULL;
    if (nlines > 0) {
        loff = malloc((size_t)nlines * sizeof(*loff));
        llen = malloc((size_t)nlines * sizeof(*llen));
        if (!loff || !llen) {
            free(loff);
            free(llen);
            return -1;
        }
        int l = 0, start = 0;
        for (int i = 0; i < tsize; i++) {
            if (text[i] == '\n') {
                loff[l] = start;
                llen[l] = i - start;
                l++;
                start = i + 1;
            }
        }
        if (start < tsize) {
            loff[l] = start;
            llen[l] = tsize - start;
        }
    }

    /* Bind every dirty entry to the file */
    char cursec[MAX_SECN] = "";
    line_info_t li;
    for (int l = 0; l < nlines; l++) {
        classify_line(text + loff[l], llen[l], &li);
        if (li.kind == 1)
            rss_strlcpy(cursec, li.sec, sizeof(cursec));
        for (int d = 0; d < ndirty; d++) {
            if (strcasecmp(dr[d].sec, cursec) != 0)
                continue;
            if (li.kind == 1)
                dr[d].sec_in_file = true;
            if (li.nonblank)
                dr[d].append_line = l;
            if (li.kind == 2 && strcasecmp(dr[d].ent->key, li.key) == 0) {
                dr[d].replace_line = l;
                dr[d].rep_key_end = li.key_end;
                dr[d].rep_com_off = li.com_off;
            }
        }
    }

    /* A replaced line grows by at most " = value"; appended keys,
     * headers, and separators are bounded by the fixed field sizes. */
    int cap = tsize + nlines + ndirty * (MAX_LINE + MAX_VAL + MAX_SECN + 16) + 16;
    char *out = malloc((size_t)cap);
    if (!out) {
        free(loff);
        free(llen);
        return -1;
    }
    int off = 0;

    /* Global-section keys with nothing to anchor to go at the top */
    for (int d = 0; d < ndirty; d++) {
        if (dr[d].sec[0] == '\0' && dr[d].replace_line < 0 && dr[d].append_line < 0) {
            off += snprintf(out + off, (size_t)(cap - off), "%s = %s\n", dr[d].ent->key,
                            dr[d].ent->value);
            dr[d].done = true;
        }
    }

    for (int l = 0; l < nlines; l++) {
        int rep = -1;
        for (int d = 0; d < ndirty; d++) {
            if (dr[d].replace_line == l) {
                rep = d;
                break;
            }
        }
        if (rep >= 0) {
            dirty_ref_t *r = &dr[rep];
            int vlen = (int)strlen(r->ent->value);
            int keylen = r->rep_key_end;
            int comlen = (r->rep_com_off >= 0) ? llen[l] - r->rep_com_off : 0;

            /* Never rebuild a line the parser would reject — it would
             * lose the value on the next load. Drop the comment first,
             * then the file's key spelling (the stored key is capped at
             * MAX_KEY, so the short form always fits). */
            if (keylen + 3 + vlen + comlen >= MAX_LINE)
                comlen = 0;
            if (keylen + 3 + vlen >= MAX_LINE)
                keylen = 0;

            if (keylen > 0) {
                memcpy(out + off, text + loff[l], (size_t)keylen);
                off += keylen;
                off += snprintf(out + off, (size_t)(cap - off), " = %s", r->ent->value);
            } else {
                off +=
                    snprintf(out + off, (size_t)(cap - off), "%s = %s", r->ent->key, r->ent->value);
            }
            if (comlen > 0) {
                memcpy(out + off, text + loff[l] + r->rep_com_off, (size_t)comlen);
                off += comlen;
            }
            out[off++] = '\n';
            r->done = true;
        } else {
            memcpy(out + off, text + loff[l], (size_t)llen[l]);
            off += llen[l];
            out[off++] = '\n';
        }
        for (int d = 0; d < ndirty; d++) {
            if (!dr[d].done && dr[d].replace_line < 0 && dr[d].append_line == l) {
                off += snprintf(out + off, (size_t)(cap - off), "%s = %s\n", dr[d].ent->key,
                                dr[d].ent->value);
                dr[d].done = true;
            }
        }
    }

    /* Sections the file does not have yet */
    for (int d = 0; d < ndirty; d++) {
        if (dr[d].done || dr[d].sec_in_file || dr[d].sec[0] == '\0')
            continue;
        if (off > 0 && !(off > 1 && out[off - 1] == '\n' && out[off - 2] == '\n'))
            out[off++] = '\n';
        off += snprintf(out + off, (size_t)(cap - off), "[%s]\n", dr[d].sec);
        for (int d2 = d; d2 < ndirty; d2++) {
            if (dr[d2].done || strcasecmp(dr[d2].sec, dr[d].sec) != 0)
                continue;
            off += snprintf(out + off, (size_t)(cap - off), "%s = %s\n", dr[d2].ent->key,
                            dr[d2].ent->value);
            dr[d2].done = true;
        }
    }

    int ret = rss_write_file_atomic(path, out, off);
    free(out);
    free(loff);
    free(llen);
    return ret;
}

int rss_config_save(rss_config_t *cfg, const char *path)
{
    if (!cfg || !path)
        return -1;

    dirty_ref_t *dr = NULL;
    int ndirty = collect_dirty(cfg, &dr);
    if (ndirty < 0)
        return -1;

    /* Nothing changed and the file exists: do not touch it. Every
     * daemon answers config-save, so a no-op save must not cost a
     * flash rewrite (or disturb a hand-maintained file) per daemon. */
    if (ndirty == 0 && access(path, F_OK) == 0)
        return 0;

    /* Serialize saves across daemons sharing the same config file.
     * flock on a .lock sidecar prevents concurrent read-edit-write
     * cycles from losing updates via the atomic rename. */
    char lockpath[512];
    snprintf(lockpath, sizeof(lockpath), "%s.lock", path);
    int lock_fd = open(lockpath, O_WRONLY | O_CREAT, 0644);
    if (lock_fd < 0)
        RSS_WARN("config: failed to acquire save lock (%s), proceeding unserialized",
                 strerror(errno));
    if (lock_fd >= 0)
        flock(lock_fd, LOCK_EX);

    /* Edit the existing file in place so everything this config does
     * not own — other daemons' keys, comments, formatting — survives.
     * Without an existing file, write out the whole in-memory config. */
    int ret;
    int tsize = 0;
    char *text = rss_read_file(path, &tsize);
    if (text) {
        ret = config_write_surgical(text, tsize, dr, ndirty, path);
        free(text);
    } else {
        ret = config_write(cfg, path);
    }

    /* Clear dirty flags on successful save */
    if (ret == 0) {
        rss_config_section_t *s;
        for (s = cfg->sections; s; s = s->next) {
            rss_config_entry_t *e;
            for (e = s->entries; e; e = e->next)
                e->dirty = false;
        }
    }

    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    free(dr);
    return ret;
}

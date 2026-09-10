/*
 * rss_shadow.c -- Reading one field of /etc/shadow. See rss_shadow.h.
 */

#include <stdio.h>
#include <string.h>

#include "rss_shadow.h"
#include "rss_common.h"

const char *rss_shadow_state_name(rss_shadow_state_t s)
{
    switch (s) {
    case RSS_SHADOW_SET:
        return "set";
    case RSS_SHADOW_UNSET:
        return "unset";
    case RSS_SHADOW_LOCKED:
        return "locked";
    case RSS_SHADOW_MISSING:
        return "missing";
    }
    return "missing";
}

/*
 * A usable hash is "$id$salt$digest": three dollars, none of them last, and
 * nothing before the first. Anything else is refused rather than guessed at --
 * a bare DES field, a truncated line, a format this libc does not implement --
 * because the safe way to be wrong about a password field is to decline it.
 *
 * "!" and "*" fall out of the same test rather than being named: neither
 * starts with '$'. They are still worth distinguishing from garbage, and the
 * caller below does that, but not by a second rule about their spelling.
 */
static bool usable_hash(const char *f)
{
    if (f[0] != '$')
        return false;

    int dollars = 0;
    size_t last = 0;

    for (size_t i = 0; f[i]; i++) {
        if (f[i] == '$') {
            dollars++;
            last = i;
        }
    }
    return dollars >= 3 && f[last + 1] != '\0';
}

/*
 * The password field of the first line naming `user`, or NULL.
 *
 * `line` is the caller's buffer and is modified in place: the field is
 * terminated where the next colon was.
 */
static const char *passwd_field(const char *path, const char *user, char *line, size_t linesz)
{
    if (!user || !user[0] || strchr(user, ':'))
        return NULL;

    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    size_t ulen = strlen(user);
    const char *field = NULL;

    while (fgets(line, (int)linesz, f)) {
        if (strncmp(line, user, ulen) != 0 || line[ulen] != ':')
            continue;

        char *p = line + ulen + 1;
        char *end = strchr(p, ':');

        if (end)
            *end = '\0';
        p[strcspn(p, "\r\n")] = '\0';
        field = p;
        break;
    }

    fclose(f);
    return field;
}

rss_shadow_state_t rss_shadow_state(const char *path, const char *user)
{
    char line[512];
    const char *f = passwd_field(path, user, line, sizeof(line));

    if (!f)
        return RSS_SHADOW_MISSING;
    if (!f[0])
        return RSS_SHADOW_UNSET;
    if (usable_hash(f))
        return RSS_SHADOW_SET;
    return RSS_SHADOW_LOCKED;
}

bool rss_shadow_hash(const char *path, const char *user, char *out, size_t outsz)
{
    char line[512];
    const char *f = passwd_field(path, user, line, sizeof(line));

    if (!f || !usable_hash(f) || strlen(f) >= outsz)
        return false;

    rss_strlcpy(out, f, outsz);
    return true;
}

/*
 * rss_shadow.h -- What /etc/shadow says about an account
 *
 * Two daemons ask about the same line and must not answer differently. rhd
 * authenticates the configuration route against the system account; rcd writes
 * that account's password when a camera is claimed, and refuses to write it a
 * second time. So "this camera has no password yet" has to mean one thing on
 * both sides of the socket -- it is the single fact the claim turns on, and a
 * second definition of it would be a second answer to "may a stranger take
 * this camera".
 *
 * The path is a parameter rather than a constant because the two callers do
 * not agree on it: rcd's is under RCD_SYSCONF_DIR so the suite can point its
 * writers at a scratch directory, rhd's is /etc/shadow. Passing it in keeps
 * the classification shared and leaves the location to whoever owns it.
 */

#ifndef RSS_SHADOW_H
#define RSS_SHADOW_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Four answers where the obvious design has two, and the two extra ones are
 * the whole reason this is an enum.
 *
 * A camera may be claimed over the network only from UNSET -- a field that is
 * genuinely empty, which is what a fresh image ships. LOCKED is an integrator
 * saying "no password login on this device", and a claim route that treated
 * that as an invitation would undo their decision from the network. MISSING is
 * a file this process could not read or an account that is not there, which is
 * a broken camera rather than an available one.
 *
 * So SET authenticates, UNSET may be claimed, and the other two do neither.
 * They are kept apart only so the log can say which it was.
 */
typedef enum {
    RSS_SHADOW_SET,     /* a usable "$id$salt$digest" */
    RSS_SHADOW_UNSET,   /* the field is empty: no password has ever been set */
    RSS_SHADOW_LOCKED,  /* "*", "!", "!!", or a field crypt(3) cannot use */
    RSS_SHADOW_MISSING, /* no such account, or the file cannot be read */
} rss_shadow_state_t;

/* One word for a state, for a log line. Never NULL. */
const char *rss_shadow_state_name(rss_shadow_state_t s);

/*
 * Classify `user`'s password field.
 *
 * The first line naming the account wins and the rest of the file is not
 * read, which is what the login stack does with a duplicated entry: answering
 * from a later line would authenticate against a hash nothing else uses.
 */
rss_shadow_state_t rss_shadow_state(const char *path, const char *user);

/*
 * The account's hash, for a caller about to run crypt(3) against it.
 *
 * True only for RSS_SHADOW_SET, so a locked or password-less account cannot
 * be authenticated against by a caller that forgot to ask the question above.
 * That is deliberate duplication: clearing the root password is a thing people
 * do while debugging, and it has to lock the camera rather than open it.
 */
bool rss_shadow_hash(const char *path, const char *user, char *out, size_t outsz);

#endif /* RSS_SHADOW_H */

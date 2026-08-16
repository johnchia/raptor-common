/*
 * rss_common.h — Raptor Streaming System common library
 *
 * Public API for logging, configuration, daemonization, signal handling,
 * and general utilities shared by all RSS daemons.
 *
 * Pure POSIX C11. No vendor dependencies.
 */

#ifndef RSS_COMMON_H
#define RSS_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Paths
 * ================================================================ */

#define RSS_RUN_DIR "/var/run/rss"
#define RSS_SOCK_FMT RSS_RUN_DIR "/%s.sock"
#define RSS_CONFIG_PATH "/etc/raptor.conf"
#define RSS_SHM_DIR "/dev/shm"

/* ================================================================
 * Logging
 * ================================================================ */

typedef enum {
    RSS_LOG_FATAL = 0,
    RSS_LOG_ERROR = 1,
    RSS_LOG_WARN = 2,
    RSS_LOG_INFO = 3,
    RSS_LOG_DEBUG = 4,
    RSS_LOG_TRACE = 5,
} rss_log_level_t;

typedef enum {
    RSS_LOG_TARGET_STDERR = 0,
    RSS_LOG_TARGET_SYSLOG = 1,
    RSS_LOG_TARGET_FILE = 2,
    RSS_LOG_TARGET_BOTH = 3,
} rss_log_target_t;

/* Initialize logging. daemon_name is used as syslog ident and log prefix.
 * Call once at daemon startup. */
void rss_log_init(const char *daemon_name, rss_log_level_t level, rss_log_target_t target,
                  const char *log_file);

/* Set log level at runtime (e.g., from raptorctl command) */
void rss_log_set_level(rss_log_level_t level);
rss_log_level_t rss_log_get_level(void);

/* Log a message. Use the macros below instead. */
void rss_log(rss_log_level_t level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5), nonnull(4)));
void rss_vlog(rss_log_level_t level, const char *file, int line, const char *fmt, va_list ap)
    __attribute__((nonnull(4)));

#define RSS_FATAL(fmt, ...) rss_log(RSS_LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define RSS_ERROR(fmt, ...) rss_log(RSS_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define RSS_WARN(fmt, ...) rss_log(RSS_LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define RSS_INFO(fmt, ...) rss_log(RSS_LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define RSS_DEBUG(fmt, ...) rss_log(RSS_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define RSS_TRACE(fmt, ...) rss_log(RSS_LOG_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* ================================================================
 * Configuration
 *
 * Simple key=value config parser. Supports sections [section],
 * comments (#), and inline comments.
 *
 * Config file format:
 *   [section]
 *   key = value
 *   # comment
 * ================================================================ */

typedef struct rss_config rss_config_t; /* opaque */

/* Load config from file. Returns NULL on error. */
rss_config_t *rss_config_load(const char *path);

/* Free config. */
void rss_config_free(rss_config_t *cfg);

/* Get string value. Returns default_val if key not found.
 * Section can be NULL for global keys.
 *
 * NOT thread-safe: lazily inserts default_val into the config on miss
 * (for config-get-section display). All access must be from a single
 * thread (main/ctrl via epoll). */
const char *rss_config_get_str(rss_config_t *cfg, const char *section, const char *key,
                               const char *default_val);

/* Get integer value. Same thread-safety caveat as rss_config_get_str. */
int rss_config_get_int(rss_config_t *cfg, const char *section, const char *key, int default_val);

/* Get boolean value (true/false, yes/no, 1/0, on/off). */
bool rss_config_get_bool(rss_config_t *cfg, const char *section, const char *key, bool default_val);

/* Iterate all keys in a section. Returns number of keys.
 * callback is called for each key/value pair. */
int rss_config_foreach(rss_config_t *cfg, const char *section,
                       void (*callback)(const char *key, const char *value, void *userdata),
                       void *userdata);

/* Iterate sections matching a prefix. Callback receives section name.
 * Pass prefix="" or NULL to iterate all sections. */
int rss_config_foreach_section(rss_config_t *cfg, const char *prefix,
                               void (*callback)(const char *section, void *userdata),
                               void *userdata);

/* Set a string value in the running config. Creates section/key if needed. */
void rss_config_set_str(rss_config_t *cfg, const char *section, const char *key, const char *value);

/* Set an integer value in the running config. */
void rss_config_set_int(rss_config_t *cfg, const char *section, const char *key, int value);

/* Set a boolean value in the running config. */
void rss_config_set_bool(rss_config_t *cfg, const char *section, const char *key, bool value);

/* True if any key was modified at runtime and not yet saved. */
bool rss_config_has_dirty(const rss_config_t *cfg);

/* Save running config to disk. Edits the existing file in place: only
 * runtime-modified (dirty) keys are touched, comments and formatting
 * survive, and a save with nothing dirty does not write at all.
 * Atomic (tmp + fsync + rename). Returns 0 on success. */
int rss_config_save(rss_config_t *cfg, const char *path);

/* ================================================================
 * Daemon Lifecycle
 * ================================================================ */

/* Daemonize the process (fork, setsid, close fds, chdir /).
 * Creates PID file at /var/run/rss/<name>.pid.
 * Returns 0 on success (in child), -1 on error.
 * If already_daemon is true, skip fork but still write pidfile. */
int rss_daemonize(const char *name, bool already_daemon);

/* Remove PID file. Call at clean shutdown. */
void rss_daemon_cleanup(const char *name);

/* Check if a daemon is running. Returns PID if running, 0 if not, -1 on error. */
int rss_daemon_check(const char *name);

/* Request clean shutdown: clears running flag.
 * The daemon's main loop exits and the process terminates. */
void rss_daemon_request_shutdown(void);

/* Request restart: sets restart flag and clears running flag.
 * Call from a ctrl handler. The daemon's main loop exits,
 * then calls rss_daemon_exec() after cleanup. */
void rss_daemon_request_restart(void);

/* Check if a restart was requested (vs normal shutdown). */
bool rss_daemon_restart_pending(void);

/* Re-exec the daemon binary. Call after cleanup, never returns on success. */
void rss_daemon_exec(const char *name);

/* ================================================================
 * Signal Handling
 * ================================================================ */

/* Install common signal handlers:
 * - SIGTERM/SIGINT -> set running=false (clean shutdown)
 * - SIGHUP/SIGPIPE -> ignore
 * Returns the "running" flag pointer. Use rss_running() to read it
 * from threads — direct dereference races with the signal handler. */
volatile sig_atomic_t *rss_signal_init(void);

/* Thread-safe read of the running flag (atomic load). */
static inline int rss_running(volatile sig_atomic_t *flag)
{
    return __atomic_load_n((int *)flag, __ATOMIC_RELAXED);
}

/* ================================================================
 * Timestamp Utilities
 * ================================================================ */

/* Get monotonic timestamp in microseconds */
int64_t rss_timestamp_us(void);

/* Get wall-clock timestamp in microseconds */
int64_t rss_wallclock_us(void);

/* True when the system clock is NTP-disciplined (kernel STA_UNSYNC
 * clear). Feeds the MISB ST 0603 time status in SEI timecodes. */
bool rss_ntp_synced(void);

/* Format timestamp as ISO 8601 string (YYYY-MM-DD HH:MM:SS) into buf.
 * buf must be at least 20 bytes. Returns buf. */
char *rss_format_timestamp(char *buf, int buf_size);

/* Format timestamp with custom format string (strftime-compatible) */
char *rss_format_timestamp_fmt(char *buf, int buf_size, const char *fmt);

/* ================================================================
 * String Utilities
 * ================================================================ */

/* Safe string copy that always NUL-terminates. Returns src length
 * (like BSD strlcpy): if retval >= dst_size, truncation occurred. */
size_t rss_strlcpy(char *dst, const char *src, size_t dst_size);

/* Trim leading/trailing whitespace in-place. Returns s. */
char *rss_trim(char *s);

/* Check if string starts with prefix */
bool rss_starts_with(const char *s, const char *prefix);

/* Constant-time string comparison (for auth credentials).
 * Compares all bytes regardless of mismatch position. */
bool rss_secure_compare(const char *a, const char *b);

/* ================================================================
 * JSON Helpers (backed by vendored cJSON)
 *
 * Key lookup in JSON strings from control socket commands.
 * Used by raptorctl and inter-daemon IPC.
 * ================================================================ */

/* Extract string value for "key":"value". Returns 0 on success, -1 if not found. */
int rss_json_get_str(const char *json, const char *key, char *buf, int buf_size);

/* Extract integer value for "key":123. Returns 0 on success, -1 if not found. */
int rss_json_get_int(const char *json, const char *key, int *out);

/* Extract integer from nested object: {"parent":{"key":123}}. Returns 0 on success. */
int rss_json_get_nested_int(const char *json, const char *parent, const char *key, int *out);

/* ================================================================
 * Daemon Init Helper
 *
 * Combines the standard startup sequence shared by all daemons:
 * parse args (-c config -f foreground -d debug -h help),
 * init logging, load config, daemonize, install signal handlers.
 * ================================================================ */

typedef struct {
    const char *name;               /* daemon name (for logging, PID file) */
    rss_config_t *cfg;              /* loaded config (caller must free) */
    const char *config_path;        /* config file path used */
    volatile sig_atomic_t *running; /* signal-controlled run flag */
    bool foreground;
    bool debug;
} rss_daemon_ctx_t;

/* Standard daemon startup. Parses args, inits logging, loads config,
 * daemonizes, installs signal handlers.
 * Returns 0 on success, 1 if daemon should exit cleanly (e.g. -h),
 * -1 on fatal error. On success, ctx is populated. */
int rss_daemon_init(rss_daemon_ctx_t *ctx, const char *name, int argc, char **argv,
                    const char *features);

/* Build info — extern symbols from rss_build_info.o (generated by the
 * raptor Makefile) so the hash isn't baked into librss_common.a. */
extern const char *rss_build_hash __attribute__((weak));
extern const char *rss_build_time __attribute__((weak));
extern const char *rss_build_platform __attribute__((weak));

/* ================================================================
 * Control Socket Common Handlers
 *
 * Standard command handlers shared by all daemons. Call from
 * daemon-specific ctrl handler to avoid duplicating config-get,
 * config-save, etc. Returns response length if handled, -1 if
 * the command was not recognized (caller should handle it).
 * ================================================================ */

int rss_ctrl_handle_common(const char *cmd_json, char *resp_buf, int resp_buf_size,
                           rss_config_t *cfg, const char *config_path);

/* Serializer-built control requests (rss_ctrl_client.c): the command
 * JSON is assembled by cJSON and never appears at call sites. Same
 * return contract as rss_ctrl_send_command. rss_ctrl_resp_is_ok()
 * parses an answer's status field — never substring-match one. */
int rss_ctrl_cmd(const char *sock_path, const char *cmd, char *resp, int resp_size, int timeout_ms);
int rss_ctrl_cmd_int(const char *sock_path, const char *cmd, const char *key, int value, char *resp,
                     int resp_size, int timeout_ms);
int rss_ctrl_cmd_str(const char *sock_path, const char *cmd, const char *key, const char *value,
                     char *resp, int resp_size, int timeout_ms);
bool rss_ctrl_resp_is_ok(const char *resp);

/* Format a control socket response. Returns byte count for handler return. */
__attribute__((format(printf, 3, 4))) static inline int rss_ctrl_resp(char *buf, int size,
                                                                      const char *fmt, ...)
{
    if (size <= 0)
        return 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, (size_t)size, fmt, ap);
    va_end(ap);
    return (int)strlen(buf);
}

/* The status responses are built by cJSON, not by hand: the old
 * printf forms silently truncated a long reason into INVALID JSON,
 * and a %s of an unvetted string was one edit away from injection.
 * On allocation failure they answer empty — clients already treat a
 * zero-length response as an error. rss_ctrl_resp() above remains
 * for the PLAIN-TEXT responses (raw config values, "restarting"),
 * which are not JSON and must not pretend to be. */
static inline int rss_ctrl_resp_ok(char *buf, int size)
{
    if (size <= 0)
        return 0;
    buf[0] = '\0';
    cJSON *r = cJSON_CreateObject();
    if (!r)
        return 0;
    cJSON_AddStringToObject(r, "status", "ok");
    if (!cJSON_PrintPreallocated(r, buf, size, 0))
        buf[0] = '\0';
    cJSON_Delete(r);
    return (int)strlen(buf);
}

static inline int rss_ctrl_resp_error(char *buf, int size, const char *reason)
{
    if (size <= 0)
        return 0;
    buf[0] = '\0';
    cJSON *r = cJSON_CreateObject();
    if (!r)
        return 0;
    cJSON_AddStringToObject(r, "status", "error");
    cJSON_AddStringToObject(r, "reason", reason ? reason : "");
    if (!cJSON_PrintPreallocated(r, buf, size, 0))
        buf[0] = '\0';
    cJSON_Delete(r);
    return (int)strlen(buf);
}

/* Serialize a cJSON object into the response buffer and free it.
 * Takes ownership: json is always deleted, even on failure. */
static inline int rss_ctrl_resp_json(char *buf, int size, cJSON *json)
{
    if (!json)
        return rss_ctrl_resp_error(buf, size, "json alloc failed");
    if (!cJSON_PrintPreallocated(json, buf, size, 0)) {
        cJSON_Delete(json);
        return rss_ctrl_resp_error(buf, size, "response too large");
    }
    cJSON_Delete(json);
    return (int)strlen(buf);
}

/* ================================================================
 * File Utilities
 * ================================================================ */

/* Read entire file contents into malloc'd buffer. Caller frees.
 * Returns NULL on error. *out_size set to file size if non-NULL. */
char *rss_read_file(const char *path, int *out_size);

/* Write buffer to file atomically (write to .tmp, rename). */
int rss_write_file_atomic(const char *path, const void *data, int size);

/* Ensure directory exists (mkdir -p). Returns 0 on success. */
int rss_mkdir_p(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* RSS_COMMON_H */

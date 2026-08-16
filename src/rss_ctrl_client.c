/*
 * rss_ctrl_client.c — serializer-built control requests
 *
 * Every daemon-to-daemon control message is built here by cJSON, never
 * by hand: string assembly of structured formats is safe only until
 * the next edit interpolates something it shouldn't, and a rule with
 * exceptions has to be re-argued at every review. Call sites name the
 * command and (optionally) one argument; the JSON never appears in
 * consumer code at all. The companion on the answer side is
 * rss_ctrl_resp_is_ok(), which parses instead of substring-matching —
 * a check tied to the peer's formatting is a formatting accident away
 * from always failing.
 */

#include "rss_common.h"
#include "cJSON.h"

#include <string.h>

/* Local prototype: raptor-common deliberately has no compile-time
 * dependency on rss_ipc.h (see rss_daemon.c); the symbol resolves
 * from librss_ipc at link time, which every consumer links. */
extern int rss_ctrl_send_command(const char *sock_path, const char *cmd_json, char *resp_buf,
                                 int resp_buf_size, int timeout_ms);

/* Serialize and send, consuming the object. Command messages are one
 * verb and at most one argument; 512 bytes is generous. */
static int ctrl_send_obj(const char *sock_path, cJSON *obj, char *resp, int resp_size,
                         int timeout_ms)
{
    char msg[512];
    int ok = obj && cJSON_PrintPreallocated(obj, msg, sizeof(msg), 0);
    cJSON_Delete(obj);
    if (!ok)
        return -1;
    return rss_ctrl_send_command(sock_path, msg, resp, resp_size, timeout_ms);
}

int rss_ctrl_cmd(const char *sock_path, const char *cmd, char *resp, int resp_size, int timeout_ms)
{
    cJSON *o = cJSON_CreateObject();
    if (o)
        cJSON_AddStringToObject(o, "cmd", cmd);
    return ctrl_send_obj(sock_path, o, resp, resp_size, timeout_ms);
}

int rss_ctrl_cmd_int(const char *sock_path, const char *cmd, const char *key, int value, char *resp,
                     int resp_size, int timeout_ms)
{
    cJSON *o = cJSON_CreateObject();
    if (o) {
        cJSON_AddStringToObject(o, "cmd", cmd);
        cJSON_AddNumberToObject(o, key, (double)value);
    }
    return ctrl_send_obj(sock_path, o, resp, resp_size, timeout_ms);
}

int rss_ctrl_cmd_str(const char *sock_path, const char *cmd, const char *key, const char *value,
                     char *resp, int resp_size, int timeout_ms)
{
    cJSON *o = cJSON_CreateObject();
    if (o) {
        cJSON_AddStringToObject(o, "cmd", cmd);
        cJSON_AddStringToObject(o, key, value ? value : "");
    }
    return ctrl_send_obj(sock_path, o, resp, resp_size, timeout_ms);
}

bool rss_ctrl_resp_is_ok(const char *resp)
{
    if (!resp)
        return false;
    cJSON *parsed = cJSON_Parse(resp);
    const cJSON *status = parsed ? cJSON_GetObjectItem(parsed, "status") : NULL;
    bool ok = cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0;
    cJSON_Delete(parsed);
    return ok;
}

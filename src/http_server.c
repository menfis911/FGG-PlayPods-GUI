#include "http_server.h"

#include "backend.h"
#include "log.h"
#include "web_assets.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define REQUEST_MAX 8192
#define JSON_MAX 32768

static int appendf(char *buf, size_t size, int used, const char *fmt, ...)
{
    va_list ap;
    int written;
    if (used < 0 || (size_t)used >= size) return -1;
    va_start(ap, fmt);
    written = vsnprintf(buf + used, size - (size_t)used, fmt, ap);
    va_end(ap);
    if (written < 0 || (size_t)written >= size - (size_t)used) return -1;
    return used + written;
}

static int append_json_string(char *buf, size_t size, int used, const char *text)
{
    used = appendf(buf, size, used, "\"");
    if (used < 0) return -1;
    for (; text && *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') used = appendf(buf, size, used, "\\%c", c);
        else if (c == '\n') used = appendf(buf, size, used, "\\n");
        else if (c >= 0x20) used = appendf(buf, size, used, "%c", c);
        if (used < 0) return -1;
    }
    return appendf(buf, size, used, "\"");
}

static void format_mac(const unsigned char addr[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static int append_device(char *buf, size_t size, int used,
                         const bt_device *device, const char *state,
                         int saved)
{
    char mac[18];
    format_mac(device->addr, mac);
    used = appendf(buf, size, used, "{\"name\":");
    used = append_json_string(buf, size, used, device->name);
    used = appendf(buf, size, used,
                   ",\"mac\":\"%s\",\"rssi\":%d,\"state\":\"%s\","
                   "\"saved\":%s}", mac, (int)device->rssi, state,
                   saved ? "true" : "false");
    return used;
}

static int send_all(int fd, const void *data, size_t size)
{
    const char *p = data;
    while (size) {
        ssize_t n = send(fd, p, size, 0);
        if (n <= 0) return 0;
        p += n;
        size -= (size_t)n;
    }
    return 1;
}

static void respond(int fd, int code, const char *reason, const char *type,
                    const void *body, size_t length)
{
    char header[768];
    int n = snprintf(header, sizeof header,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Connection: close\r\n\r\n",
                     code, reason, type, length);
    if (n > 0) send_all(fd, header, (size_t)n);
    if (length) send_all(fd, body, length);
}

static void json_message(int fd, int code, const char *reason,
                         int accepted, const char *message)
{
    char body[384];
    int n = snprintf(body, sizeof body,
                     "{\"accepted\":%s,\"message\":\"%s\"}",
                     accepted ? "true" : "false", message);
    respond(fd, code, reason, "application/json; charset=utf-8", body, (size_t)n);
}

static void api_status(int fd)
{
    backend_snapshot state;
    char body[JSON_MAX];
    int used = 0;
    backend_get_snapshot(&state);
    used = appendf(body, sizeof body, used,
                   "{\"status\":\"%s\",\"controller\":%s,"
                   "\"scanning\":%s,\"connected\":%s,\"streaming\":%s,"
                   "\"deviceCount\":%d,\"error\":",
                   backend_status_name(state.status),
                   state.controller_ready ? "true" : "false",
                   state.status == BACKEND_SCANNING ? "true" : "false",
                   (state.status == BACKEND_CONNECTED ||
                    state.status == BACKEND_STREAMING ||
                    state.status == BACKEND_DISCONNECTING) ? "true" : "false",
                   state.status == BACKEND_STREAMING ? "true" : "false",
                   state.device_count);
    used = append_json_string(body, sizeof body, used, state.error);
    used = appendf(body, sizeof body, used, ",\"active\":");
    if (state.active_valid)
        used = append_device(body, sizeof body, used, &state.active,
                             backend_status_name(state.status),
                             state.saved_valid &&
                             memcmp(state.saved.addr, state.active.addr, 6) == 0);
    else
        used = appendf(body, sizeof body, used, "null");
    used = appendf(body, sizeof body, used, "}");
    if (used < 0) json_message(fd, 500, "Internal Server Error", 0, "JSON overflow");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)used);
}

static void api_devices(int fd)
{
    backend_snapshot state;
    char body[JSON_MAX];
    int used = 0;
    backend_get_snapshot(&state);
    used = appendf(body, sizeof body, used, "{\"devices\":[");
    for (int i = 0; i < state.device_count; ++i) {
        int is_active = state.active_valid &&
                        memcmp(state.active.addr, state.devices[i].addr, 6) == 0;
        int is_saved = state.saved_valid &&
                       memcmp(state.saved.addr, state.devices[i].addr, 6) == 0;
        if (i) used = appendf(body, sizeof body, used, ",");
        used = append_device(body, sizeof body, used, &state.devices[i],
                             is_active ? backend_status_name(state.status) : "available",
                             is_saved);
    }
    used = appendf(body, sizeof body, used, "]}");
    if (used < 0) json_message(fd, 500, "Internal Server Error", 0, "JSON overflow");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)used);
}

static void api_saved(int fd)
{
    backend_snapshot state;
    char body[1024];
    int used = 0;
    backend_get_snapshot(&state);
    used = appendf(body, sizeof body, used, "{\"devices\":[");
    if (state.saved_valid)
        used = append_device(body, sizeof body, used, &state.saved, "saved", 1);
    used = appendf(body, sizeof body, used, "]}");
    respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)used);
}

static int json_mac(const char *body, char mac[18])
{
    const char *p = strstr(body, "\"mac\"");
    const char *q;
    if (!p || !(p = strchr(p, ':'))) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != '"' || !(q = strchr(p, '"')) || q - p != 17) return 0;
    memcpy(mac, p, 17);
    mac[17] = '\0';
    return 1;
}

static void handle_request(int fd, char *request)
{
    char method[8], path[256];
    char *body = strstr(request, "\r\n\r\n");
    char *query;
    web_asset asset;
    if (sscanf(request, "%7s %255s", method, path) != 2) {
        json_message(fd, 400, "Bad Request", 0, "Malformed request");
        return;
    }
    body = body ? body + 4 : request + strlen(request);
    query = strchr(path, '?');
    if (query) *query = '\0';

    if (strcmp(method, "OPTIONS") == 0) {
        respond(fd, 204, "No Content", "text/plain", "", 0);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
        api_status(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/devices") == 0) {
        api_devices(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/saved") == 0) {
        api_saved(fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/scan") == 0) {
        int ok = backend_request_scan();
        json_message(fd, ok ? 202 : 409, ok ? "Accepted" : "Conflict", ok,
                     ok ? "Bluetooth scan queued" : "Backend is busy");
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/connect") == 0) {
        char mac[18];
        int ok = json_mac(body, mac) && backend_request_connect(mac);
        json_message(fd, ok ? 202 : 409, ok ? "Accepted" : "Conflict", ok,
                     ok ? "Connection queued" : "Unknown device or backend is busy");
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/disconnect") == 0) {
        int ok = backend_request_disconnect();
        json_message(fd, ok ? 202 : 409, ok ? "Accepted" : "Conflict", ok,
                     ok ? "Disconnect requested" : "No active connection");
    } else if (strcmp(method, "GET") == 0 && web_asset_find(path, &asset)) {
        respond(fd, 200, "OK", asset.content_type, asset.data, asset.size);
    } else {
        json_message(fd, 404, "Not Found", 0, "Not found");
    }
}

static void serve_client(int fd)
{
    char request[REQUEST_MAX + 1];
    size_t used = 0;
    for (;;) {
        ssize_t n = recv(fd, request + used, REQUEST_MAX - used, 0);
        if (n <= 0) break;
        used += (size_t)n;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n")) {
            const char *length = strstr(request, "Content-Length:");
            size_t need = 0;
            char *body = strstr(request, "\r\n\r\n") + 4;
            if (length) need = (size_t)strtoul(length + 15, NULL, 10);
            if (used >= (size_t)(body - request) + need) break;
        }
        if (used == REQUEST_MAX) break;
    }
    request[used] = '\0';
    handle_request(fd, request);
}

int http_server_run(unsigned short port, volatile int *running)
{
    int server;
    int reuse = 1;
    struct sockaddr_in addr;

    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) return 0;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(server, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(server, 8) != 0) {
        log_line("http: cannot listen on port %u errno=%d", port, errno);
        close(server);
        return 0;
    }
    log_line("http: Web GUI listening on port %u", port);

    while (*running) {
        fd_set reads;
        struct timeval timeout = { 0, 250000 };
        FD_ZERO(&reads);
        FD_SET(server, &reads);
        if (select(server + 1, &reads, NULL, NULL, &timeout) > 0) {
            int client = accept(server, NULL, NULL);
            if (client >= 0) {
                serve_client(client);
                close(client);
            }
        }
    }
    close(server);
    return 1;
}

#include "url.h"

static int host_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static int path_char(char c) {
    return c > ' ' && c < 0x7F && c != '?' && c != '#';
}

int jpr_url_parse(const char *url, struct jpr_url *u) {
    static const char scheme[] = "https://";
    u32 i, n = 0, k;
    for (i = 0; scheme[i]; i++)
        if (url[i] != scheme[i]) return -1;
    while (host_char(url[i])) {
        if (n + 1 >= JPR_HOST_MAX) return -1;
        u->host[n++] = url[i++];
    }
    u->host[n] = 0;
    if (!n) return -1;
    u->port = 443;
    if (url[i] == ':') {
        u32 port = 0, digits = 0;
        for (i++; url[i] >= '0' && url[i] <= '9'; i++) {
            if (++digits > 5) return -1;
            port = port * 10 + (u32)(url[i] - '0');
        }
        if (!digits || !port || port > 65535) return -1;
        u->port = port;
    }
    n = 0;
    if (url[i] == '/') {
        while (path_char(url[i])) {
            if (n + 1 >= JPR_PATH_MAX) return -1;
            u->path[n++] = url[i++];
        }
    }
    if (url[i]) return -1;
    while (n && u->path[n - 1] == '/') n--;
    u->path[n] = 0;
    for (k = 0; u->host[k]; k++) u->authority[k] = u->host[k];
    if (u->port != 443) {
        char digits[6];
        u32 p = u->port, d = 0;
        while (p) {
            digits[d++] = (char)('0' + p % 10);
            p /= 10;
        }
        u->authority[k++] = ':';
        while (d) u->authority[k++] = digits[--d];
    }
    u->authority[k] = 0;
    return 0;
}

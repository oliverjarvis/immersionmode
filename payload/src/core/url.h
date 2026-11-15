#ifndef JPR_URL_H
#define JPR_URL_H

#include "types.h"

#define JPR_HOST_MAX 64
#define JPR_PATH_MAX 64

/* Where kokoro is: its host (checked against the certificate), the Host header's value (with ":port"
 * when it is not 443), the port, and a path prefix without a trailing '/'. */
struct jpr_url {
    char host[JPR_HOST_MAX];
    char authority[JPR_HOST_MAX + 6];
    u32 port;
    char path[JPR_PATH_MAX];
};

/* "https://host[:port][/path]": 0, or -1 for another scheme, an empty or odd host, a bad port, or one part too long. */
int jpr_url_parse(const char *url, struct jpr_url *u);

#endif

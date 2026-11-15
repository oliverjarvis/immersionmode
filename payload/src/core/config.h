#ifndef JPR_CONFIG_H
#define JPR_CONFIG_H

#include "types.h"

#define JPR_URL_MAX 128
#define JPR_TOKEN_MAX 64
#define JPR_CONFIG_MAX 1024              /* bytes of config.txt read */

/* /jpreader/config.txt (spec 7.4). */
struct jpr_config {
    u32 debug;                           /* 1: show the status line */
    u32 voice;                           /* 1 (default): a story line's voice clip plays as its box opens */
    char kokoro_url[JPR_URL_MAX];        /* kokoro's site, "" when not linked (jpreader kokoro-link) */
    char kokoro_token[JPR_TOKEN_MAX];    /* the 3DS's kokoro device token, "" when not linked */
};

/* key=value lines; spaces around both trimmed, CR LF accepted, # comments and unknown keys ignored;
 * a value too long for its field is ignored. Older cards' api_key= and model= lines are ignored too. */
void jpr_config_parse(struct jpr_config *c, const char *text, u32 len);

#endif

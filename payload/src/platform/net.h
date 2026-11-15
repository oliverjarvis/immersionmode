#ifndef JPR_NET_H
#define JPR_NET_H

#include "core/types.h"

#define JPR_TICKS_PER_MS 268111u
struct jpr_deadline { s64 at; const volatile u32 *cancel; u32 id; };   /* at: system tick */
#define JPR_LIVE 0
#define JPR_EXPIRED 1
#define JPR_CANCELLED 2
int jpr_deadline_check(const struct jpr_deadline *d);                  /* JPR_LIVE / EXPIRED / CANCELLED */
void jpr_deadline_set(struct jpr_deadline *d, u32 ms, const volatile u32 *cancel, u32 id);
int jpr_service(const char *name, u32 *session);                        /* via srv:; 0 or -1 */
s32 jpr_call(u32 session);                                              /* send this thread's command; result */
#define JPR_EAGAIN (-6)
#define JPR_EINPROGRESS (-26)
#define JPR_EISCONN (-30)
s32 jpr_soc_call(u32 soc);                                              /* result: cmd[2] (count or -errno) */

u64 jpr_time_ms(void);                       /* milliseconds since 1900, from the shared page (0x1FF81000) */
/* ssl:C GenerateRandomData, mixed with the tick counter. 0, or -1 when ssl:C gave nothing: then the
 * bytes must not be used as a key seed. */
int jpr_random(u8 *out, u32 n);

#endif

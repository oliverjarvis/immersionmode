#include "core/capture.h"
#include "choices.h"
#include "screen.h"
#include "display.h"
#include "core/util.h"
#include "debug.h"
#include "game.h"
#include "kokoro.h"
#include "svc.h"
#include "worker.h"

extern u8 __jpr_bss_start[];
extern u8 __jpr_bss_end[];

/* First code in the payload image. The loader stub calls it before the game's own start-up. */
__attribute__((section(".text.jpr_init")))
void jpr_init(u32 protect_result) {
    s64 used = 0;
    u32 addr = 0;

    jpr_memset(__jpr_bss_start, 0, (u32)(__jpr_bss_end - __jpr_bss_start));
    jpr_cap_init();
    jpr_menu_init(&jpr_menu);
    jpr_screen_init(&jpr_screen);
    jpr_display_init();

    jpr_dbg.protect_result = protect_result;
    jpr_dbg.app_mem_total = *(volatile u32 *)CFGMEM_APPMEMALLOC;
    svcGetSystemInfo(&used, 0, 1);
    jpr_dbg.app_mem_used_init = (u32)used;

    /* Claim the socket buffer now, before the game sizes its own heaps. */
    jpr_dbg.sock_buf_result = (u32)svcControlMemory(&addr, JPR_SOCK_BUF_ADDR, 0, JPR_SOCK_BUF_SIZE,
                                                    MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE);
    jpr_dbg.sock_buf_addr = jpr_dbg.sock_buf_result == 0 ? addr : 0;
    /* The panel's own image: without it the panel draws straight into the game's buffer, slower. */
    jpr_dbg.panel_image_addr = svcControlMemory(&addr, JPR_PANEL_IMAGE_ADDR, 0, JPR_PANEL_IMAGE_SIZE, MEMOP_ALLOC,
                                                MEMPERM_READ | MEMPERM_WRITE) == 0 ? addr : 0;
    /* kokoro's word list and marks: without them the panel shows no marks. */
    jpr_dbg.kokoro_addr = svcControlMemory(&addr, JPR_KOKORO_ADDR, 0, JPR_KOKORO_SIZE, MEMOP_ALLOC,
                                           MEMPERM_READ | MEMPERM_WRITE) == 0 ? addr : 0;
    jpr_kokoro_init((void *)jpr_dbg.kokoro_addr);

    svcGetSystemInfo(&used, 0, 1);
    jpr_dbg.app_mem_used_now = (u32)used;
    jpr_dbg.link_result = (u32)&jpr_link.result;
    jpr_dbg.magic = JPR_DBG_MAGIC;
}

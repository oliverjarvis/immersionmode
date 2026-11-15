#include <stdio.h>
#include <string.h>

#include "core/ipc.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_srv(void) {
    u32 cmd[16] = {0};
    jpr_ipc_srv_register(cmd);
    CHECK(cmd[0] == 0x00010002 && cmd[1] == 0x20);
    jpr_ipc_srv_get_service(cmd, "fs:USER");
    CHECK(cmd[0] == 0x00050100);
    CHECK(cmd[1] == 0x553A7366);                   /* "fs:U" */
    CHECK(cmd[2] == 0x00524553);                   /* "SER\0" */
    CHECK(cmd[3] == 7 && cmd[4] == 0);
}

static void test_fs(void) {
    u32 cmd[16] = {0};
    jpr_ipc_fs_initialize(cmd);
    CHECK(cmd[0] == 0x08010002 && cmd[1] == 0x20);
    jpr_ipc_fs_open_file_directly(cmd, JPR_ARCHIVE_SDMC, 0x1000, 1, 0x2000, 19, JPR_FS_OPEN_READ);
    CHECK(cmd[0] == 0x08030204);
    CHECK(cmd[1] == 0 && cmd[2] == 9 && cmd[3] == 1 && cmd[4] == 1);
    CHECK(cmd[5] == 3 && cmd[6] == 19 && cmd[7] == 1 && cmd[8] == 0);
    jpr_ipc_fs_open_file_directly(cmd, JPR_ARCHIVE_SDMC, 0x1000, 1, 0x2000, 19, JPR_FS_OPEN_APPEND);
    CHECK(cmd[7] == 7);                            /* read, write, create */
    CHECK(cmd[9] == ((1u << 14) | (2u << 10) | 2) && cmd[10] == 0x1000);
    CHECK(cmd[11] == ((19u << 14) | 2) && cmd[12] == 0x2000);
}

static void test_file(void) {
    u32 cmd[16] = {0};
    jpr_ipc_file_read(cmd, 0, 0x20000, 0x0FEB0000);
    CHECK(cmd[0] == 0x080200C2);
    CHECK(cmd[1] == 0 && cmd[2] == 0 && cmd[3] == 0x20000);
    CHECK(cmd[4] == ((0x20000u << 4) | 0xC) && cmd[5] == 0x0FEB0000);
    jpr_ipc_file_write(cmd, 0x30, 12, 0x0FEB0000);
    CHECK(cmd[0] == 0x08030102);
    CHECK(cmd[1] == 0x30 && cmd[2] == 0 && cmd[3] == 12 && cmd[4] == 1);   /* flush */
    CHECK(cmd[5] == ((12u << 4) | 0xA) && cmd[6] == 0x0FEB0000);
    jpr_ipc_file_get_size(cmd);
    CHECK(cmd[0] == 0x08040000);
    jpr_ipc_file_set_size(cmd, 0);
    CHECK(cmd[0] == 0x08050080 && cmd[1] == 0 && cmd[2] == 0);
    jpr_ipc_file_close(cmd);
    CHECK(cmd[0] == 0x08080000);
    jpr_ipc_file_get_size(cmd);
    CHECK(cmd[0] == 0x08040000);
}

static void test_network_commands(void) {
    u32 cmd[16];
    memset(cmd, 0xEE, sizeof cmd);
    jpr_ipc_ac_get_wifi_status(cmd);
    CHECK(cmd[0] == 0x000D0000);
    jpr_ipc_ac_create_default_config(cmd);
    CHECK(cmd[0] == 0x00010000);
    jpr_ipc_ac_connect_async(cmd, 0x55, 0x1000);
    CHECK(cmd[0] == 0x00040006 && cmd[1] == 0x20 && cmd[3] == 0 && cmd[4] == 0x55 &&
          cmd[5] == ((0x200u << 14) | (1u << 10) | 2u) && cmd[6] == 0x1000);
    /* the sequence Ultra Moon's own nn::ac::Connect sends (code 0x165FC4..0x166754) */
    jpr_ipc_ac_set_client_version(cmd);
    CHECK(cmd[0] == 0x00400042 && cmd[1] == 0x0B0401C8 && cmd[2] == 0x20);
    jpr_ipc_ac_get_infra_priority(cmd, 0x1000);
    CHECK(cmd[0] == 0x00270002 && cmd[1] == 0x00800402 && cmd[2] == 0x1000);
    jpr_ipc_ac_set_request_eula_version(cmd, 0x1000);
    CHECK(cmd[0] == 0x002D0082 && cmd[1] == 0 && cmd[2] == 0 && cmd[3] == 0x00800002 && cmd[4] == 0x1000);
    jpr_ipc_ac_get_connect_result(cmd);
    CHECK(cmd[0] == 0x00050002 && cmd[1] == 0x20);
    jpr_ipc_ac_close_async(cmd, 0x56);
    CHECK(cmd[0] == 0x00080004 && cmd[1] == 0x20 && cmd[3] == 0 && cmd[4] == 0x56);
    jpr_ipc_ac_add_deny_ap_type(cmd, 0x1000);
    CHECK(cmd[0] == 0x00240042 && cmd[1] == 0x40 && cmd[2] == 0x00800002 && cmd[3] == 0x1000);
    jpr_ipc_ndm_enter_exclusive_state(cmd);
    CHECK(cmd[0] == 0x00010042 && cmd[1] == 1 && cmd[2] == 0x20);
    jpr_ipc_ndm_leave_exclusive_state(cmd);
    CHECK(cmd[0] == 0x00020002 && cmd[1] == 0x20);
    jpr_ipc_soc_initialize(cmd, 0x60000, 0x77);
    CHECK(cmd[0] == 0x00010044 && cmd[1] == 0x60000 && cmd[2] == 0x20 && cmd[4] == 0 && cmd[5] == 0x77);
    jpr_ipc_soc_shutdown(cmd);
    CHECK(cmd[0] == 0x00190000);
    jpr_ipc_soc_socket(cmd);
    CHECK(cmd[0] == 0x000200C2 && cmd[1] == 2 && cmd[2] == 1 && cmd[3] == 0 && cmd[4] == 0x20);
    jpr_ipc_soc_fcntl(cmd, 3, 4, 4);
    CHECK(cmd[0] == 0x001300C2 && cmd[1] == 3 && cmd[2] == 4 && cmd[3] == 4 && cmd[4] == 0x20);
    jpr_ipc_soc_connect(cmd, 3, 0x2000);
    CHECK(cmd[0] == 0x00060084 && cmd[2] == 8 && cmd[3] == 0x20 && cmd[5] == ((8u << 14) | 2u) && cmd[6] == 0x2000);
    jpr_ipc_soc_gethostbyname(cmd, 0x3000, 14, 0x1A88);
    CHECK(cmd[0] == 0x000D0082 && cmd[1] == 14 && cmd[2] == 0x1A88 && cmd[3] == ((14u << 14) | 0xC02u) &&
          cmd[4] == 0x3000);
    jpr_ipc_soc_poll(cmd, 0x4000, 1, 100);
    CHECK(cmd[0] == 0x00140084 && cmd[1] == 1 && cmd[2] == 100 && cmd[3] == 0x20 &&
          cmd[5] == ((12u << 14) | (10u << 10) | 2u) && cmd[6] == 0x4000);
    jpr_ipc_soc_recv(cmd, 3, 0x5000, 512);
    CHECK(cmd[0] == 0x00070104 && cmd[2] == 512 && cmd[4] == 0 && cmd[5] == 0x20 &&
          cmd[7] == ((512u << 4) | 0xCu) && cmd[8] == 0x5000);
    jpr_ipc_soc_send(cmd, 3, 0x6000, 100);
    CHECK(cmd[0] == 0x00090106 && cmd[2] == 100 && cmd[5] == 0x20 && cmd[7] == ((0u << 14) | (1u << 10) | 2u) &&
          cmd[9] == ((100u << 4) | 0xAu) && cmd[10] == 0x6000);
    jpr_ipc_soc_close(cmd, 3);
    CHECK(cmd[0] == 0x000B0042 && cmd[1] == 3 && cmd[2] == 0x20);
    jpr_ipc_ssl_initialize(cmd);
    CHECK(cmd[0] == 0x00010002 && cmd[1] == 0x20);
    jpr_ipc_ssl_random(cmd, 0x7000, 32);
    CHECK(cmd[0] == 0x00110042 && cmd[1] == 32 && cmd[2] == ((32u << 4) | 0xCu) && cmd[3] == 0x7000);
}

int main(void) {
    test_network_commands();
    test_srv();
    test_fs();
    test_file();
    if (failures) {
        printf("ipc: %d check(s) failed\n", failures);
        return 1;
    }
    printf("ipc: all checks passed\n");
    return 0;
}

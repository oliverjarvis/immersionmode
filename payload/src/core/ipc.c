#include "ipc.h"

#define HEADER(id, normal, translate) (((u32)(id) << 16) | ((u32)(normal) << 6) | (u32)(translate))
#define DESC_PROCESS_ID 0x20u
#define DESC_STATIC(size, index) (((u32)(size) << 14) | ((u32)(index) << 10) | 0x2u)
#define DESC_BUFFER_W(size) (((u32)(size) << 4) | 0xCu)
#define DESC_BUFFER_R(size) (((u32)(size) << 4) | 0xAu)

void jpr_ipc_srv_register(u32 *cmd) {
    cmd[0] = HEADER(0x1, 0, 2);
    cmd[1] = DESC_PROCESS_ID;
    cmd[2] = 0;
}

void jpr_ipc_srv_get_service(u32 *cmd, const char *name) {
    u8 packed[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    u32 len = 0;
    while (len < 8 && name[len]) {
        packed[len] = (u8)name[len];
        len++;
    }
    cmd[0] = HEADER(0x5, 4, 0);
    cmd[1] = (u32)packed[0] | ((u32)packed[1] << 8) | ((u32)packed[2] << 16) | ((u32)packed[3] << 24);
    cmd[2] = (u32)packed[4] | ((u32)packed[5] << 8) | ((u32)packed[6] << 16) | ((u32)packed[7] << 24);
    cmd[3] = len;
    cmd[4] = 0; /* wait for the service */
}

void jpr_ipc_fs_initialize(u32 *cmd) {
    cmd[0] = HEADER(0x801, 0, 2);
    cmd[1] = DESC_PROCESS_ID;
    cmd[2] = 0;
}

void jpr_ipc_fs_open_file_directly(u32 *cmd, u32 archive, u32 archive_path, u32 archive_path_size,
                                   u32 file_path, u32 file_path_size, u32 open_flags) {
    cmd[0] = HEADER(0x803, 8, 4);
    cmd[1] = 0; /* transaction */
    cmd[2] = archive;
    cmd[3] = JPR_PATH_EMPTY;
    cmd[4] = archive_path_size;
    cmd[5] = JPR_PATH_ASCII;
    cmd[6] = file_path_size;
    cmd[7] = open_flags;
    cmd[8] = 0; /* attributes */
    cmd[9] = DESC_STATIC(archive_path_size, 2);
    cmd[10] = archive_path;
    cmd[11] = DESC_STATIC(file_path_size, 0);
    cmd[12] = file_path;
}

void jpr_ipc_file_read(u32 *cmd, u32 offset, u32 size, u32 buffer) {
    cmd[0] = HEADER(0x802, 3, 2);
    cmd[1] = offset;
    cmd[2] = 0; /* offset, high word */
    cmd[3] = size;
    cmd[4] = DESC_BUFFER_W(size);
    cmd[5] = buffer;
}

void jpr_ipc_file_write(u32 *cmd, u32 offset, u32 size, u32 buffer) {
    cmd[0] = HEADER(0x803, 4, 2);
    cmd[1] = offset;
    cmd[2] = 0; /* offset, high word */
    cmd[3] = size;
    cmd[4] = 1; /* flush */
    cmd[5] = DESC_BUFFER_R(size);
    cmd[6] = buffer;
}

void jpr_ipc_file_get_size(u32 *cmd) { cmd[0] = HEADER(0x804, 0, 0); }

void jpr_ipc_file_set_size(u32 *cmd, u32 size) {
    cmd[0] = HEADER(0x805, 2, 0);
    cmd[1] = size;
    cmd[2] = 0;
}

void jpr_ipc_file_close(u32 *cmd) {
    cmd[0] = HEADER(0x808, 0, 0);
}

void jpr_ipc_ac_create_default_config(u32 *cmd) { cmd[0] = HEADER(0x1, 0, 0); }

void jpr_ipc_ac_connect_async(u32 *cmd, u32 event, u32 config) {
    cmd[0] = HEADER(0x4, 0, 6);
    cmd[1] = DESC_PROCESS_ID;
    cmd[2] = 0;
    cmd[3] = 0;                 /* one handle, copied */
    cmd[4] = event;
    cmd[5] = DESC_STATIC(0x200, 1);
    cmd[6] = config;
}

void jpr_ipc_ac_get_wifi_status(u32 *cmd) { cmd[0] = HEADER(0xD, 0, 0); }

/* The rest of the connection sequence, as Ultra Moon's own nn::ac::Connect sends it. On a 3DS the
 * Wi-Fi service answers nothing useful until the client version is set, and a connection only comes
 * up once the background services have handed over the radio (ndm's exclusive state). */
void jpr_ipc_ac_get_connect_result(u32 *cmd) {
    cmd[0] = HEADER(0x5, 0, 2);
    cmd[1] = DESC_PROCESS_ID;
    cmd[2] = 0;
}

void jpr_ipc_ac_close_async(u32 *cmd, u32 event) {
    cmd[0] = HEADER(0x8, 0, 4);
    cmd[1] = DESC_PROCESS_ID;
    cmd[2] = 0;
    cmd[3] = 0;                 /* one handle, copied */
    cmd[4] = event;
}

void jpr_ipc_ac_get_infra_priority(u32 *cmd, u32 config) {
    cmd[0] = HEADER(0x27, 0, 2);
    cmd[1] = DESC_STATIC(0x200, 1);
    cmd[2] = config;
}

void jpr_ipc_ac_set_request_eula_version(u32 *cmd, u32 config) {
    cmd[0] = HEADER(0x2D, 2, 2);
    cmd[1] = 0;                 /* major */
    cmd[2] = 0;                 /* minor */
    cmd[3] = DESC_STATIC(0x200, 0);
    cmd[4] = config;
}

void jpr_ipc_ac_set_client_version(u32 *cmd) {
    cmd[0] = HEADER(0x40, 1, 2);
    cmd[1] = 0x0B0401C8;        /* the game's own */
    cmd[2] = DESC_PROCESS_ID;
    cmd[3] = 0;
}

void jpr_ipc_ac_add_deny_ap_type(u32 *cmd, u32 config) {
    cmd[0] = HEADER(0x24, 1, 2);
    cmd[1] = 0x40;              /* as the game, for a config with a nonzero infra priority */
    cmd[2] = DESC_STATIC(0x200, 0);
    cmd[3] = config;
}

void jpr_ipc_ndm_enter_exclusive_state(u32 *cmd) {
    cmd[0] = HEADER(0x1, 1, 2);
    cmd[1] = 1;                 /* infrastructure */
    cmd[2] = DESC_PROCESS_ID;
    cmd[3] = 0;
}

void jpr_ipc_ndm_leave_exclusive_state(u32 *cmd) {
    cmd[0] = HEADER(0x2, 0, 2);
    cmd[1] = DESC_PROCESS_ID;
    cmd[2] = 0;
}

void jpr_ipc_soc_initialize(u32 *cmd, u32 size, u32 block) {
    cmd[0] = HEADER(0x1, 1, 4);
    cmd[1] = size;
    cmd[2] = DESC_PROCESS_ID;
    cmd[3] = 0;
    cmd[4] = 0;                 /* one handle, copied */
    cmd[5] = block;
}

void jpr_ipc_soc_shutdown(u32 *cmd) { cmd[0] = HEADER(0x19, 0, 0); }

void jpr_ipc_soc_socket(u32 *cmd) {
    cmd[0] = HEADER(0x2, 3, 2);
    cmd[1] = 2;                 /* AF_INET */
    cmd[2] = 1;                 /* SOCK_STREAM */
    cmd[3] = 0;
    cmd[4] = DESC_PROCESS_ID;
}

void jpr_ipc_soc_fcntl(u32 *cmd, u32 fd, u32 op, u32 arg) {
    cmd[0] = HEADER(0x13, 3, 2);
    cmd[1] = fd;
    cmd[2] = op;
    cmd[3] = arg;
    cmd[4] = DESC_PROCESS_ID;
}

void jpr_ipc_soc_connect(u32 *cmd, u32 fd, u32 addr) {
    cmd[0] = HEADER(0x6, 2, 4);
    cmd[1] = fd;
    cmd[2] = 8;
    cmd[3] = DESC_PROCESS_ID;
    cmd[4] = 0;
    cmd[5] = DESC_STATIC(8, 0);
    cmd[6] = addr;
}

void jpr_ipc_soc_gethostbyname(u32 *cmd, u32 name, u32 name_size, u32 out_size) {
    cmd[0] = HEADER(0xD, 2, 2);
    cmd[1] = name_size;
    cmd[2] = out_size;
    cmd[3] = DESC_STATIC(name_size, 3);
    cmd[4] = name;
}

void jpr_ipc_soc_poll(u32 *cmd, u32 fds, u32 nfds, u32 timeout_ms) {
    cmd[0] = HEADER(0x14, 2, 4);
    cmd[1] = nfds;
    cmd[2] = timeout_ms;
    cmd[3] = DESC_PROCESS_ID;
    cmd[4] = 0;
    cmd[5] = DESC_STATIC(12 * nfds, 10);
    cmd[6] = fds;
}

void jpr_ipc_soc_recv(u32 *cmd, u32 fd, u32 buf, u32 len) {
    cmd[0] = HEADER(0x7, 4, 4);
    cmd[1] = fd;
    cmd[2] = len;
    cmd[3] = 0;
    cmd[4] = 0;                 /* no source address wanted */
    cmd[5] = DESC_PROCESS_ID;
    cmd[6] = 0;
    cmd[7] = DESC_BUFFER_W(len);
    cmd[8] = buf;
}

void jpr_ipc_soc_send(u32 *cmd, u32 fd, u32 buf, u32 len) {
    cmd[0] = HEADER(0x9, 4, 6);
    cmd[1] = fd;
    cmd[2] = len;
    cmd[3] = 0;
    cmd[4] = 0;
    cmd[5] = DESC_PROCESS_ID;
    cmd[6] = 0;
    cmd[7] = DESC_STATIC(0, 1);
    cmd[8] = 0;
    cmd[9] = DESC_BUFFER_R(len);
    cmd[10] = buf;
}

void jpr_ipc_soc_close(u32 *cmd, u32 fd) {
    cmd[0] = HEADER(0xB, 1, 2);
    cmd[1] = fd;
    cmd[2] = DESC_PROCESS_ID;
}

void jpr_ipc_ssl_initialize(u32 *cmd) {
    cmd[0] = HEADER(0x1, 0, 2);
    cmd[1] = DESC_PROCESS_ID;
}

void jpr_ipc_ssl_random(u32 *cmd, u32 buf, u32 size) {
    cmd[0] = HEADER(0x11, 1, 2);
    cmd[1] = size;
    cmd[2] = DESC_BUFFER_W(size);
    cmd[3] = buf;
}

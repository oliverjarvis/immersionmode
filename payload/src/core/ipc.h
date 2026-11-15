#ifndef JPR_IPC_H
#define JPR_IPC_H

#include "types.h"

#define JPR_ARCHIVE_SDMC 9
#define JPR_PATH_EMPTY 1
#define JPR_PATH_ASCII 3
#define JPR_FS_OPEN_READ 1
#define JPR_FS_OPEN_APPEND 7     /* read, write, create */

/* Each fills in an IPC command buffer; addresses are passed as numbers. */
void jpr_ipc_srv_register(u32 *cmd);
void jpr_ipc_srv_get_service(u32 *cmd, const char *name);
void jpr_ipc_fs_initialize(u32 *cmd);
void jpr_ipc_fs_open_file_directly(u32 *cmd, u32 archive, u32 archive_path, u32 archive_path_size,
                                   u32 file_path, u32 file_path_size, u32 open_flags);
void jpr_ipc_file_read(u32 *cmd, u32 offset, u32 size, u32 buffer);
void jpr_ipc_file_close(u32 *cmd);
void jpr_ipc_file_write(u32 *cmd, u32 offset, u32 size, u32 buffer);     /* flushed */
void jpr_ipc_file_get_size(u32 *cmd);                                    /* size in cmd[2..3] */
void jpr_ipc_file_set_size(u32 *cmd, u32 size);

/* Network services (spec 9.3). */
void jpr_ipc_ac_create_default_config(u32 *cmd);                         /* 0x00010000 */
void jpr_ipc_ac_connect_async(u32 *cmd, u32 event, u32 config);           /* 0x00040006 */
void jpr_ipc_ac_get_wifi_status(u32 *cmd);                                /* 0x000D0000 */
void jpr_ipc_ac_get_connect_result(u32 *cmd);                             /* 0x00050002 */
void jpr_ipc_ac_close_async(u32 *cmd, u32 event);                         /* 0x00080004 */
void jpr_ipc_ac_get_infra_priority(u32 *cmd, u32 config);                 /* 0x00270002 */
void jpr_ipc_ac_set_request_eula_version(u32 *cmd, u32 config);          /* 0x002D0082, version 0.0 */
void jpr_ipc_ac_set_client_version(u32 *cmd);                             /* 0x00400042 */
void jpr_ipc_ac_add_deny_ap_type(u32 *cmd, u32 config);                   /* 0x00240042, type 0x40 */
void jpr_ipc_ndm_enter_exclusive_state(u32 *cmd);                         /* 0x00010042, infrastructure */
void jpr_ipc_ndm_leave_exclusive_state(u32 *cmd);                         /* 0x00020002 */
void jpr_ipc_soc_initialize(u32 *cmd, u32 size, u32 block);               /* 0x00010044 */
void jpr_ipc_soc_shutdown(u32 *cmd);                                      /* 0x00190000 */
void jpr_ipc_soc_socket(u32 *cmd);                                        /* 0x000200C2: AF_INET, SOCK_STREAM */
void jpr_ipc_soc_fcntl(u32 *cmd, u32 fd, u32 op, u32 arg);                /* 0x001300C2 */
void jpr_ipc_soc_connect(u32 *cmd, u32 fd, u32 addr);                     /* 0x00060084, 8-byte address */
void jpr_ipc_soc_gethostbyname(u32 *cmd, u32 name, u32 name_size, u32 out_size); /* 0x000D0082 */
void jpr_ipc_soc_poll(u32 *cmd, u32 fds, u32 nfds, u32 timeout_ms);       /* 0x00140084 */
void jpr_ipc_soc_recv(u32 *cmd, u32 fd, u32 buf, u32 len);                /* 0x00070104 */
void jpr_ipc_soc_send(u32 *cmd, u32 fd, u32 buf, u32 len);                /* 0x00090106 */
void jpr_ipc_soc_close(u32 *cmd, u32 fd);                                 /* 0x000B0042 */
void jpr_ipc_ssl_initialize(u32 *cmd);                                    /* 0x00010002 */
void jpr_ipc_ssl_random(u32 *cmd, u32 buf, u32 size);                     /* 0x00110042 */

#endif

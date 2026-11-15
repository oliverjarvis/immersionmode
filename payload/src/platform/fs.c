#include "fs.h"
#include "core/ipc.h"
#include "svc.h"

static u32 fs_session;

/* Sends the command in this thread's buffer; returns the kernel's or the service's result. */
static s32 call(u32 session) {
    s32 r = svcSendSyncRequest(session);
    return r ? r : (s32)jpr_cmdbuf()[1];
}

/* An fs:USER session, opened once and kept. */
static int session(void) {
    u32 *cmd = jpr_cmdbuf();
    u32 srv = 0;
    int err = 0;
    if (fs_session) return 0;
    if (svcConnectToPort(&srv, "srv:")) return JPR_FS_ERR_SRV;
    jpr_ipc_srv_register(cmd);
    if (call(srv)) {
        err = JPR_FS_ERR_SRV;
        goto done;
    }
    jpr_ipc_srv_get_service(cmd, "fs:USER");
    if (call(srv)) {
        err = JPR_FS_ERR_FS;
        goto done;
    }
    fs_session = cmd[3];
    jpr_ipc_fs_initialize(cmd);
    if (call(fs_session)) {
        svcCloseHandle(fs_session);
        fs_session = 0;
        err = JPR_FS_ERR_FS;
    }
done:
    svcCloseHandle(srv);
    return err;
}

static int open_with(const char *path, u32 *file, u32 flags) {
    static const char empty[1] = {0};
    u32 *cmd = jpr_cmdbuf();
    u32 size = 0;
    int err = session();
    if (err) return err;
    while (path[size]) size++;
    jpr_ipc_fs_open_file_directly(cmd, JPR_ARCHIVE_SDMC, (u32)empty, 1, (u32)path, size + 1, flags);
    if (call(fs_session)) return JPR_FS_ERR_OPEN;
    *file = cmd[3];
    return 0;
}

int jpr_fs_open(const char *path, u32 *file) { return open_with(path, file, JPR_FS_OPEN_READ); }

int jpr_fs_read(u32 file, u32 offset, void *buf, u32 size, u32 *len) {
    u32 *cmd = jpr_cmdbuf();
    jpr_ipc_file_read(cmd, offset, size, (u32)buf);
    if (call(file)) return JPR_FS_ERR_READ;
    *len = cmd[2];
    return 0;
}

int jpr_fs_size(u32 file, u32 *size) {
    u32 *cmd = jpr_cmdbuf();
    jpr_ipc_file_get_size(cmd);
    if (call(file)) return JPR_FS_ERR_READ;
    *size = cmd[3] ? 0xFFFFFFFFu : cmd[2];
    return 0;
}

void jpr_fs_close(u32 file) {
    u32 *cmd = jpr_cmdbuf();
    jpr_ipc_file_close(cmd);
    call(file);
    svcCloseHandle(file);
}

int jpr_fs_read_file(const char *path, void *buf, u32 max, u32 *len) {
    u32 file = 0;
    int err = jpr_fs_open(path, &file);
    if (err) return err;
    err = jpr_fs_read(file, 0, buf, max, len);
    jpr_fs_close(file);
    return err;
}

int jpr_fs_append(const char *path, const void *data, u32 size) {
    u32 *cmd = jpr_cmdbuf(), file = 0, end = 0;
    int err = open_with(path, &file, JPR_FS_OPEN_APPEND);
    if (err) return err;
    jpr_ipc_file_get_size(cmd);
    if (call(file)) {               /* without the size, a write could land over the log's start */
        jpr_fs_close(file);
        return JPR_FS_ERR_READ;
    }
    end = cmd[3] ? 0xFFFFFFFFu : cmd[2];
    if (end > 0x10000) {
        jpr_ipc_file_set_size(cmd, 0);
        call(file);
        end = 0;
    }
    jpr_ipc_file_write(cmd, end, size, (u32)data);
    err = call(file) ? JPR_FS_ERR_READ : 0;
    jpr_fs_close(file);
    return err;
}

int jpr_fs_write_at(const char *path, u32 offset, const void *data, u32 size) {
    u32 *cmd = jpr_cmdbuf(), file = 0;
    int err = open_with(path, &file, JPR_FS_OPEN_APPEND);   /* read, write, create */
    if (err) return err;
    jpr_ipc_file_write(cmd, offset, size, (u32)data);
    err = call(file) ? JPR_FS_ERR_READ : 0;
    jpr_fs_close(file);
    return err;
}

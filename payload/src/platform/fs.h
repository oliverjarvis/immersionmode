#ifndef JPR_FS_H
#define JPR_FS_H

#include "core/types.h"

#define JPR_FS_ERR_SRV 1  /* could not talk to the service manager */
#define JPR_FS_ERR_FS 2   /* could not open an fs:USER session */
#define JPR_FS_ERR_OPEN 3 /* the file could not be opened (usually: it is not on the SD card) */
#define JPR_FS_ERR_READ 4

/* Opens an SD card file for reading; the fs:USER session it needs is opened once and kept.
 * Returns 0 or a JPR_FS_ERR_* code. */
int jpr_fs_open(const char *path, u32 *file);
/* Reads up to `size` bytes at `offset`; *len says how many came (fewer at the end of the file). */
int jpr_fs_read(u32 file, u32 offset, void *buf, u32 size, u32 *len);
void jpr_fs_close(u32 file);
/* The file's size in bytes (files past 4 GB do not occur here). Returns 0 or JPR_FS_ERR_READ. */
int jpr_fs_size(u32 file, u32 *size);
/* Reads up to `max` bytes of an SD card file into buf. Returns 0 or a JPR_FS_ERR_* code. */
int jpr_fs_read_file(const char *path, void *buf, u32 max, u32 *len);
/* Adds data to the end of an SD card file, creating it; a file past 64 KB starts again from empty. */
int jpr_fs_append(const char *path, const void *data, u32 size);
/* Writes data at offset in an SD card file, creating it; the rest of the file stays. 0 or JPR_FS_ERR_*. */
int jpr_fs_write_at(const char *path, u32 offset, const void *data, u32 size);

#endif

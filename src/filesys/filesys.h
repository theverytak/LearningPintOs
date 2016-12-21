#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

// path관련 동적할당을 피하기 위하여
#define PATH_MAX_LEN 256
/* Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
bool filesys_create_dir (const char *name);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);
struct dir *parse_path (char *path_name, char *file_name);

#endif /* filesys/filesys.h */

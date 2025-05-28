#ifndef COURSEWORK_FUSE_FUNCTIONS_H
#define COURSEWORK_FUSE_FUNCTIONS_H

#include "fsinfo.h"
#include "header.h"
#include "fat.h"
#include "fs_state.h"
#include "direntry.h"
#include "util_functions.h"

void* sfsInit (struct fuse_conn_info *conn);
void sfsDestroy(void *private_data);
int sfsCreate (const char* path, mode_t mode, struct fuse_file_info* fileInfo);
int sfsMkdir (const char* path, mode_t mode);
int sfsGetattr(const char* path, struct stat* buf);
int sfsOpen (const char* path, struct fuse_file_info* fi);
int sfsRead (const char* path, char* buffer, size_t bytes, off_t offset, struct fuse_file_info* fi);
int sfsWrite (const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
int sfsReaddir (const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi);
int sfsTruncate(const char* path, off_t size);
int sfsUtimens (const char* path, const struct timespec tv[2]);
int sfsRelease (const char* path, struct fuse_file_info* fi);
int sfsUnlink (const char* path);
int sfsRmdir (const char* path);
#endif //COURSEWORK_FUSE_FUNCTIONS_H

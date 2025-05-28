#include "header.h"
#include "fsinfo.h"
#include "fat.h"
#include "fuse_functions.h"
#include "fs_state.h"

FSInfo fsInfo;
FsState state;

static const struct fuse_operations operations = {
    .init = sfsInit,
    .destroy = sfsDestroy,
    .create = sfsCreate,
    .mkdir = sfsMkdir,
    .getattr = sfsGetattr,
    .open = sfsOpen,
    .read = sfsRead,
    .write = sfsWrite,
    .readdir = sfsReaddir,
    .truncate = sfsTruncate,
    .utimens = sfsUtimens,
    .release = sfsRelease,
    .unlink = sfsUnlink,
    .rmdir = sfsRmdir
};

int main(int argc, char* argv[])
{
    return fuse_main(argc, argv, &operations, NULL);
}


#ifndef COURSEWORK_FS_STATE_H
#define COURSEWORK_FS_STATE_H
#include <limits.h>
#include <stdio.h>

typedef struct FsState {
    char pathToContainer[PATH_MAX];
    FILE* container;
} FsState;

#endif //COURSEWORK_FS_STATE_H

#define FUSE_USE_VERSION 35
#include <fuse/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <fuse/fuse_common.h>
#include <math.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#define MAX_DEPTH 10
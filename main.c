/*#define FUSE_USE_VERSION 35
#include <fuse/fuse.h>
#include <stdio.h>*/

#include "header.h"

static const struct fuse_operations operations = {

};

int main(int argc, char* argv[])
{
    
    return fuse_main(argc, argv, &operations, NULL);
}


#ifndef MESSAGE_H
#define MESSAGE_H

#include "header.h"

typedef struct {
    char type;
    short hash;
    char size;
    char* data;
} Message;

int calculateHash(const char *data, int length);
#endif
#ifndef MESSAGE_H
#define MESSAGE_H

#include "header.h"

typedef struct {
    char type;
    unsigned short hash;
    unsigned char size;
    char data[260];
} Message;

unsigned short calculateHash(const char *data, int length);
#endif
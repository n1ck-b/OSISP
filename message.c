#include "header.h"
#include "message.h"

unsigned short calculateHash(const char *data, int length) {
    unsigned short hash = 0x1DC5;
    for (int i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= 0x193;
    }
    return hash;
}

#include "header.h"
#include "message.h"

int calculateHash(const char *data, int length) {
    int hash = 0x811C9DC5;
    for (int i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= 0x01000193;
    }
    return hash;
}

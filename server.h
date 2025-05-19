#ifndef LAB8_SERVER_H
#define LAB8_SERVER_H

#define INFO_FILE_PATH "./server_info.txt"

#include "libs.h"

typedef struct {
    int clientSocket;
    char currentDir[PATH_MAX];
} ClientConnection;

#endif //LAB8_SERVER_H

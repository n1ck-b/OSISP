#ifndef SEMAPHORES_H
#define SEMAPHORES_H

#include "header.h"
#include "messagesQueue.h"

void increaseSemaphore(int semId);
void decreaseSemaphore(int semId);
int getValueOfSemaphore(int semId);
#endif
#ifndef MESSAGES_QUEUE
#define MESSAGES_QUEUE

#include "message.h"
#define SIZE 50// Размер очереди

typedef struct {
    Message messages[SIZE];
    int front, rear;
    int addedMessages;
    int retrievedMessages;
    int size;
    int freeSpace;
} MessagesQueue;

void initQueue(MessagesQueue *q, int semFreeSpace, int semAddedMessages);

int isFull(MessagesQueue *q);

int isEmpty(MessagesQueue *q);

void push(MessagesQueue *q, Message message);

Message pop(MessagesQueue *q);

void clearQueue(MessagesQueue *q);
#endif
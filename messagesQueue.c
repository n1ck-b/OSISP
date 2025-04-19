#include "header.h"
#include "messagesQueue.h"

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short  *array;
};

void initQueue(MessagesQueue *q, int semFreeSpace, int semAddedMessages) {
    q->front = -1;
    q->rear = -1;
    q->size = 0;
    q->addedMessages = semAddedMessages;
    q->freeSpace = semFreeSpace;
    q->retrievedMessages = 0;
    union semun arg;
    arg.val = 0;
    semctl(semAddedMessages, 0, SETVAL, arg);
    arg.val = SIZE;
    semctl(semFreeSpace, 0, SETVAL, arg);
}

int isFull(MessagesQueue *q) {
    //return (q->rear + 1) % SIZE == q->front;
    return q->size == SIZE;
}

int isEmpty(MessagesQueue *q) {
    //return q->front == -1;
    return q->size == 0;
}

void push(MessagesQueue *q, Message message) {
    //printf("\ninside push message: data=%s, size=%d, hash=%x\n", message.data, message.size, message.hash);
    if(isFull(q)) {
        printf("Очередь заполнена\n");
        return;
    }
    else {
        if (isEmpty(q)) {
            q->front = 0;
        }
        q->rear = (q->rear + 1) % SIZE;
        q->messages[q->rear] = message;
        q->size += 1;
        //q->addedMessages += 1;
        //q->freeSpace -= 1;
    }
}

Message pop(MessagesQueue *q) {
    if(isEmpty(q)) {
        printf("Очередь пуста\n");
        Message emptyMessage = {0};
        return emptyMessage;
    }
    //printf("\ninside pop\n");
    Message message = q->messages[q->front];
//    if(message.data==NULL) {
//        printf("\nmessage data is null\n");
//    }
    //printf("\ninside pop message: data=%s, size=%d, hash=%x\n", message.data, message.size, message.hash);
    if (q->front == q->rear) {
        q->front = q->rear = -1;
    } else {
        q->front = (q->front + 1) % SIZE;
    }
    q->size -= 1;
    q->retrievedMessages += 1;
    //q->freeSpace += 1;
    return message;
}

void clearQueue(MessagesQueue *q) {
    // Сброс указателей
    q->front = -1;
    q->rear = -1;
    q->size = 0;
    q->retrievedMessages = 0;
    q->addedMessages = 1;
    q->freeSpace = -1;
}

//void display(MessagesQueue *q) {
//    printf("Очередь: ");
//    int i = q->front;
//    while (1) {
//        printf("%d ", q->messages[i]);
//        if (i == q->rear) break;
//        i = (i + 1) % SIZE;
//    }
//    printf("\n");
//}
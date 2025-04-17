#include "header.h"
#include "messagesQueue.h"

void initQueue(MessagesQueue *q, int semFreeSpace, int semAddedMessages) {
    q->front = -1;
    q->rear = -1;
    q->size = 0;
    q->addedMessages = semAddedMessages;
    q->freeSpace = semFreeSpace;
    q->retrievedMessages = 0;
    semctl(semAddedMessages, 0, SETVAL, 0);
    semctl(semFreeSpace, 0, SETVAL, SIZE);
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
    Message message = q->messages[q->front];
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
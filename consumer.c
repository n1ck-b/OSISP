#include "consumer.h"
#include "messagesQueue.h"
#include "semaphores.h"
#include "message.h"

extern pid_t* consumers;
extern MessagesQueue* queue;
extern int numOfConsumers;
extern pthread_mutex_t mutex;

int createNewConsumer() {
    printf("Количество потребителей: %d\n", numOfConsumers);
    switch(consumers[numOfConsumers] = fork()) {
        case -1:
            printf("Ошибка создания процесса-потребителя: %d\n", errno);
            return -1;
        case 0: //процесс потомок
            break;
        default:
            numOfConsumers++;
            consumers = (pid_t*)realloc(consumers, numOfConsumers * sizeof(pid_t));
            return 0;
    }
    while(1) {
        printf("While consumer\n");
        Message message;
        decreaseSemaphore(queue->addedMessages);
        pthread_mutex_lock(&mutex);
        printf("consumer got mutex");
        message = pop(queue);
        pthread_mutex_unlock(&mutex);
        increaseSemaphore(queue->freeSpace);
        consumeMessage(&message);
        printf("Процессом pid = %d извлечено сообщение hash = %x\n. Количество сообщений в очереди = %d\n", getpid(), message.hash, queue->retrievedMessages);
        sleep(3);
    }
}

void consumeMessage(Message* message) {
    int messageHash = message->hash;
    message->hash = 0;
    int calculatedHash = calculateHash(message->data, message->size + 1);
    if(messageHash != calculatedHash) {
        printf("Вычисленный hash = %x не соответствует hash в сообщении = %x", calculatedHash, messageHash);
    }
    message->hash = messageHash;
}

void deleteConsumer() {
    if(numOfConsumers == 0) {
        printf("Нет активных процессов-потребителей\n");
        return;
    }
    pid_t temp = consumers[numOfConsumers];
    numOfConsumers--;
    consumers = (pid_t*)realloc(consumers, numOfConsumers*sizeof(pid_t));
    kill(temp, SIGTERM);
    wait(NULL);
    printf("Процесс-потребитель pid = %d удален\n", temp);
}
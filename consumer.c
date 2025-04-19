#include "consumer.h"
#include "messagesQueue.h"
#include "semaphores.h"
#include "message.h"

extern pid_t* consumers;
extern MessagesQueue* queue;
extern int numOfConsumers;
extern pthread_mutex_t *mutex;

int createNewConsumer() {
    printf("\nКоличество потребителей: %d\n", numOfConsumers + 1);
    switch(consumers[numOfConsumers] = fork()) {
        case -1:
            printf("Ошибка создания процесса-потребителя: %d\n", errno);
            return -1;
        case 0: //процесс потомок
            //queue = (MessagesQueue *)shmat(shmId, NULL, 0);s
            printf("\nСоздан новый процесс-потребитель pid = %d\n", getpid());
            break;
        default: //родительский процесс
            numOfConsumers++;
            consumers = (pid_t*)realloc(consumers, numOfConsumers * sizeof(pid_t));
            return 0;
    }
    while(1) {
        //printf("While consumer\n");
        Message message;
        //printf("addedMessages in consumer = %d", getValueOfSemaphore(queue->addedMessages));
        decreaseSemaphore(queue->addedMessages);
        //printf("\nafter semaphore\n");
        pthread_mutex_lock(mutex);
        //printf("\nconsumer got mutex\n");
        message = pop(queue);
        pthread_mutex_unlock(mutex);
        //printf("\nconsumer unlocked mutex\n");
        increaseSemaphore(queue->freeSpace);
        //printf("\nconsumer increased semaphore\n");
        //printf("\n message: data=%s, size=%d, hash=%x\n", message.data, message.size, message.hash);
        consumeMessage(&message);
        //printf("\nafter consume message\n");
        printf("\nПроцессом pid = %d извлечено сообщение hash = %x.\nКоличество извлеченных сообщений = %d\n", getpid(), message.hash, queue->retrievedMessages);
        sleep(3);
    }
}

void consumeMessage(Message* message) {
    //printf("\ninside consumemessgae\n");
    int messageHash = message->hash;
    //printf("\ninside consumemessgae\n");
    message->hash = 0;
    //printf("\ninside consumemessgae\n");
    //printf("\ndata=%s, size=%d, hash=%x\n", message->data, message->size, message->hash);
    int calculatedHash = calculateHash(message->data, message->size + 1);
   // printf("\ninside consumemessgae\n");
    if(messageHash != calculatedHash) {
        printf("Вычисленный hash = %x не соответствует hash в сообщении = %x\n", calculatedHash, messageHash);
    }
    message->hash = messageHash;
}

void deleteConsumer() {
    if(numOfConsumers == 0) {
        printf("Нет активных процессов-потребителей\n");
        return;
    }
    pid_t temp = consumers[numOfConsumers - 1];
    numOfConsumers--;
    if(numOfConsumers != 0)
        consumers = (pid_t*)realloc(consumers, numOfConsumers*sizeof(pid_t));
    kill(temp, SIGTERM);
    wait(NULL);
    printf("Процесс-потребитель pid = %d удален\n", temp);
}
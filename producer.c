#include "producer.h"
#include "messagesQueue.h"
#include "semaphores.h"

extern pid_t* producers;
extern MessagesQueue* queue;
extern int numOfProducers;
extern pthread_mutex_t *mutex;

int createNewProducer() {
    switch(producers[numOfProducers] = fork()) {
        case -1:
            printf("Ошибка создания процесса-производителя: %d\n", errno);
            return -1;
        case 0: //процесс потомок
            srand(getpid());
            printf("\nСоздан новый процесс-производитель pid = %d\n", getpid());
            break;
        default:
            numOfProducers++;
            producers = (pid_t*)realloc(producers, numOfProducers * sizeof(pid_t));
            return 0;
    }
    while(1) {
        Message message = createMessage();
        decreaseSemaphore(queue->freeSpace);
        pthread_mutex_lock(mutex);
        push(queue, message);
        pthread_mutex_unlock(mutex);
        increaseSemaphore(queue->addedMessages);
        printf("\nПроцессом pid = %d добавлено новое сообщение hash = %x.\nКоличество сообщений в очереди = %d\n", getpid(), message.hash, getValueOfSemaphore(queue->addedMessages));
        sleep(3);
    }
}

Message createMessage() {
    //Message* message = (Message*)calloc(1, sizeof(Message));
    Message message;
//    message->size = rand() % 256 + 1;
//    message->data = (char*)calloc(((message->size + 3)/4)*4, sizeof(char));
//    for(int i = 0; i < message->size; i++) {
//        message->data[i] = rand() % 256;
//    }
//    message->hash = 0;
//    message->hash = calculateHash(message->data, message->size + 1);
//    return message;
    message.type = 0;
    unsigned short tmp = rand() % 256 + 1;
    if(tmp < 256) {
        message.size = tmp;
    } else {
        message.size = tmp - 1;
    }
    //message.data = (char*)calloc(((message.size + 3)/4)*4, sizeof(char));
    for(int i = 0; i < ((message.size + 3)/4)*4; i++) {
        message.data[i] = rand() % 256;
    }
    message.hash = 0;
    message.hash = calculateHash(message.data, message.size + 1);
    //printf("\nСозданное сообщение: data=%s, hash=%x, size=%d\n", message.data, message.hash, message.size);
    return message;
}

void deleteProducer() {
    if(numOfProducers == 0) {
        printf("Нет активных процессов-производителей\n");
        return;
    }
    pid_t temp = producers[numOfProducers - 1];
    numOfProducers--;
    if(numOfProducers != 0)
        producers = (pid_t*)realloc(producers, numOfProducers*sizeof(pid_t));
    kill(temp, SIGTERM);
    wait(NULL);
    printf("Процесс-производитель pid = %d удален\n", temp);
}
#include "libs.h"

int clientSocket;

void createSocket() {
    //создаем сокет с семейством протоколов AF_INET(IPv4), типа STREAM, протокол TCP
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        printf( "Ошибка создания сокета для клиента: %s\n", strerror(errno));
        exit(errno);
    }
}

void connectToServer(int portNum) {
    struct sockaddr_in address;
    address.sin_family = AF_INET; //семейство протоколов
    address.sin_port = htons(portNum); //переводим номер порта в сетевой порядок байт
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //адрес (localhost) в сетевом порядке байт
    if (connect(clientSocket, (struct sockaddr*)&address, sizeof(struct sockaddr_in)) == -1) {
        printf("Ошибка подключения сокета к серверу: %s\n", strerror(errno));
        exit(errno);
    }
}

void extractCommand(char *input) {
    //ищем `>` в строке
    char *commandStart = strchr(input, '>');
    if (commandStart != NULL) {
        commandStart++;  //переходим к следующему символу
        //пропускаем пробел, если он есть
        if (*commandStart == ' ') commandStart++;
        //если остался только '\n', очищаем строку
        if (*commandStart == '\n' || *commandStart == '\0') {
            input[0] = '\0';
            return;
        }
        //сдвигаем команду в начало
        memmove(input, commandStart, strlen(commandStart) + 1);
    }
    if (strstr(input, "\n") != NULL) {
        //заменяем '\n' на '\0'
        input[strcspn(input, "\n")] = '\0';
    }
}

void handleCommands() {
    char buffer[1024];
    char hint[1024];
    while(1) {
        //выводим сообщения от сервера, пока не будет получено сообщение с ">"
        while(1) {
            memset(buffer, 0, sizeof(buffer));
            if (recv(clientSocket, buffer, sizeof(buffer), 0) == -1) {
                printf("Ошибка получения сообщения от сервера: %s\n", strerror(errno));
                exit(errno);
            }
            printf("%s", buffer);
            if (strstr(buffer, ">") != NULL) {
                memset(hint, 0, sizeof(hint));
                strcpy(hint, buffer);
                memset(buffer, 0, sizeof(buffer));
                break;
            }
        }
        while(1) {
            //ожидаем ввода команды
            fgets(buffer, 1023, stdin);
            //убираем лишние символы
            extractCommand(buffer);
            if (strcmp(buffer, "\0") == 0 || strcmp(buffer, "\n") == 0) {
                printf("%s", hint);
                fflush(stdout);  // Принудительный вывод в консоль
            }
            else {
                break;
            }
        }
        //отправляем команду серверу
        if (send(clientSocket, buffer, strlen(buffer), 0) == -1) {
            printf("Ошибка отправки сообщения серверу: %s\n", strerror(errno));
            exit(errno);
        }
        //если была введена команда QUIT, то получаем сообщение от сервера и завершаем работу клиента
        if (strncasecmp(buffer, "QUIT", 4) == 0) {
            memset(buffer, 0, sizeof(buffer));
            if (recv(clientSocket, buffer, sizeof(buffer), 0) == -1) {
                printf("Ошибка получения сообщения от сервера: %s\n", strerror(errno));
                exit(errno);
            }
            printf("%s", buffer);
            return;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Неверное число аргументов. Формат запуска: client port_no\n");
        exit(EXIT_FAILURE);
    }
    int portNum = strtol(argv[1], NULL, 10);
    createSocket();
    connectToServer(portNum);
    handleCommands();
    close(clientSocket);
    printf("Завершение работы клиента\n");
    return 0;
}
#ifndef CONSUMER_H
#define CONSUMER_H

#include "header.h"
#include "message.h"

void consumeMessage(Message* message);
int createNewConsumer();
void deleteConsumer();
#endif
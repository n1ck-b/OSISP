#ifndef PRODUCER_H
#define PRODUCER_H

#include "header.h"
#include "message.h"

int createNewProducer();
Message* createMessage();
void deleteProducer();
#endif
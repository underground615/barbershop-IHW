#ifndef BARBERSHOP_QUEUE_H
#define BARBERSHOP_QUEUE_H

#include "models.h"

bool queue_init(Queue *queue, int capacity);

bool queue_push(Queue *queue, int customer_id);

int queue_pop(Queue *queue, QueueStrategy strategy, uint32_t random_value);

void queue_free(Queue *queue);

#endif

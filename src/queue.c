#include "queue.h"
#include <stdlib.h>

// инициализируем очередь
bool queue_init(Queue *q, int capacity) {
    if (!q || capacity < 0) {
        return false;
    }
    q->size = 0;
    q->capacity = capacity;
    if (capacity > 0) {
        q->customer_ids = malloc((size_t)capacity * sizeof(int));
    } else {
        q->customer_ids = NULL;
    }
    return capacity == 0 || q->customer_ids;
}

// добавляем клиента
bool queue_push(Queue *q, int id) {
    if (!q || q->size >= q->capacity) {
        return false;
    }
    q->customer_ids[q->size++] = id;
    return true;
}

// получаем клиента
int queue_pop(Queue *q, QueueStrategy strategy, uint32_t random_value) {
    if (!q || !q->size) {
        return -1;
    }
    int index = 0; // 0 если FIFO
    if (strategy == Q_LIFO) {
        index = q->size - 1; // последний элемент
    } else if (strategy == Q_RANDOM) {
        index = (int)(random_value % (uint32_t)q->size); // случайный элемент
    }
    // нормализуем очередь после удаления клиента
    int id = q->customer_ids[index];
    for (int i = index; i + 1 < q->size; ++i) {
        q->customer_ids[i] = q->customer_ids[i + 1];
    }
    q->size--;
    return id;
}

// чистка очереди
void queue_free(Queue *q) {
    if (!q) {
        return;
    }
    free(q->customer_ids);
    q->customer_ids = NULL;
    q->size = q->capacity = 0;
}

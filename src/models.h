#ifndef BARBERSHOP_MODELS_H
#define BARBERSHOP_MODELS_H

#include <stdbool.h>
#include <stdint.h>

// состояние посетителя: ждет, стрижется, обслужен, ушел.
typedef enum {
    CUSTOMER_WAITING,
    CUSTOMER_IN_SERVICE,
    CUSTOMER_SERVED,
    CUSTOMER_LEFT
} CustomerState;

// Виды стратегий очереди
typedef enum {
    Q_FIFO,
    Q_LIFO,
    Q_RANDOM
} QueueStrategy;

// сущность посетителя
typedef struct {
    int id;
    CustomerState state;
    long arrival_ms;
    long start_ms;
    long finish_ms;
} Customer;

// сущность барбера
typedef struct {
    int id;
    bool is_busy;
    int customer_id;
    long finish_ms;
    long busy_ms;
} Barber;

// очерель посетителей
typedef struct {
    int *customer_ids;
    int size;
    int capacity;
} Queue;

// входные параметры из командной строки
typedef struct {
    int max_customers;
    int barber_count;
    int chair_count;
    long arrival_min_ms;
    long arrival_max_ms;
    long haircut_min_ms;
    long haircut_max_ms;
    QueueStrategy strategy;
    unsigned int seed;
    long closing_ms;
    const char *log_path;
} SimulationConfig;

// состояние симуляции барбершопа
typedef struct {
    Barber *barbers;
    Customer *customers;
    Queue queue;
    int barbers_cnt;
    int customers_capacity;
    int customers_cnt;
    int served_cnt;
    int left_cnt;
    long current_time_ms;
    long total_wait_ms;
    long haircut_min_ms;
    long haircut_max_ms;
    QueueStrategy strategy;
    int log_fd;
} Shop;

#endif
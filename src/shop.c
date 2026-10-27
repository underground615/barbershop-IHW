#include "shop.h"
#include "queue.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
// записываем все данные в файловый дескриптор
static void write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0) {
            return;
        }
        buf += n;
        len -= (size_t)n;
    }
}
// выводим сообщение в консоль и лог
void shop_log(Shop *shop, const char *format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    size_t len;
    if ((size_t)n < sizeof(buf)) {
        len = (size_t)n;
    } else {
        len = sizeof(buf) - 1;
    }
    write_all(STDOUT_FILENO, buf, len);
    if (shop && shop->log_fd >= 0) {
        write_all(shop->log_fd, buf, len);
    }
}
// генерируем случайное число
uint32_t shop_random(Shop *shop) {
    (void)shop;
    return (uint32_t)rand();
}
// генерируем случайное число в заданном диапазоне
long shop_random_range(Shop *shop, long min, long max) {
    uint64_t span = (uint64_t)((unsigned long)(max - min)) + 1u;
    return min + (long)((uint64_t)shop_random(shop) % span);
}
// инициализируем парикмахерскую
bool shop_init(Shop *shop, const SimulationConfig *config) {
    if (!shop || !config || config->barber_count <= 0 ||
        config->chair_count < 0 || config->max_customers < 0) {
        return false;
    }
    memset(shop, 0, sizeof(*shop));
    // Переносим консольные настройки в парикмахерскую
    shop->log_fd = -1;
    shop->barbers_cnt = config->barber_count;
    shop->customers_capacity = config->max_customers;
    shop->haircut_min_ms = config->haircut_min_ms;
    shop->haircut_max_ms = config->haircut_max_ms;
    shop->strategy = config->strategy;
    srand(config->seed);
    // создаем мьютекс и общий семафор
    if (pthread_mutex_init(&shop->mutex, NULL) != 0) {
        return false;
    }
    shop->mutex_ready = true;
    if (sem_init(&shop->arrived, 0, 0) != 0) {
        shop_free(shop);
        return false;
    }
    shop->arrived_ready = true;
    // память под парикмахеров
    shop->barbers = calloc((size_t)config->barber_count,sizeof(Barber));
    // памяь под посетителей
    int customer_capacity = config->max_customers;
    if (customer_capacity == 0) {
        customer_capacity = 1;
    }
    shop->customers = calloc((size_t)customer_capacity, sizeof(Customer));

    if (!shop->barbers || !shop->customers || !queue_init(&shop->queue, config->chair_count)) {
        shop_free(shop);
        return false;
    }
    // открываем лог файл
    if (config->log_path) {
        shop->log_fd = open(
            config->log_path,
            O_WRONLY | O_CREAT | O_TRUNC,
            0644
        );

        if (shop->log_fd < 0) {
            perror("log");
            shop_free(shop);
            return false;
        }
    }
    for (int i = 0; i < config->barber_count; ++i) {
        shop->barbers[i].id = i + 1;
        shop->barbers[i].customer_id = -1;
        shop->barbers[i].shop = shop;
        // барбер получает свой семафор
        if (sem_init(&shop->barbers[i].wake, 0, 0) != 0) {
            shop_free(shop);
            return false;
        }
        shop->barber_sems++;
    }
    for (int i = 0; i < config->max_customers; ++i) {
        Customer *customer = &shop->customers[i];
        customer->id = i + 1;
        customer->shop = shop;
        if (sem_init(&customer->done, 0, 0) != 0) {
            shop_free(shop);
            return false;
        }
        if (sem_init(&customer->released, 0, 0) != 0) {
            sem_destroy(&customer->done);
            shop_free(shop);
            return false;
        }
        shop->customer_sems++;
    }
    return true;
}
// освобождаем память парикмахерской
void shop_free(Shop *shop) {
    if (!shop) {
        return;
    }
    if (shop->log_fd >= 0) {
        close(shop->log_fd);
    }
    for (int i = 0; i < shop->barber_sems; ++i) {
        sem_destroy(&shop->barbers[i].wake);
    }
    for (int i = 0; i < shop->customer_sems; ++i) {
        sem_destroy(&shop->customers[i].done);
        sem_destroy(&shop->customers[i].released);
    }
    if (shop->arrived_ready) {
        sem_destroy(&shop->arrived);
    }
    if (shop->mutex_ready) {
        pthread_mutex_destroy(&shop->mutex);
    }
    queue_free(&shop->queue);
    free(shop->barbers);
    free(shop->customers);
    memset(shop, 0, sizeof(*shop));
}
// ищем свободного парикмахера
int shop_find_free_barber(const Shop *shop) {
    for (int i = 0; i < shop->barbers_cnt; i++) {
        if (!shop->barbers[i].is_busy) {
            return i;
        }
    }
    return -1;
}
// обрабатываем приход посетителя
void shop_customer_arrive(Shop *shop, int id) {
    if (!shop || id != shop->customers_cnt + 1 || id > shop->customers_capacity) {
        return;
    }
    Customer *customer = &shop->customers[id - 1];
    customer->id = id;
    customer->arrival_ms = shop->current_time_ms;
    shop->customers_cnt++;
    shop_log(shop,"[%ld ms] Пришёл посетитель %d\n", shop->current_time_ms, id);
    // ищем свободног барбера и начинаем стрижку
    int index = shop_find_free_barber(shop);
    if (index >= 0) {
        shop->barbers[index].is_busy = true;
        shop->barbers[index].customer_id = id;
        sem_post(&shop->barbers[index].wake);
    } else if (queue_push(&shop->queue, id)) { // не нашли добавляем в очередь
        customer->state = CUSTOMER_WAITING;
        shop_log(shop,"[%ld ms] Посетитель %d ожидает. Очередь: %d/%d\n", shop->current_time_ms, id, shop->queue.size, shop->queue.capacity);
    } else { // не влезает в очередь и уходит
        customer->state = CUSTOMER_LEFT;
        shop->left_cnt++;
        shop_log(shop, "[%ld ms] Посетитель %d ушёл: нет свободных мест\n", shop->current_time_ms, id);
    }
}

// получение времени с начала работы
long shop_time_ms(const Shop *shop) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)((now.tv_sec - shop->started.tv_sec) * 1000
        + (now.tv_nsec - shop->started.tv_nsec) / 1000000);
}
// Приостанавливаем текущий поток 
void shop_sleep_ms(long ms) {
    struct timespec delay = {ms / 1000, (ms % 1000) * 1000000};
    nanosleep(&delay, NULL);
}
// ждем разрешение на семафоре (конец стрижки)
void shop_wait(sem_t *semaphore) {
    if (sem_wait(semaphore) != 0) {
        fprintf(stderr, "Ошибка ожидания семафора\n");
        exit(EXIT_FAILURE);
    }
}

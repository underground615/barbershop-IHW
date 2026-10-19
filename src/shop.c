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
// начинаем стрижку посетителя
static void start_haircut(Shop *shop, int barber_index, int id) {
    Barber *barber = &shop->barbers[barber_index];
    Customer *customer = &shop->customers[id - 1];
    long duration = shop_random_range(shop, shop->haircut_min_ms, shop->haircut_max_ms);
    // меняем состояние барбера
    barber->is_busy = true;
    barber->customer_id = id;
    barber->finish_ms = shop->current_time_ms + duration;
    barber->busy_ms += duration;
    // меняем состояние посетителя
    customer->state = CUSTOMER_IN_SERVICE;
    customer->start_ms = shop->current_time_ms;
    customer->finish_ms = barber->finish_ms;

    shop->total_wait_ms += customer->start_ms - customer->arrival_ms;
    shop_log(shop,"[%ld ms] Парикмахер %d начал стричь посетителя %d (длительность %ld ms)\n", shop->current_time_ms, barber->id, id, duration);
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
        start_haircut(shop, index, id);
    } else if (queue_push(&shop->queue, id)) { // не нашли добавляем в очередь
        customer->state = CUSTOMER_WAITING;
        shop_log(shop,"[%ld ms] Посетитель %d ожидает. Очередь: %d/%d\n", shop->current_time_ms, id, shop->queue.size, shop->queue.capacity);
    } else { // не влезает в очередь и уходит
        customer->state = CUSTOMER_LEFT;
        shop->left_cnt++;
        shop_log(shop, "[%ld ms] Посетитель %d ушёл: нет свободных мест\n", shop->current_time_ms, id);
    }
}
// завершаем стрижку посетителя
void shop_finish_haircut(Shop *shop, int index) {
    if (!shop || index < 0 || index >= shop->barbers_cnt) {
        return;
    }
    Barber *barber = &shop->barbers[index];
    if (!barber->is_busy || barber->finish_ms != shop->current_time_ms) {
        return;
    }
    int id = barber->customer_id;
    shop->customers[id - 1].state = CUSTOMER_SERVED;
    shop->served_cnt++;
    shop_log(shop, "[%ld ms] Парикмахер %d закончил стрижку посетителя %d\n", shop->current_time_ms, barber->id, id);
    barber->is_busy = false;
    barber->customer_id = -1;
    barber->finish_ms = 0;
    int next = queue_pop(&shop->queue, shop->strategy, shop_random(shop));
    if (next >= 0) { // если нашли ожидающего
        start_haircut(shop, index, next);
    } else { // очередь была пуста
        shop_log(shop,"[%ld ms] Парикмахер %d свободен\n", shop->current_time_ms, barber->id);
    }
}
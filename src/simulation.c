#include "simulation.h"
#include "shop.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

// поток посетителя
static void *customer_run(void *arg) {
    Customer *customer = arg;
    Shop *shop = customer->shop;
    pthread_mutex_lock(&shop->mutex);
    shop->current_time_ms = shop_time_ms(shop);
    shop_customer_arrive(shop, customer->id);
    bool accepted = customer->state != CUSTOMER_LEFT;
    pthread_mutex_unlock(&shop->mutex);
    sem_post(&shop->arrived);
    if (!accepted) {
        return NULL;
    }
    shop_wait(&customer->done);
    pthread_mutex_lock(&shop->mutex);
    shop_log(shop, "[%ld ms] Посетитель %d освободил кресло и ушёл\n",
             shop_time_ms(shop), customer->id);
    pthread_mutex_unlock(&shop->mutex);
    sem_post(&customer->released);
    return NULL;
}

// поток парикмахера
static void *barber_run(void *arg) {
    Barber *barber = arg;
    Shop *shop = barber->shop;
    for (;;) {
        pthread_mutex_lock(&shop->mutex);
        if (!barber->is_busy && shop->closing) {
            pthread_mutex_unlock(&shop->mutex);
            break;
        }
        if (!barber->is_busy) {
            shop_log(shop, "[%ld ms] Парикмахер %d засыпает\n",
                     shop_time_ms(shop), barber->id);
        }
        pthread_mutex_unlock(&shop->mutex);
        shop_wait(&barber->wake);
        pthread_mutex_lock(&shop->mutex);
        if (!barber->is_busy && shop->closing) {
            pthread_mutex_unlock(&shop->mutex);
            break;
        }
        shop_log(shop, "[%ld ms] Парикмахер %d проснулся\n",
                 shop_time_ms(shop), barber->id);
        while (barber->is_busy) {
            Customer *customer = &shop->customers[barber->customer_id - 1];
            long duration = shop_random_range(shop, shop->haircut_min_ms, shop->haircut_max_ms);
            customer->state = CUSTOMER_IN_SERVICE;
            customer->start_ms = shop_time_ms(shop);
            shop->total_wait_ms += customer->start_ms - customer->arrival_ms;
            shop_log(shop, "[%ld ms] Парикмахер %d пригласил посетителя %d к креслу\n",
                     customer->start_ms, barber->id, customer->id);
            shop_log(shop, "[%ld ms] Парикмахер %d начал стричь посетителя %d (длительность %ld ms)\n",
                     customer->start_ms, barber->id, customer->id, duration);
            pthread_mutex_unlock(&shop->mutex);
            shop_sleep_ms(duration);
            pthread_mutex_lock(&shop->mutex);
            customer->finish_ms = shop_time_ms(shop);
            customer->state = CUSTOMER_SERVED;
            barber->busy_ms += customer->finish_ms - customer->start_ms;
            shop->served_cnt++;
            shop_log(shop, "[%ld ms] Парикмахер %d закончил стрижку посетителя %d\n",
                     customer->finish_ms, barber->id, customer->id);
            pthread_mutex_unlock(&shop->mutex);
            sem_post(&customer->done);
            shop_wait(&customer->released);
            pthread_mutex_lock(&shop->mutex);
            int next = queue_pop(&shop->queue, shop->strategy, shop_random(shop));
            barber->customer_id = next;
            barber->is_busy = next >= 0;
        }
        pthread_mutex_unlock(&shop->mutex);
    }
    return NULL;
}

// запуск симуляции
int simulation_run(Shop *shop, const SimulationConfig *config) {
    int result = 0;
    int barber_threads = 0;
    int customer_threads = 0;
    clock_gettime(CLOCK_MONOTONIC, &shop->started);
    shop_log(shop, "=== Парикмахерская открылась ===\n");
    // создаем потоки для парикмахеров
    for (int i = 0; i < shop->barbers_cnt; ++i) {
        int error = pthread_create(&shop->barbers[i].thread, NULL, barber_run, &shop->barbers[i]);
        if (error != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            result = 1;
            break;
        }
        barber_threads++;
    }
    // создаем потоки для посетителей
    for (int i = 0; result == 0 && i < config->max_customers; ++i) {
        pthread_mutex_lock(&shop->mutex);
        long delay = shop_random_range(shop, config->arrival_min_ms, config->arrival_max_ms);
        pthread_mutex_unlock(&shop->mutex);
        long remaining = config->closing_ms - shop_time_ms(shop);
        if (remaining <= 0) {
            break;
        }
        if (delay > remaining) {
            shop_sleep_ms(remaining);
            break;
        }
        shop_sleep_ms(delay);
        if (shop_time_ms(shop) >= config->closing_ms) {
            break;
        }
        // создаем сам поток
        int error = pthread_create(&shop->customers[i].thread, NULL, customer_run, &shop->customers[i]);
        if (error != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(error));
            result = 1;
            break;
        }
        customer_threads++;
        shop_wait(&shop->arrived);
    }
    pthread_mutex_lock(&shop->mutex);
    shop_log(shop, "[%ld ms] Приём новых посетителей окончен\n", shop_time_ms(shop));
    pthread_mutex_unlock(&shop->mutex);
    for (int i = 0; i < customer_threads; ++i) {
        pthread_join(shop->customers[i].thread, NULL);
    }
    pthread_mutex_lock(&shop->mutex);
    shop->closing = true;
    pthread_mutex_unlock(&shop->mutex);
    for (int i = 0; i < barber_threads; ++i) {
        sem_post(&shop->barbers[i].wake);
    }
    for (int i = 0; i < barber_threads; ++i) {
        pthread_join(shop->barbers[i].thread, NULL);
    }
    shop->current_time_ms = shop_time_ms(shop);
    // выводим итоговую статистику
    shop_log(
        shop,
        "\n=== Статистика ===\n"
        "Пришло: %d\n"
        "Обслужено: %d\n"
        "Ушло: %d\n"
        "Осталось в очереди: %d\n"
        "Время работы: %ld ms\n",
        shop->customers_cnt,
        shop->served_cnt,
        shop->left_cnt,
        shop->queue.size,
        shop->current_time_ms
    );
    // среднее время ожидания в очереди
    double average_wait = 0.0;
    if (shop->served_cnt > 0) {
        average_wait = (double)shop->total_wait_ms / shop->served_cnt;
    }
    shop_log(shop,"Среднее ожидание: %.2f ms\n", average_wait);
    // выводим загруженность каждого парикмахера
    for (int i = 0; i < shop->barbers_cnt; ++i) {
        double perc = 0.0;
        if (shop->current_time_ms > 0) {
            perc = 100.0 * shop->barbers[i].busy_ms / shop->current_time_ms;
        }
        shop_log(shop, "Загрузка парикмахера %d: %.2f%%\n", i + 1, perc);
    }
    return result;
}

#include "simulation.h"
#include "shop.h"
#include <limits.h>
#include <signal.h>

static volatile sig_atomic_t interrupted = 0;
// обрабатываем сигнал прерывания
static void on_signal(int signum) {
    (void)signum;
    interrupted = 1;
}
// ищем ближайшее завершение стрижки
static long next_finish(const Shop *shop) {
    long result = -1;
    for (int i = 0; i < shop->barbers_cnt; ++i) {
        if (shop->barbers[i].is_busy &&
            (result < 0 || shop->barbers[i].finish_ms < result)) {
            result = shop->barbers[i].finish_ms;
        }
    }
    return result;
}
// запуск симуляции
void simulation_run(Shop *shop, const SimulationConfig *config) {
    if (!shop || !config) {
        return;
    }
    interrupted = 0;
    void (*old_int)(int) = signal(SIGINT, on_signal); // ручное завершение Ctrl+C
    // генерируем время первого клиента
    long arrival = -1;
    if (config->max_customers > 0) {
        arrival = shop_random_range(shop, config->arrival_min_ms, config->arrival_max_ms);
    }
    // успеет ли до закрытия первый клиент
    if (arrival >= 0 && arrival > config->closing_ms) {
        arrival = -1;
    }
    shop_log(shop, "=== Парикмахерская открылась ===\n");
    while (!interrupted) {
        long finish = next_finish(shop);
        // нет клиентов и активных стрижек
        if (arrival < 0 && finish < 0) {
            break;
        }
        // финиш до пришедшего клиента
        if (finish >= 0 && (arrival < 0 || finish <= arrival)) {
            shop->current_time_ms = finish;
            for (int i = 0; i < shop->barbers_cnt; ++i) {
                if (shop->barbers[i].is_busy && shop->barbers[i].finish_ms == finish) {
                    shop_finish_haircut(shop, i); // заканчиваем стрижки барберов которые заканчиваются сейчас
                }
            }
        } else { // приход клиента
            shop->current_time_ms = arrival;
            shop_customer_arrive(shop, shop->customers_cnt + 1);
            if (shop->customers_cnt == config->max_customers) {
                arrival = -1; // больше клиенты не влезают
            } else {
                // время прихода следующего клиента
                long delta = shop_random_range(shop, config->arrival_min_ms, config->arrival_max_ms);
                if (delta > LONG_MAX - arrival || arrival + delta > config->closing_ms) {
                    arrival = -1;
                } else {
                    arrival += delta;
                }
            }
        }
    }
    if (interrupted) {
        shop_log(shop, "Симуляция прервана сигналом\n");
    }
    // выводим итоговую статистику
    shop_log(
        shop,
        "\n=== Статистика ===\n"
        "Пришло: %d\n"
        "Обслужено: %d\n"
        "Ушло: %d\n"
        "Осталось в очереди: %d\n"
        "Время модели: %ld ms\n",
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
    signal(SIGINT, old_int);
}
#include "shop.h"
#include "simulation.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

// преобразуем строку в число
static int parse_long(const char *s, long *out) {
    char *end;
    long value = strtol(s, &end, 10);
    if (!*s || *end) {
        return 0;
    }
    *out = value;
    return 1;
}

// инструкция по запуску
static void usage(const char *name) {
    fprintf(
        stderr,
        "Использование: %s "
        "[--barbers N] [--chairs N] [--customers N] "
        "[--arrival-min MS] [--arrival-max MS] "
        "[--haircut-min MS] [--haircut-max MS] "
        "[--closing MS] [--strategy fifo|lifo|random] "
        "[--seed N] [--log FILE]\n",
        name
    );
}

int main(int argc, char **argv) {
// для русских символов на винде
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // задаём параметры по умолчанию
    SimulationConfig config = {
        .max_customers = 15,
        .barber_count = 2,
        .chair_count = 3,
        .arrival_min_ms = 500,
        .arrival_max_ms = 2000,
        .haircut_min_ms = 2000,
        .haircut_max_ms = 5000,
        .strategy = Q_FIFO,
        .seed = 1,
        .closing_ms = 60000,
        .log_path = "simulation.log"
    };

    // обрабатываем аргументы командной строки
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }

        const char *key = argv[i];
        const char *val = argv[i + 1];

        if (!strcmp(key, "--log")) {
            config.log_path = val;
            continue;
        }

        if (!strcmp(key, "--strategy")) {
            if (!strcmp(val, "fifo")) {
                config.strategy = Q_FIFO;
            } else if (!strcmp(val, "lifo")) {
                config.strategy = Q_LIFO;
            } else if (!strcmp(val, "random")) {
                config.strategy = Q_RANDOM;
            } else {
                usage(argv[0]);
                return 2;
            }

            continue;
        }

        long n;

        if (!parse_long(val, &n)) {
            usage(argv[0]);
            return 2;
        }

        if (!strcmp(key, "--barbers") && n > 0 && n <= INT_MAX) {
            config.barber_count = (int)n;
        } else if (!strcmp(key, "--chairs") && n >= 0 && n <= INT_MAX) {
            config.chair_count = (int)n;
        } else if (!strcmp(key, "--customers") && n >= 0 && n <= INT_MAX) {
            config.max_customers = (int)n;
        } else if (!strcmp(key, "--arrival-min")) {
            config.arrival_min_ms = n;
        } else if (!strcmp(key, "--arrival-max")) {
            config.arrival_max_ms = n;
        } else if (!strcmp(key, "--haircut-min")) {
            config.haircut_min_ms = n;
        } else if (!strcmp(key, "--haircut-max")) {
            config.haircut_max_ms = n;
        } else if (!strcmp(key, "--closing")) {
            config.closing_ms = n;
        } else if (!strcmp(key, "--seed") &&
                   n >= 0 && (unsigned long)n <= UINT_MAX) {
            config.seed = (unsigned int)n;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    // проверяем интервалы времени
    if (config.arrival_min_ms <= 0 ||
        config.arrival_max_ms < config.arrival_min_ms ||
        config.haircut_min_ms <= 0 ||
        config.haircut_max_ms < config.haircut_min_ms ||
        config.closing_ms < 0 ||
        config.arrival_max_ms > LONG_MAX - config.closing_ms ||
        config.haircut_max_ms >
            LONG_MAX - config.closing_ms - config.arrival_max_ms) {
        fprintf(stderr, "Некорректные интервалы времени\n");
        return 2;
    }

    Shop shop;
    if (!shop_init(&shop, &config)) {
        fprintf(stderr, "Не удалось создать парикмахерскую\n");
        return 1;
    }
    simulation_run(&shop, &config);
    shop_free(&shop);
    return 0;
}
#ifndef BARBERSHOP_SHOP_H
#define BARBERSHOP_SHOP_H

#include "models.h"

bool shop_init(Shop *shop, const SimulationConfig *config);

void shop_free(Shop *shop);

int shop_find_free_barber(const Shop *shop);

void shop_customer_arrive(Shop *shop, int customer_id);

long shop_time_ms(const Shop *shop);

void shop_sleep_ms(long ms);

void shop_wait(sem_t *semaphore);

uint32_t shop_random(Shop *shop);

long shop_random_range(Shop *shop, long min, long max);

void shop_log(Shop *shop, const char *format, ...);

#endif

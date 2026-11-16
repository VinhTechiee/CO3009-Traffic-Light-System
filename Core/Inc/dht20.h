#ifndef CO3009_DHT20_H
#define CO3009_DHT20_H

#include "global.h"

typedef struct {
    float temperature_c;
    float relative_humidity_percent;
    bool valid;
} dht20_sample_t;
app_status_t dht20_init(void);
/* Scheduled nonblocking measurement/transfer step. */
void dht20_task(void);
/* Copy latest sample; APP_NOT_READY before first valid sample. */
app_status_t dht20_get_sample(dht20_sample_t *sample);

#endif /* CO3009_DHT20_H */

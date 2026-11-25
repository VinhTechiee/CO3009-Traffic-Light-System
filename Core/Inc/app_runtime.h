#ifndef CO3009_APP_RUNTIME_H
#define CO3009_APP_RUNTIME_H

#include "dht20.h"
#include "lcd.h"

typedef struct {
    bool buttons_enabled;
    uint8_t button_active_low_mask;
    /* NULL bus disables the sensor. NULL LCD leaves display tasks disabled. */
    const i2c_async_t *bus;
    dht20_clock_ms_fn_t clock_ms;
    void *clock_context;
    const lcd_config_t *lcd;
} app_runtime_config_t;

typedef struct {
    app_status_t sensor;
    app_status_t display;
} app_runtime_services_t;

/* Call once at boot after hardware setup and before enabling the scheduler IRQ.
 * Optional-service startup statuses are reported independently from core status.
 * Drivers copy their configuration and retain only long-lived contexts. */
app_status_t app_runtime_start(const app_runtime_config_t *config,
                               app_runtime_services_t *services);

#endif

#ifndef CO3009_GLOBAL_H
#define CO3009_GLOBAL_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { APP_OK, APP_INVALID_ARGUMENT, APP_NOT_READY, APP_BUSY,
               APP_NO_CAPACITY, APP_NOT_FOUND, APP_IO_ERROR } app_status_t;
typedef enum { APP_MODE_AUTOMATIC, APP_MODE_MANUAL, APP_MODE_TUNING } app_mode_t;
typedef enum { TRAFFIC_RED, TRAFFIC_YELLOW, TRAFFIC_GREEN } traffic_color_t;
typedef struct { traffic_color_t road_1; traffic_color_t road_2; } traffic_state_t;
/* Units only; defaults, ranges and relationships are not specified yet. */
typedef struct { uint32_t red_ms; uint32_t yellow_ms; uint32_t green_ms; } timing_config_t;
/* Logical identifiers only: no pin mapping or action assignment. */
typedef uint8_t button_id_t;
typedef enum { BUTTON_PRESS, BUTTON_RELEASE } button_event_kind_t;
typedef struct { button_id_t id; button_event_kind_t kind; } button_event_t;
#endif /* CO3009_GLOBAL_H */

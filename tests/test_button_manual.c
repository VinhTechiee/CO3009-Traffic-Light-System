#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "board_profile.h"
#include "button.h"
#include "fsm_manual.h"
#include "traffic_light.h"

enum {
    ROAD1_A = 1U << 3,
    ROAD1_B = 1U << 5,
    ROAD2_A = 1U << 4,
    ROAD2_B = 1U << 10,
    UNRELATED = 1U << 15
};

static const uint8_t active_low = BUTTON_MASK(BUTTON_ID_1) | BUTTON_MASK(BUTTON_ID_3)
    | BUTTON_MASK(BUTTON_ID_USER);
static bool high[BUTTON_ID_COUNT];
static uint32_t now_ms = UINT32_MAX - 30U;
static uint16_t pins;
static unsigned writes;
static unsigned sampled_green;
static unsigned previous_green;

static const uint8_t codes[2][3] = {
    {CO3009_TRAFFIC_ROAD1_RED_CODE, CO3009_TRAFFIC_ROAD1_YELLOW_CODE,
     CO3009_TRAFFIC_ROAD1_GREEN_CODE},
    {CO3009_TRAFFIC_ROAD2_RED_CODE, CO3009_TRAFFIC_ROAD2_YELLOW_CODE,
     CO3009_TRAFFIC_ROAD2_GREEN_CODE}
};

static bool read_level(button_id_t id)
{
    return high[id];
}

static uint32_t clock_ms(void)
{
    return now_ms;
}

static void set_pressed(button_id_t id, bool pressed)
{
    high[id] = pressed != ((active_low & BUTTON_MASK(id)) != 0U);
}

static void sample(unsigned count)
{
    for (unsigned n = 0U; n < count; ++n) {
        now_ms += 10U;
        button_task();
    }
}

static void expect_event(button_id_t id, button_input_event_kind_t kind)
{
    button_input_event_t event;
    assert(button_take_event(&event));
    assert(event.id == id && event.kind == kind);
}

static void expect_empty(void)
{
    button_input_event_t event;
    assert(!button_take_event(&event));
}

static void test_bounce(void)
{
    set_pressed(BUTTON_ID_1, true);
    sample(1);
    set_pressed(BUTTON_ID_1, false);
    sample(1);
    set_pressed(BUTTON_ID_1, true);
    sample(2);
    expect_empty();
    sample(1);
    expect_event(BUTTON_ID_1, BUTTON_EVENT_PRESS);
    sample(160);
    expect_empty();

    set_pressed(BUTTON_ID_1, false);
    sample(2);
    expect_empty();
    set_pressed(BUTTON_ID_1, true);
    sample(1);
    set_pressed(BUTTON_ID_1, false);
    sample(3);
    expect_event(BUTTON_ID_1, BUTTON_EVENT_RELEASE);
    expect_empty();
}

static void test_short_and_long(void)
{
    for (unsigned tap = 0; tap < 3U; ++tap) {
        set_pressed(BUTTON_ID_2, true);
        sample(3);
        expect_event(BUTTON_ID_2, BUTTON_EVENT_PRESS);
        set_pressed(BUTTON_ID_2, false);
        sample(3);
        expect_event(BUTTON_ID_2, BUTTON_EVENT_RELEASE);
        expect_event(BUTTON_ID_2, BUTTON_EVENT_SHORT);
        expect_empty();
    }

    set_pressed(BUTTON_ID_2, true);
    sample(3);
    expect_event(BUTTON_ID_2, BUTTON_EVENT_PRESS);
    sample(49);
    expect_empty();
    sample(1);
    expect_event(BUTTON_ID_2, BUTTON_EVENT_LONG);
    sample(100);
    expect_empty();
    set_pressed(BUTTON_ID_2, false);
    sample(3);
    expect_event(BUTTON_ID_2, BUTTON_EVENT_RELEASE);
    expect_empty();

    set_pressed(BUTTON_ID_USER, true);
    sample(3);
    expect_event(BUTTON_ID_USER, BUTTON_EVENT_PRESS);
    sample(149);
    expect_empty();
    sample(1);
    expect_event(BUTTON_ID_USER, BUTTON_EVENT_LONG);
    set_pressed(BUTTON_ID_USER, false);
    sample(3);
    expect_event(BUTTON_ID_USER, BUTTON_EVENT_RELEASE);
    expect_empty();
}

static void test_adjust_conflict(void)
{
    set_pressed(BUTTON_ID_3, true);
    set_pressed(BUTTON_ID_4, true);
    sample(100);
    expect_empty();
    set_pressed(BUTTON_ID_3, false);
    sample(60);
    expect_empty();
    set_pressed(BUTTON_ID_4, false);
    sample(3);
    expect_empty();

    set_pressed(BUTTON_ID_3, true);
    sample(3);
    expect_event(BUTTON_ID_3, BUTTON_EVENT_PRESS);
    sample(49);
    expect_empty();
    sample(1);
    expect_event(BUTTON_ID_3, BUTTON_EVENT_REPEAT);
    sample(24);
    expect_empty();
    sample(1);
    expect_event(BUTTON_ID_3, BUTTON_EVENT_REPEAT);
    set_pressed(BUTTON_ID_3, false);
    sample(3);
    expect_event(BUTTON_ID_3, BUTTON_EVENT_RELEASE);
    expect_empty();
}

static void test_flush_and_capacity(void)
{
    set_pressed(BUTTON_ID_1, true);
    set_pressed(BUTTON_ID_2, true);
    sample(3);
    button_flush(BUTTON_MASK(BUTTON_ID_2));
    expect_event(BUTTON_ID_1, BUTTON_EVENT_PRESS);
    sample(60);
    expect_empty();

    set_pressed(BUTTON_ID_2, false);
    sample(2);
    set_pressed(BUTTON_ID_2, true);
    sample(4);
    expect_empty();
    set_pressed(BUTTON_ID_2, false);
    sample(3);
    expect_empty();
    set_pressed(BUTTON_ID_1, false);
    sample(3);
    expect_event(BUTTON_ID_1, BUTTON_EVENT_RELEASE);

    /* The first 32 edges survive. New events are dropped while full. */
    for (unsigned tap = 0; tap < 20U; ++tap) {
        set_pressed(BUTTON_ID_1, true);
        sample(3);
        set_pressed(BUTTON_ID_1, false);
        sample(3);
    }
    for (unsigned tap = 0; tap < 16U; ++tap) {
        expect_event(BUTTON_ID_1, BUTTON_EVENT_PRESS);
        expect_event(BUTTON_ID_1, BUTTON_EVENT_RELEASE);
    }
    expect_empty();
}

static uint16_t code_pins(uint8_t code, uint16_t pin_a, uint16_t pin_b)
{
    const uint16_t levels[] = {0U, pin_b, pin_a, (uint16_t)(pin_a | pin_b)};
    return levels[code];
}

static uint16_t electrical_level(uint8_t road1, uint8_t road2)
{
    return (uint16_t)(code_pins(road1, ROAD1_A, ROAD1_B)
                      | code_pins(road2, ROAD2_A, ROAD2_B) | UNRELATED);
}

static void write_gpio(uint16_t mask, bool set)
{
    const uint16_t green1 = code_pins(codes[0][TRAFFIC_GREEN], ROAD1_A, ROAD1_B);
    const uint16_t green2 = code_pins(codes[1][TRAFFIC_GREEN], ROAD2_A, ROAD2_B);

    assert(mask != 0U && (mask & (ROAD1_A | ROAD1_B | ROAD2_A | ROAD2_B)) == mask);
    ++writes;
    /* Model one signal settling before the next, even for a grouped write. */
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        const uint16_t pin = (uint16_t)(1U << bit);
        if ((mask & pin) != 0U) {
            pins = set ? (uint16_t)(pins | pin) : (uint16_t)(pins & ~pin);
            const bool road1_is_green = (pins & (ROAD1_A | ROAD1_B)) == green1;
            const bool road2_is_green = (pins & (ROAD2_A | ROAD2_B)) == green2;
            assert(!road1_is_green || !road2_is_green);
            sampled_green |= (road1_is_green ? 1U : 0U) | (road2_is_green ? 2U : 0U);
        }
    }
    assert((pins & UNRELATED) != 0U);
}

static void expect_codes(uint8_t road1, uint8_t road2)
{
    assert(pins == electrical_level(road1, road2));
    const unsigned green = (road1 == codes[0][TRAFFIC_GREEN] ? 1U : 0U)
        | (road2 == codes[1][TRAFFIC_GREEN] ? 2U : 0U);
    assert((sampled_green & ~(previous_green | green)) == 0U);
    previous_green = green;
    sampled_green = 0U;
}

static void expect_lights(traffic_state_t state)
{
    expect_codes(codes[0][state.road_1], codes[1][state.road_2]);
}

static void test_light_transitions(void)
{
    const traffic_state_t states[] = {
        {TRAFFIC_RED, TRAFFIC_RED}, {TRAFFIC_RED, TRAFFIC_YELLOW},
        {TRAFFIC_RED, TRAFFIC_GREEN}, {TRAFFIC_YELLOW, TRAFFIC_RED},
        {TRAFFIC_YELLOW, TRAFFIC_YELLOW}, {TRAFFIC_YELLOW, TRAFFIC_GREEN},
        {TRAFFIC_GREEN, TRAFFIC_RED}, {TRAFFIC_GREEN, TRAFFIC_YELLOW}
    };
    const traffic_state_t invalid = {TRAFFIC_RED, (traffic_color_t)99};
    const traffic_state_t dual_green = {TRAFFIC_GREEN, TRAFFIC_GREEN};

    assert(traffic_light_init() == APP_NOT_READY);
    assert(traffic_light_apply(&states[0]) == APP_NOT_READY);
    assert(traffic_light_off() == APP_NOT_READY);
    assert(traffic_light_bind_io(NULL) == APP_INVALID_ARGUMENT);
    assert(traffic_light_bind_io(write_gpio) == APP_OK);
    pins = electrical_level(codes[0][TRAFFIC_RED], codes[1][TRAFFIC_RED]); /* Startup preload. */
    assert(traffic_light_init() == APP_OK);
    assert(traffic_light_bind_io(write_gpio) == APP_BUSY);
    expect_lights(states[0]);

    for (size_t i = 0U; i < sizeof states / sizeof states[0]; ++i) {
        for (size_t j = 0U; j < sizeof states / sizeof states[0]; ++j) {
            assert(traffic_light_apply(&states[i]) == APP_OK);
            expect_lights(states[i]);
            assert(traffic_light_apply(&states[j]) == APP_OK);
            expect_lights(states[j]);
        }
        assert(traffic_light_apply(&states[i]) == APP_OK);
        expect_lights(states[i]);
        assert(traffic_light_off() == APP_OK);
        expect_codes(CO3009_TRAFFIC_ROAD1_OFF_CODE, CO3009_TRAFFIC_ROAD2_OFF_CODE);
        assert(traffic_light_apply(&states[i]) == APP_OK);
        expect_lights(states[i]);
    }

    const unsigned previous_writes = writes;
    assert(traffic_light_apply(NULL) == APP_INVALID_ARGUMENT);
    assert(traffic_light_apply(&invalid) == APP_INVALID_ARGUMENT);
    assert(traffic_light_apply(&dual_green) == APP_INVALID_ARGUMENT);
    assert(writes == previous_writes);
    assert(traffic_light_init() == APP_OK);
    expect_lights(states[0]);
}

static void route_manual_edges(void)
{
    button_input_event_t input;
    while (button_take_event(&input)) {
        if (input.kind == BUTTON_EVENT_PRESS || input.kind == BUTTON_EVENT_RELEASE) {
            const button_event_t edge = {input.id, input.kind == BUTTON_EVENT_PRESS ? BUTTON_PRESS : BUTTON_RELEASE};
            fsm_manual_on_button(&edge);
        }
    }
}

static void manual_press(void)
{
    set_pressed(BUTTON_ID_2, true);
    sample(3);
    set_pressed(BUTTON_ID_2, false);
    sample(3);
    route_manual_edges();
}

static void expect_manual(traffic_state_t expected)
{
    traffic_state_t phase;
    assert(fsm_manual_get_phase(&phase) == APP_OK);
    assert(phase.road_1 == expected.road_1 && phase.road_2 == expected.road_2);
    expect_lights(expected);
}

static void test_manual(void)
{
    const traffic_state_t all_red = {TRAFFIC_RED, TRAFFIC_RED};
    const traffic_state_t phases[] = {
        {TRAFFIC_RED, TRAFFIC_GREEN}, {TRAFFIC_RED, TRAFFIC_YELLOW},
        {TRAFFIC_GREEN, TRAFFIC_RED}, {TRAFFIC_YELLOW, TRAFFIC_RED}
    };
    const traffic_state_t invalid = {TRAFFIC_GREEN, TRAFFIC_GREEN};
    traffic_state_t phase;

    assert(fsm_manual_enter(NULL) == APP_INVALID_ARGUMENT);
    assert(fsm_manual_get_phase(NULL) == APP_INVALID_ARGUMENT);
    assert(fsm_manual_get_phase(&phase) == APP_NOT_READY);
    assert(fsm_manual_enter(&invalid) == APP_OK);
    assert(fsm_manual_enter(&all_red) == APP_BUSY);
    expect_manual(all_red);
    for (unsigned step = 0U; step < 8U; ++step) {
        manual_press();
        fsm_manual_task();
        expect_manual(phases[step % 4U]);
        fsm_manual_task();
        expect_manual(phases[step % 4U]);
    }

    manual_press();
    manual_press();
    fsm_manual_task();
    expect_manual(phases[0]);
    fsm_manual_task();
    expect_manual(phases[1]);

    set_pressed(BUTTON_ID_2, true);
    sample(3);
    route_manual_edges();
    fsm_manual_exit();
    assert(fsm_manual_get_phase(&phase) == APP_NOT_READY);
    assert(fsm_manual_enter(&phases[2]) == APP_OK);
    sample(80);
    route_manual_edges();
    fsm_manual_task();
    expect_manual(phases[2]);
    set_pressed(BUTTON_ID_2, false);
    sample(3);
    route_manual_edges();
    manual_press();
    fsm_manual_task();
    expect_manual(phases[3]);
    fsm_manual_exit();
}

int main(void)
{
    for (button_id_t id = 0U; id < BUTTON_ID_COUNT; ++id) {
        set_pressed(id, false);
    }
    assert(button_init() == APP_NOT_READY);
    assert(button_bind_io(NULL, clock_ms) == APP_INVALID_ARGUMENT);
    assert(button_configure_active_low(0xFFU) == APP_INVALID_ARGUMENT);
    assert(button_bind_io(read_level, clock_ms) == APP_OK);
    assert(button_configure_active_low(active_low) == APP_OK);
    assert(button_init() == APP_OK);
    assert(button_init() == APP_BUSY);
    assert(button_configure_active_low(0U) == APP_BUSY);
    assert(!button_take_event(NULL));
    sample(4);
    expect_empty();

    test_bounce();
    test_short_and_long();
    test_adjust_conflict();
    test_flush_and_capacity();
    test_light_transitions();
    test_manual();
    puts("Button, traffic output and Manual tests passed");
    return 0;
}

#include "scheduler.h"
#include "software_timer.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char task;
    uint64_t tick;
} trace_entry_t;

static trace_entry_t trace[32];
static size_t trace_count;
static sch_task_id_t callback_id;
static uint64_t fake_tick;

static void record(char task)
{
    assert(trace_count < sizeof trace / sizeof trace[0]);
    trace[trace_count++] = (trace_entry_t){task, SCH_Now()};
}

static void task_a(void) { record('A'); }
static void task_b(void) { record('B'); }
static void task_c(void) { record('C'); }
static void task_d(void) { record('D'); }

static sch_task_id_t add(sch_task_fn_t task, uint32_t delay, uint32_t period)
{
    sch_task_id_t id = 0U;
    assert(SCH_Add_Task(task, delay, period, &id) == APP_OK);
    assert(id != 0U);
    return id;
}

static void advance(uint32_t ticks)
{
    for (uint32_t i = 0U; i < ticks; ++i) {
        SCH_Update();
    }
}

static void expect_trace(const trace_entry_t *expected, size_t count)
{
    assert(trace_count == count);
    for (size_t i = 0U; i < count; ++i) {
        assert(trace[i].task == expected[i].task);
        assert(trace[i].tick == expected[i].tick);
    }
}

static void reset(void)
{
    SCH_Init();
    trace_count = 0U;
    assert(SCH_Now() == 0U);
}

static void test_empty_and_invalid(void)
{
    sch_task_id_t id = 99U;
    assert(SCH_Add_Task(task_a, 1U, 1U, &id) == APP_NOT_READY);
    assert(SCH_Delete_Task(id) == APP_NOT_READY);
    reset();
    assert(SCH_Add_Task(NULL, 1U, 1U, &id) == APP_INVALID_ARGUMENT);
    assert(SCH_Add_Task(task_a, 1U, 1U, NULL) == APP_INVALID_ARGUMENT);
    assert(SCH_Add_Task(task_a, 0U, 1U, &id) == APP_INVALID_ARGUMENT);
    assert(id == 99U);
    assert(SCH_Delete_Task(0U) == APP_INVALID_ARGUMENT);
    assert(SCH_Delete_Task(id) == APP_NOT_FOUND);
    advance(20U);
    SCH_Dispatch_Tasks();
    assert(SCH_Now() == 20U && trace_count == 0U);
}

static void test_order_and_periods(void)
{
    reset();
    (void)add(task_a, 2U, 3U);
    (void)add(task_b, 4U, 5U);
    const sch_task_id_t once = add(task_c, 8U, 0U);
    (void)add(task_d, 4U, 0U);
    for (uint32_t tick = 1U; tick <= 15U; ++tick) {
        const size_t before = trace_count;
        SCH_Update();
        assert(trace_count == before);
        SCH_Dispatch_Tasks();
        const size_t after = trace_count;
        SCH_Dispatch_Tasks();
        assert(trace_count == after);
    }
    const trace_entry_t expected[] = {
        {'A', 2U}, {'B', 4U}, {'D', 4U}, {'A', 5U}, {'C', 8U},
        {'A', 8U}, {'B', 9U}, {'A', 11U}, {'B', 14U}, {'A', 14U}
    };
    expect_trace(expected, sizeof expected / sizeof expected[0]);
    assert(SCH_Delete_Task(once) == APP_NOT_FOUND);
}

static void test_relative_deletion(void)
{
    reset();
    const sch_task_id_t head = add(task_a, 2U, 0U);
    (void)add(task_b, 4U, 0U);
    const sch_task_id_t middle = add(task_a, 5U, 0U);
    (void)add(task_d, 6U, 0U);
    const sch_task_id_t tail = add(task_a, 8U, 0U);
    assert(SCH_Delete_Task(middle) == APP_OK);
    assert(SCH_Delete_Task(head) == APP_OK);
    assert(SCH_Delete_Task(tail) == APP_OK);
    for (uint32_t tick = 1U; tick <= 9U; ++tick) {
        SCH_Update();
        SCH_Dispatch_Tasks();
    }
    const trace_entry_t expected[] = {{'B', 4U}, {'D', 6U}};
    expect_trace(expected, sizeof expected / sizeof expected[0]);

    reset();
    const sch_task_id_t overdue_head = add(task_a, 1U, 0U);
    (void)add(task_b, 3U, 0U);
    advance(4U);
    assert(SCH_Delete_Task(overdue_head) == APP_OK);
    (void)add(task_c, 1U, 0U);
    SCH_Dispatch_Tasks();
    SCH_Update();
    SCH_Dispatch_Tasks();
    const trace_entry_t overdue_expected[] = {{'B', 4U}, {'C', 5U}};
    expect_trace(overdue_expected, sizeof overdue_expected / sizeof overdue_expected[0]);
}

static void test_overrun(void)
{
    reset();
    (void)add(task_a, 2U, 3U);
    (void)add(task_b, 5U, 0U);
    advance(8U);
    (void)add(task_c, 1U, 0U);
    SCH_Dispatch_Tasks();
    SCH_Dispatch_Tasks();
    SCH_Update();
    SCH_Dispatch_Tasks();
    SCH_Update();
    SCH_Dispatch_Tasks();
    SCH_Update();
    SCH_Dispatch_Tasks();
    const trace_entry_t expected[] = {{'A', 8U}, {'B', 8U}, {'C', 9U}, {'A', 11U}};
    expect_trace(expected, sizeof expected / sizeof expected[0]);
}

static void slow_callback(void)
{
    record('A');
    advance(5U);
    (void)add(task_c, 1U, 0U);
    SCH_Dispatch_Tasks(); /* Recursion must leave the current pass intact. */
    SCH_Init(); /* Reset must leave the current callback intact. */
}

static void test_tick_during_dispatch(void)
{
    reset();
    const sch_task_id_t slow = add(slow_callback, 1U, 2U);
    (void)add(task_b, 2U, 0U);
    SCH_Update();
    SCH_Dispatch_Tasks();
    assert(SCH_Now() == 6U && trace_count == 1U);
    SCH_Dispatch_Tasks();
    assert(trace_count == 2U);
    SCH_Update();
    SCH_Dispatch_Tasks();
    assert(SCH_Delete_Task(slow) == APP_OK);
    const trace_entry_t expected[] = {{'A', 1U}, {'B', 6U}, {'C', 7U}, {'A', 7U}};
    expect_trace(expected, sizeof expected / sizeof expected[0]);
}

static void cancel_and_replace(void)
{
    sch_task_id_t id = 99U;
    assert(SCH_Add_Task(task_b, 1U, 0U, &id) == APP_NO_CAPACITY);
    assert(id == 99U);
    assert(SCH_Delete_Task(callback_id) == APP_OK);
    assert(SCH_Delete_Task(callback_id) == APP_NOT_FOUND);
    (void)add(task_b, 1U, 0U);
    record('A');
}

static void test_capacity_and_reuse(void)
{
    reset();
    callback_id = add(cancel_and_replace, 1U, 1U);
    sch_task_id_t ids[15];
    for (size_t i = 0U; i < sizeof ids / sizeof ids[0]; ++i) {
        ids[i] = add(task_d, UINT32_MAX, 0U);
    }
    sch_task_id_t extra = 99U;
    assert(SCH_Add_Task(task_d, 2U, 0U, &extra) == APP_NO_CAPACITY && extra == 99U);
    SCH_Update();
    SCH_Dispatch_Tasks();
    SCH_Update();
    SCH_Dispatch_Tasks();
    const trace_entry_t expected[] = {{'A', 1U}, {'B', 2U}};
    expect_trace(expected, sizeof expected / sizeof expected[0]);
    for (size_t i = 0U; i < sizeof ids / sizeof ids[0]; ++i) {
        assert(SCH_Delete_Task(ids[i]) == APP_OK);
    }
    sch_task_id_t previous = callback_id;
    for (uint32_t i = 0U; i < 1000U; ++i) {
        const sch_task_id_t id = add(task_b, 1U, 0U);
        assert(SCH_Delete_Task(previous) == APP_NOT_FOUND);
        assert(SCH_Delete_Task(id) == APP_OK);
        previous = id;
    }
    reset();
    (void)add(task_a, 1U, 1U);
    assert(SCH_Delete_Task(previous) == APP_NOT_FOUND);
    SCH_Update();
    SCH_Dispatch_Tasks();
    SCH_Update();
    SCH_Dispatch_Tasks();
    const trace_entry_t periodic[] = {{'A', 1U}, {'A', 2U}};
    expect_trace(periodic, sizeof periodic / sizeof periodic[0]);
}

static bool timer_expired(software_timer_id_t id)
{
    bool expired = false;
    assert(software_timer_is_expired(id, &expired) == APP_OK);
    return expired;
}

static uint64_t fake_clock(void) { return fake_tick; }

static void test_software_timers(void)
{
    software_timer_id_t first = 99U;
    software_timer_id_t second = 0U;
    bool expired = false;
    assert(software_timer_start(1U, &first) == APP_NOT_READY && first == 99U);
    assert(software_timer_cancel(first) == APP_NOT_READY);
    assert(software_timer_init_with_clock(NULL) == APP_INVALID_ARGUMENT);
    software_timer_init();
    assert(software_timer_start(0U, &first) == APP_INVALID_ARGUMENT && first == 99U);
    assert(software_timer_start(1U, NULL) == APP_INVALID_ARGUMENT);
    assert(software_timer_is_expired(first, NULL) == APP_INVALID_ARGUMENT);
    assert(software_timer_start(2U, &first) == APP_OK);
    assert(software_timer_start(1U, &second) == APP_OK);
    assert(!timer_expired(first) && !timer_expired(second));
    software_timer_tick();
    assert(!timer_expired(first) && timer_expired(second));
    assert(software_timer_cancel(first) == APP_OK);
    software_timer_tick();
    assert(timer_expired(second));
    assert(software_timer_is_expired(first, &expired) == APP_NOT_FOUND);
    assert(software_timer_cancel(first) == APP_NOT_FOUND);
    assert(software_timer_cancel(second) == APP_OK);

    reset();
    assert(software_timer_init_with_clock(SCH_Now) == APP_OK);
    assert(software_timer_start(4U, &first) == APP_OK);
    advance(3U);
    assert(software_timer_start(2U, &second) == APP_OK);
    advance(1U);
    assert(timer_expired(first) && !timer_expired(second));
    software_timer_tick(); /* Same clock value cannot double-advance. */
    assert(!timer_expired(second));
    advance(1U);
    assert(timer_expired(second));

    const software_timer_id_t stale = first;
    fake_tick = UINT64_MAX - 2U;
    assert(software_timer_init_with_clock(fake_clock) == APP_OK);
    assert(software_timer_start(5U, &first) == APP_OK);
    assert(software_timer_cancel(stale) == APP_NOT_FOUND);
    fake_tick = 1U;
    assert(!timer_expired(first));
    fake_tick = 2U;
    assert(timer_expired(first));
    assert(software_timer_cancel(first) == APP_OK);
}

int main(void)
{
    test_empty_and_invalid();
    test_order_and_periods();
    test_relative_deletion();
    test_overrun();
    test_tick_during_dispatch();
    test_capacity_and_reuse();
    test_software_timers();
    puts("Scheduler and software timer tests passed");
    return 0;
}

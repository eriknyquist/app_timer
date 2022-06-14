#include "unity.h"
#include "app_timer_api.h"

#include <stdint.h>
#include <stdbool.h>


static uint32_t _init_callcount = 0u;
static uint32_t _units_to_timer_counts_callcount = 0u;
static uint32_t _read_timer_counts_callcount = 0u;
static uint32_t _set_timer_period_counts_callcount = 0u;
static uint32_t _set_timer_running_callcount = 0u;
static uint32_t _set_interrupts_enabled_callcount = 0u;

static bool _callcount_init_returnval = true;
static bool _callcount_init(void)
{
    _init_callcount += 1u;
    return _callcount_init_returnval;
}

static app_timer_running_count_t _callcount_units_to_timer_counts_returnval = 0u;
static app_timer_running_count_t _callcount_units_to_timer_counts(app_timer_period_t time)
{
    _units_to_timer_counts_callcount += 1u;
    return _callcount_units_to_timer_counts_returnval;
}

static app_timer_count_t _callcount_read_timer_counts_returnval = 0u;
static app_timer_count_t _callcount_read_timer_counts(void)
{
    _read_timer_counts_callcount += 1u;
    return _callcount_read_timer_counts_returnval;
}

static void _callcount_set_timer_period_counts(app_timer_count_t counts)
{
    _set_timer_period_counts_callcount += 1u;
}

static void _callcount_set_timer_running(bool enabled)
{
    _set_timer_running_callcount += 1u;
}

static void _callcount_set_interrupts_enabled(bool enabled, app_timer_int_status_t *status)
{
    _set_interrupts_enabled_callcount += 1u;
}

static app_timer_hw_model_t _hw_model =
{
    .max_count = 0u, // Will set this per-test as needed
    .init = _callcount_init,
    .units_to_timer_counts = _callcount_units_to_timer_counts,
    .read_timer_counts = _callcount_read_timer_counts,
    .set_timer_period_counts = _callcount_set_timer_period_counts,
    .set_timer_running = _callcount_set_timer_running,
    .set_interrupts_enabled = _callcount_set_interrupts_enabled
};

// Max. size for any call argument stack
#define MAX_EXPECT_COUNT (32u)


// Mock set_timer_running functions + stack for call arguments

typedef struct
{
    bool enabled;
} set_timer_running_args_t;

typedef struct
{
    set_timer_running_args_t args[MAX_EXPECT_COUNT];
    uint32_t count;
    uint32_t pos;
} set_timer_running_stack_t;

static set_timer_running_stack_t _set_timer_running_stack = {.count=0u, .pos=0u};

static void _mock_set_timer_running(bool enabled)
{
    if (_set_timer_running_stack.pos >= _set_timer_running_stack.count)
    {
        TEST_FAIL_MESSAGE("hw_model->set_timer_running was called more times than expected");
        return;
    }

    uint32_t pos = _set_timer_running_stack.pos;
    set_timer_running_args_t *args = &_set_timer_running_stack.args[pos];

    if (args->enabled != enabled)
    {
        TEST_FAIL_MESSAGE("unexpected arg passed to hw_model->set_timer_running");
    }

    _set_timer_running_stack.pos += 1u;
}

static void _set_timer_running_expect(bool enabled)
{
    uint32_t count = _set_timer_running_stack.count;
    set_timer_running_args_t *args = &_set_timer_running_stack.args[count];
    args->enabled = enabled;
    _set_timer_running_stack.count += 1u;
}


// Mock set_interrupts_enabled functions + stack for call arguments

typedef struct
{
    bool enabled;
} set_interrupts_enabled_args_t;

typedef struct
{
    set_interrupts_enabled_args_t args[MAX_EXPECT_COUNT];
    uint32_t count;
    uint32_t pos;
} set_interrupts_enabled_stack_t;

static set_interrupts_enabled_stack_t _set_interrupts_enabled_stack = {.count=0u, .pos=0u};

static void _mock_set_interrupts_enabled(bool enabled, app_timer_int_status_t *status)
{
    if (_set_interrupts_enabled_stack.pos >= _set_interrupts_enabled_stack.count)
    {
        TEST_FAIL_MESSAGE("hw_model->set_interrupts_enabled was called more times than expected");
        return;
    }

    uint32_t pos = _set_interrupts_enabled_stack.pos;
    set_interrupts_enabled_args_t *args = &_set_interrupts_enabled_stack.args[pos];

    if (args->enabled != enabled)
    {
        TEST_FAIL_MESSAGE("unexpected arg value passed to hw_model->set_interrupts_enabled");
    }

    _set_interrupts_enabled_stack.pos += 1u;
}

static void _set_interrupts_enabled_expect(bool enabled)
{
    uint32_t count = _set_interrupts_enabled_stack.count;
    set_interrupts_enabled_args_t *args = &_set_interrupts_enabled_stack.args[count];
    args->enabled = enabled;
    _set_interrupts_enabled_stack.count += 1u;
}


void setUp(void)
{
    _init_callcount = 0u;
    _units_to_timer_counts_callcount = 0u;
    _read_timer_counts_callcount = 0u;
    _set_timer_period_counts_callcount = 0u;
    _set_timer_running_callcount = 0u;
    _set_interrupts_enabled_callcount = 0u;

    _set_timer_running_stack.pos = 0u;
    _set_timer_running_stack.count = 0u;
}

void checkExpectedCalls(void)
{
    if (_set_timer_running_stack.pos != _set_timer_running_stack.count)
    {
        TEST_FAIL_MESSAGE("hw_model->set_timer_running called fewer times than expected");
    }

    if (_set_interrupts_enabled_stack.pos != _set_interrupts_enabled_stack.count)
    {
        TEST_FAIL_MESSAGE("hw_model->set_interrupts_enabled called fewer times than expected");
    }
}

void tearDown(void)
{
    checkExpectedCalls();
}


// Handler that does nothing, for cases where I just need a valid handler pointer
static void _dummy_handler(void *context)
{
    ; // Do nothing
}

// Tests that app_timer_create returns expected error code when module is not initialized
void test_app_timer_create_not_init(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_STATE, app_timer_create(&t, _dummy_handler, APP_TIMER_TYPE_REPEATING));
}

// Tests that app_timer_start returns expected error code when module is not initialized
void test_app_timer_start_not_init(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_STATE, app_timer_start(&t, 1000u, NULL));
}

// Tests that app_timer_stop returns expected error code when module is not initialized
void test_app_timer_stop_not_init(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_STATE, app_timer_stop(&t));
}

// Tests that app_timer_is_active returns expected error code when module is not initialized
void test_app_timer_is_active_not_init(void)
{
    app_timer_t t;
    bool active;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_STATE, app_timer_is_active(&t, &active));
}

// Tests that app_timer_init returns expected error when NULL HW model is passed
void test_app_timer_init_null_hwmodel_ptr(void)
{
    TEST_ASSERT_EQUAL_INT(APP_TIMER_NULL_PARAM, app_timer_init(NULL));
}

// Tests that app_timer_init returns expected error when HW model contains
// invalid value for 'max_count' field
void test_app_timer_init_max_count_invalid(void)
{
    _hw_model.max_count = 0u;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_init(&_hw_model));
    _hw_model.max_count = 0xffffu; // Restore valid max_count
}

// Tests that app_timer_init returns expected error code when HW model contains
// NULL pointer for 'init' function
void test_app_timer_init_null_init(void)
{
    void *old_val = _hw_model.init;
    _hw_model.init = NULL;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_init(&_hw_model));
    _hw_model.init = old_val; // Restore valid value
}

// Tests that app_timer_init returns expected error code when HW model contains
// NULL pointer for 'units_to_timer_counts' function
void test_app_timer_init_null_units_to_timer_counts(void)
{
    void *old_val = _hw_model.units_to_timer_counts;
    _hw_model.units_to_timer_counts = NULL;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_init(&_hw_model));
    _hw_model.units_to_timer_counts = old_val; // Restore valid value
}

// Tests that app_timer_init returns expected error code when HW model contains
// NULL pointer for 'read_timer_counts' function
void test_app_timer_init_null_read_timer_counts(void)
{
    void *old_val = _hw_model.read_timer_counts;
    _hw_model.read_timer_counts = NULL;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_init(&_hw_model));
    _hw_model.read_timer_counts = old_val; // Restore valid value
}

// Tests that app_timer_init returns expected error code when HW model contains
// NULL pointer for 'set_timer_period_counts' function
void test_app_timer_init_null_set_timer_period_counts(void)
{
    void *old_val = _hw_model.set_timer_period_counts;
    _hw_model.set_timer_period_counts = NULL;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_init(&_hw_model));
    _hw_model.set_timer_period_counts = old_val; // Restore valid value
}

// Tests that app_timer_init returns expected error code when HW model contains
// NULL pointer for 'set_timer_running' function
void test_app_timer_init_null_set_timer_running(void)
{
    void *old_val = _hw_model.set_timer_running;
    _hw_model.set_timer_running = NULL;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_init(&_hw_model));
    _hw_model.set_timer_running = old_val; // Restore valid value
}

// Tests that app_timer_init returns expected error code when HW model contains
// NULL pointer for 'set_interrupts_enabled' function
void test_app_timer_init_null_set_interrupts_enabled(void)
{
    void *old_val = _hw_model.set_interrupts_enabled;
    _hw_model.set_interrupts_enabled = NULL;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_init(&_hw_model));
    _hw_model.set_interrupts_enabled = old_val; // Restore valid value
}

// Tests that app_timer_init returns expected error when HW model init. function fails
void test_app_timer_init_hwmodel_init_fail(void)
{
    _callcount_init_returnval = false;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_ERROR, app_timer_init(&_hw_model));
    _callcount_init_returnval = true;
}

// Tests that app_timer_init succeeds in the happy path
void test_app_timer_init_success(void)
{
    void *old_set_timer_running = _hw_model.set_timer_running;
    void *old_set_interrupts_enabled = _hw_model.set_interrupts_enabled;

    _hw_model.set_timer_running = _mock_set_timer_running;
    _set_timer_running_expect(false);

    _hw_model.set_interrupts_enabled = _mock_set_interrupts_enabled;
    _set_interrupts_enabled_expect(true);

    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_init(&_hw_model));

    // Restore valid values
    _hw_model.set_timer_running = old_set_timer_running;
    _hw_model.set_interrupts_enabled = old_set_interrupts_enabled;
}

// Tests that app_timer_create returns expected error code when NULL timer is passed
void test_app_timer_create_null_timer(void)
{
    TEST_ASSERT_EQUAL_INT(APP_TIMER_NULL_PARAM, app_timer_create(NULL, _dummy_handler, APP_TIMER_TYPE_REPEATING));
}

// Tests that app_timer_create returns expected error code when invalid type is passed
void test_app_timer_create_invalid_type(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_create(&t, _dummy_handler, APP_TIMER_TYPE_COUNT));
}

// Tests that app_timer_create succeeds in the happy path (repeating timer)
void test_app_timer_create_success_repeating(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_create(&t, _dummy_handler, APP_TIMER_TYPE_REPEATING));

    TEST_ASSERT_EQUAL_PTR(_dummy_handler, t.handler);
    TEST_ASSERT_EQUAL_INT(0, t.start_counts);
    TEST_ASSERT_EQUAL_INT(0, t.total_counts);
    TEST_ASSERT_EQUAL_PTR(NULL, t.next);
    TEST_ASSERT_EQUAL_PTR(NULL, t.previous);
    TEST_ASSERT_EQUAL_INT(2, t.flags);
}

// Tests that app_timer_create succeeds in the happy path (single-shot timer)
void test_app_timer_create_success_single_shot(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_create(&t, _dummy_handler, APP_TIMER_TYPE_SINGLE_SHOT));

    TEST_ASSERT_EQUAL_PTR(_dummy_handler, t.handler);
    TEST_ASSERT_EQUAL_INT(0, t.start_counts);
    TEST_ASSERT_EQUAL_INT(0, t.total_counts);
    TEST_ASSERT_EQUAL_PTR(NULL, t.next);
    TEST_ASSERT_EQUAL_PTR(NULL, t.previous);
    TEST_ASSERT_EQUAL_INT(0, t.flags);
}

// Tests that app_timer_is_active returns expected error code when NULL pointer given for timer
void test_app_timer_is_active_null_timer(void)
{
    bool active;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_NULL_PARAM, app_timer_is_active(NULL, &active));
}

// Tests that app_timer_is_active returns expected error code when NULL pointer given for result
void test_app_timer_is_active_null_result(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_NULL_PARAM, app_timer_is_active(&t, NULL));
}

// Tests that app_timer_is_active behaves as expected, before and after starting & stopping
// a timer instance
void test_app_timer_is_active_repeating_success(void)
{
    app_timer_t t;

    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_create(&t, _dummy_handler, APP_TIMER_TYPE_REPEATING));

    // verify timer is not active yet
    bool active;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_is_active(&t, &active));
    TEST_ASSERT_FALSE(active);

    // start timer
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_start(&t, 1000u, NULL));

    // timer should be active now
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_is_active(&t, &active));
    TEST_ASSERT_TRUE(active);

    // stop timer
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_stop(&t));

    // timer should be inactive again now
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_is_active(&t, &active));
    TEST_ASSERT_FALSE(active);
}

// Tests that app_timer_start returns expected error code when NULL pointer passed for timer
void test_app_timer_start_null_timer(void)
{
    TEST_ASSERT_EQUAL_INT(APP_TIMER_NULL_PARAM, app_timer_start(NULL, 10u, NULL));
}

// Tests that app_timer_start returns expected error code when invalid time provided
void test_app_timer_start_invalid_time(void)
{
    app_timer_t t;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_create(&t, _dummy_handler, APP_TIMER_TYPE_REPEATING));
    TEST_ASSERT_EQUAL_INT(APP_TIMER_INVALID_PARAM, app_timer_start(&t, 0u, NULL));

    // verify timer is not active yet
    bool active;
    TEST_ASSERT_EQUAL_INT(APP_TIMER_OK, app_timer_is_active(&t, &active));
    TEST_ASSERT_FALSE(active);
}

// tests that app_timer_start
int main(void)
{
    UNITY_BEGIN();

    // Tests for all functions before module initialization
    RUN_TEST(test_app_timer_create_not_init);
    RUN_TEST(test_app_timer_start_not_init);
    RUN_TEST(test_app_timer_stop_not_init);
    RUN_TEST(test_app_timer_is_active_not_init);

    // Tests for app_timer_init
    RUN_TEST(test_app_timer_init_null_hwmodel_ptr);
    RUN_TEST(test_app_timer_init_max_count_invalid);
    RUN_TEST(test_app_timer_init_null_init);
    RUN_TEST(test_app_timer_init_null_units_to_timer_counts);
    RUN_TEST(test_app_timer_init_null_read_timer_counts);
    RUN_TEST(test_app_timer_init_null_set_timer_period_counts);
    RUN_TEST(test_app_timer_init_null_set_timer_running);
    RUN_TEST(test_app_timer_init_null_set_interrupts_enabled);
    RUN_TEST(test_app_timer_init_hwmodel_init_fail);
    RUN_TEST(test_app_timer_init_success);

    // Tests for app_timer_create
    RUN_TEST(test_app_timer_create_null_timer);
    RUN_TEST(test_app_timer_create_invalid_type);
    RUN_TEST(test_app_timer_create_success_repeating);
    RUN_TEST(test_app_timer_create_success_single_shot);

    // Tests for app_timer_is_active
    RUN_TEST(test_app_timer_is_active_null_timer);
    RUN_TEST(test_app_timer_is_active_null_result);
    RUN_TEST(test_app_timer_is_active_repeating_success);

    // Tests for app_timer_start and app_timer_is_valid
    RUN_TEST(test_app_timer_start_null_timer);
    RUN_TEST(test_app_timer_start_invalid_time);

    return UNITY_END();
}

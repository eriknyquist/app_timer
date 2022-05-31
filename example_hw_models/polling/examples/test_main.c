#include <stdio.h>
#include <inttypes.h>

#include "app_timer_api.h"
#include "polling_app_timer.h"
#include "timing.h"


#define TOTAL_TEST_TIME_SECONDS (60u * 60u)


#define NUM_SINGLE_TIMERS (128u)
#define NUM_REPEAT_TIMERS (128u)
#define NUM_TEST_TIMERS (NUM_SINGLE_TIMERS + NUM_REPEAT_TIMERS)

#define SINGLE_PERIOD_START_MS (200u)
#define SINGLE_PERIOD_INCREMENT_MS (20u)

#define REPEAT_PERIOD_START_MS (210u)
#define REPEAT_PERIOD_INCREMENT_MS (20u)


/**
 * Represents a single timer instance and all data needed to test it
 */
typedef struct
{
    app_timer_t timer;       ///< Timer instance
    uint32_t ms;             ///< Timer period in milliseconds
    uint64_t last_us;        ///< Last timer expiration timestamp
    int64_t sum_diff_us;     ///< Sum of all differences between timer period and measured period
    int64_t lowest_diff_us;  ///< Lowest deviation seen (from timer period)
    int64_t highest_diff_us; ///< Highest deviation seen (from timer period)
    uint64_t expirations;    ///< Total number of times this timer has expired
} test_timer_t;


/**
 * Holds summary information from processing a completed test
 */
typedef struct
{
    double lowest_avg_percent;
    double highest_avg_percent;
    double average_avg_percent;

    double lowest_avg_ms;
    double highest_avg_ms;
    double average_avg_ms;

    double lowest_ms;
    double highest_ms;

    uint64_t expirations_not_plus1_count;
    uint64_t expirations_plus1_count;
    uint64_t total_timers_with_expirations;
    uint64_t total_expected_expirations;
    uint64_t total_actual_expirations;
} test_results_summary_t;


static test_timer_t _test_timers[NUM_TEST_TIMERS] = {0};


static void _process_timer_expiration(test_timer_t *t)
{
    t->expirations += 1u;

    uint64_t now_us = timing_usecs_elapsed();
    uint64_t actual_period_us = now_us - t->last_us;

    int64_t diff = ((int64_t) actual_period_us) - (((int64_t) t->ms) * 1000);
    t->sum_diff_us += diff;
    t->last_us = now_us;

    // Get absolute value of difference
    uint64_t abs_diff = (diff < 0LL) ? ((uint64_t) (diff * -1LL)) : (uint64_t) diff;

    if (abs_diff > t->highest_diff_us)
    {
        t->highest_diff_us = abs_diff;
    }

    if (abs_diff < t->lowest_diff_us)
    {
        t->lowest_diff_us = abs_diff;
    }
}

// Timer callback for all single-shot timers
static void _single_timer_callback(void *context)
{
    test_timer_t *t = (test_timer_t *) context;

    _process_timer_expiration(t);

    // Re-start timer instance
    app_timer_error_e err = app_timer_start(&t->timer, t->ms, t);
    if (APP_TIMER_OK != err)
    {
        printf("app_timer_create failed, err: 0x%x", err);
    }
}

// Timer callback for all repeating timers
static void _repeat_timer_callback(void *context)
{
    test_timer_t *t = (test_timer_t *) context;
    _process_timer_expiration(t);
}


static void _dump_test_results(test_results_summary_t *results)
{
    results->total_timers_with_expirations = 0u;
    results->total_expected_expirations = 0u;
    results->total_actual_expirations = 0u;
    results->expirations_not_plus1_count = 0u;
    results->expirations_plus1_count = 0u;

    results->highest_avg_percent = 0.0f;
    results->lowest_avg_percent = 100.0f;
    double sum_percent = 0.0f;

    results->highest_avg_ms = 0.0f;
    results->lowest_avg_ms = 99999999.0f;
    double sum_ms = 0.0f;

    results->highest_ms = 0.0f;
    results->lowest_ms = 99999999.0f;

    uint32_t total_time_ms = TOTAL_TEST_TIME_SECONDS * 1000UL;

    // Dump state for all single-shot timer instances
    for (uint32_t i = 0u; i < NUM_TEST_TIMERS; i++)
    {
        uint64_t expected_expirations = (total_time_ms - 1UL) / _test_timers[i].ms;
        results->total_expected_expirations += expected_expirations;

        // Check if number of expirations matches expected
        if (expected_expirations != _test_timers[i].expirations)
        {
            int64_t int_diff = ((int64_t) expected_expirations) - ((int64_t) _test_timers[i].expirations);
            uint64_t abs_diff = (int_diff < 0) ? ((uint64_t) int_diff * -1) : ((uint64_t) int_diff);

            if (abs_diff > 1UL)
            {
                results->expirations_not_plus1_count += 1UL;
            }
            else
            {
                results->expirations_plus1_count += 1UL;
            }

            printf("WARNING: timer #%u expired %"PRIu64" times, but expected %"PRIu64" (period is %ums, total time is %ums)\n",
                   i, _test_timers[i].expirations, expected_expirations, _test_timers[i].ms, total_time_ms);
        }

        printf("timer #%u", i);

        if (i < NUM_SINGLE_TIMERS)
        {
            printf("(single): ");
        }
        else
        {
            printf("(repeat): ");
        }

        printf("period=%ums ", _test_timers[i].ms);

        if (0u < _test_timers[i].expirations)
        {
            int64_t avg_diff_us = _test_timers[i].sum_diff_us / (int64_t) _test_timers[i].expirations;
            double avg_diff_ms = ((double) avg_diff_us) / 1000.0;
            double percent_of_period = avg_diff_ms / (((double) _test_timers[i].ms) / 100.0f);

            double lowest_diff_ms = ((double) _test_timers[i].lowest_diff_us) / 1000.0f;
            double highest_diff_ms = ((double) _test_timers[i].highest_diff_us) / 1000.0f;

            if (lowest_diff_ms < results->lowest_ms)
            {
                results->lowest_ms = lowest_diff_ms;
            }

            if (highest_diff_ms > results->highest_ms)
            {
                results->highest_ms = highest_diff_ms;
            }

            if (avg_diff_ms < results->lowest_avg_ms)
            {
                results->lowest_avg_ms = avg_diff_ms;
            }

            if (avg_diff_ms > results->highest_avg_ms)
            {
                results->highest_avg_ms = avg_diff_ms;
            }

            if (percent_of_period < results->lowest_avg_percent)
            {
                results->lowest_avg_percent = percent_of_period;
            }

            if (percent_of_period > results->highest_avg_percent)
            {
                results->highest_avg_percent = percent_of_period;
            }

            results->total_timers_with_expirations += 1u;
            results->total_actual_expirations += _test_timers[i].expirations;
            sum_ms += avg_diff_ms;
            sum_percent += percent_of_period;

            printf("expirations=%"PRIu64", avg_diff=%.2fms (%.2f%% of period)\n",
                   _test_timers[i].expirations, avg_diff_ms, percent_of_period);
        }
        else
        {
            printf("(never expired)\n");
        }
    }

    results->average_avg_percent = sum_percent / ((double) results->total_actual_expirations);
    results->average_avg_ms = sum_ms / ((double) results->total_actual_expirations);
}


int main(int argc, char *argv[])
{
    // Initialize polling_app_timer
    polling_app_timer_init();

    app_timer_error_e err = APP_TIMER_OK;

    uint32_t period_ms = SINGLE_PERIOD_START_MS;
    uint32_t lowest_period_ms = period_ms;
    uint32_t highest_period_ms = period_ms;

    // Initialize all single-shot timer instances
    printf("\n");
    printf("starting microseconds timestamp (uint32): %u\n\n", (uint32_t) timing_usecs_elapsed());
    printf("- initializing %u single-shot timers and %u repeating timers\n",
           NUM_SINGLE_TIMERS, NUM_REPEAT_TIMERS);

    for (uint32_t i = 0u; i < NUM_SINGLE_TIMERS; i++)
    {
        _test_timers[i].ms = period_ms;
        _test_timers[i].last_us = timing_usecs_elapsed();
        _test_timers[i].sum_diff_us = 0LL;
        _test_timers[i].highest_diff_us = 0LL;
        _test_timers[i].lowest_diff_us = 0LL;
        _test_timers[i].expirations = 0UL;

        err = app_timer_create(&_test_timers[i].timer, &_single_timer_callback, APP_TIMER_TYPE_SINGLE_SHOT);
        if (APP_TIMER_OK != err)
        {
            printf("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        // Start timer instance
        err = app_timer_start(&_test_timers[i].timer, period_ms, &_test_timers[i]);
        if (APP_TIMER_OK != err)
        {
            printf("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        if (period_ms > highest_period_ms)
        {
            highest_period_ms = period_ms;
        }

        period_ms += SINGLE_PERIOD_INCREMENT_MS;
    }

    period_ms = REPEAT_PERIOD_START_MS;

    // Initialize all repeating timer instances
    printf("- initializing %u repeating timers...\n", NUM_REPEAT_TIMERS);
    for (uint32_t i = NUM_SINGLE_TIMERS; i < NUM_TEST_TIMERS; i++)
    {
        _test_timers[i].ms = period_ms;
        _test_timers[i].last_us = timing_usecs_elapsed();
        _test_timers[i].sum_diff_us = 0LL;
        _test_timers[i].highest_diff_us = 0LL;
        _test_timers[i].lowest_diff_us = 0LL;
        _test_timers[i].expirations = 0UL;

        err = app_timer_create(&_test_timers[i].timer, &_repeat_timer_callback, APP_TIMER_TYPE_REPEATING);
        if (APP_TIMER_OK != err)
        {
            printf("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        // Start timer instance
        err = app_timer_start(&_test_timers[i].timer, period_ms, &_test_timers[i]);
        if (APP_TIMER_OK != err)
        {
            printf("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        if (period_ms > highest_period_ms)
        {
            highest_period_ms = period_ms;
        }

        period_ms += REPEAT_PERIOD_INCREMENT_MS;
    }

    printf("- created %u timers, with periods ranging from %u-%ums\n",
           NUM_TEST_TIMERS, lowest_period_ms, highest_period_ms);

    printf("- %zu bytes of memory used\n", sizeof(_test_timers));

    printf("\n");
    printf("- running %u timers for %u seconds...\n", NUM_TEST_TIMERS, TOTAL_TEST_TIME_SECONDS);
    printf("\n");

    uint64_t start_us = timing_usecs_elapsed();
    uint64_t total_test_time_us = TOTAL_TEST_TIME_SECONDS * 1000000ULL;

    // Enter polling loop-- polling_app_timer_poll must be called regularly
    while ((timing_usecs_elapsed() - start_us) <= total_test_time_us)
    {
        polling_app_timer_poll();
    }

    // Stop all the timers
    for (uint32_t i = 0u; i < NUM_TEST_TIMERS; i++)
    {
        err = app_timer_stop(&_test_timers[i].timer);
        if (APP_TIMER_OK != err)
        {
            printf("app_timer_stop failed, err: 0x%x", err);
            return err;
        }
    }

    test_results_summary_t results;

    _dump_test_results(&results),

    printf("\n\n");
    printf("Summary:\n\n");
    printf("ending microseconds timestamp (uint32): %u\n\n", (uint32_t) timing_usecs_elapsed());
    printf("Ran %u timers at once (%u single-shot and %u repeating), all with different\n",
           NUM_TEST_TIMERS, NUM_SINGLE_TIMERS, NUM_REPEAT_TIMERS);
    printf("periods between %u-%u milliseconds, for %u seconds total.\n\n",
           lowest_period_ms, highest_period_ms, TOTAL_TEST_TIME_SECONDS);

    printf("No. of timers that differ from expected expiration counts by more than 1:\n");
    printf("- %"PRIu64"\n\n", results.expirations_not_plus1_count);

    printf("No. of timers that differ from expected expiration counts by 1:\n");
    printf("- %"PRIu64"\n\n", results.expirations_plus1_count);

    printf("Diff. between expected and measured period (as a relative percentage of timer period):\n");
    printf("- Highest average seen for a single timer: %.2f\n", results.highest_avg_percent);
    printf("- Lowest average seen for a single timer: %.2f\n", results.lowest_avg_percent);
    printf("- Average across all timers: %.2f\n\n", results.average_avg_percent);

    printf("Diff. between expected and measured period (in absolute milliseconds):\n");
    printf("- Highest average seen for a single timer: %.2f\n", results.highest_avg_ms);
    printf("- Lowest average seen for a single timer: %.2f\n", results.lowest_avg_ms);
    printf("- Average across all timers: %.2f\n", results.average_avg_ms);
    printf("- Absolute lowest seen across all timers: %.2f\n", results.lowest_ms);
    printf("- Absolute highest seen across all timers: %.2f\n\n", results.highest_ms);

    int64_t total_expected_exp = results.total_expected_expirations;
    int64_t total_actual_exp = results.total_actual_expirations;
    int64_t exp_diff = total_actual_exp - total_expected_exp;
    const char *desc = (exp_diff < 0) ? "fewer" : "more";
    uint64_t abs_diff = (exp_diff < 0) ? (uint64_t) (exp_diff * -1) : (uint64_t) exp_diff;
    double diff_percent = ((double) abs_diff) / (((double) results.total_expected_expirations) / 100.0);

    printf("Diff. between expected and actual total expiration count:\n");
    printf("- %"PRIu64" total expirations occurred, out of expected %"PRIu64"\n",
           results.total_actual_expirations, results.total_expected_expirations);

    printf("- Saw %.2f%% %s expirations than expected\n\n",
           diff_percent, desc);
}

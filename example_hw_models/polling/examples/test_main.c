/**
 * Test program which creates a configurable number of single-shot and repeating timers,
 * all with different periods, and runs them all at once for a configurable period of time
 * using the polling app_timer HW model. When the configured runtime has elapsed, all
 * timers are stopped, and some collected information / statistics about the overall
 * accuracy and behaviour of all timers is printed to stdout.
 *
 * This is a useful smoke test to verify basic functionality and correctness of
 * maintaining notion of time, which can be easily run on a windows or linux development
 * system. The only thing we can't really test here is interrupt safefty (app_timer_target_count_reached
 * and other app_timer functions are not being called in interrupt contexts, as they may
 * be on an embedded system).
 */


#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

#include "app_timer_api.h"
#include "polling_app_timer.h"
#include "timing.h"


// If set to 1, extra information is printed about each timer instance at the end of the test
#ifndef VERBOSE
#define VERBOSE (0u)
#endif // VERBOSE


#define TOTAL_TEST_TIME_SECONDS (10 * 60u)   ///< Total runtime for all timers, seconds
#define TIME_LOG_INTERVAL_SECS  (60u)        ///< How often to log runtime remaining, seconds

#define NUM_SINGLE_TIMERS (128u)             ///< Number of single-shot timers to create (re-started in timer callback)
#define NUM_REPEAT_TIMERS (128u)             ///< Number of repeating timers to create
#define NUM_TEST_TIMERS \
    (NUM_SINGLE_TIMERS + NUM_REPEAT_TIMERS)  ///< Total number of timers

#define SINGLE_PERIOD_START_MS (200u)        ///< Period of first single-shot timer, in millisecs
#define SINGLE_PERIOD_INCREMENT_MS (50u)     ///< Increment period for each subsequent single-shot timer by this much

#define REPEAT_PERIOD_START_MS (225u)        ///< Period of first repeating timer, in millisecs
#define REPEAT_PERIOD_INCREMENT_MS (50u)     ///< Increment period for each subsequent repeating timer by this much

#define MAX_LOG_MSG_SIZE (256u)              ///< Log messages printed to stdout can't be larger than this


/**
 * Represents a single timer instance and all data needed to test it
 */
typedef struct
{
    app_timer_t timer;       ///< Timer instance
    uint32_t ms;             ///< Timer period in milliseconds
    uint64_t first_us;       ///< First timer expiration timestamp
    uint64_t last_us;        ///< Last timer expiration timestamp
    int64_t sum_diff_us;     ///< Sum of all differences between timer period and measured period
    int64_t lowest_diff_us;  ///< Lowest deviation seen (from timer period)
    int64_t highest_diff_us; ///< Highest deviation seen (from timer period)
    uint64_t expirations;    ///< Total number of times this timer has expired
    bool first;              ///< Set to true before the first expiration
} test_timer_t;


/**
 * Holds summary information from processing a completed test
 */
typedef struct
{
    double lowest_avg_percent;   ///< Lowest avg. devation in % from expected period for any 1 timer
    double highest_avg_percent;  ///< Highest avg. devation in % from expected period for any 1 timer
    double average_avg_percent;  ///< Average deviation in % from expected period across all timers
    double lowest_percent;       ///< Absolute lowest deviation in % from expected period across all timers
    double highest_percent;      ///< Absolute highest deviation in % from expected period across all timers

    double lowest_avg_ms;        ///< Lowest avg. deviation in millisecs from expected period for any 1 timer
    double highest_avg_ms;       ///< Highest avg. deviation in millisecs from expected period for any 1 timer
    double average_avg_ms;       ///< Average devation in millisecs from expected period across all timers
    double lowest_ms;            ///< Absolute lowest deviation in millisecs from expected period across all timers
    double highest_ms;           ///< Absolute highest deviation in millisecs from expected period across all timers

    uint64_t expirations_not_plus1_count;    ///< Timers that differ from expected expiration count by more than 1
    uint64_t expirations_plus1_count;        ///< Timers that differ from expected expiration count by exactly 1
    uint64_t total_timers_with_expirations;  ///< Total number of timers that had a non-zero number of expirations
    uint64_t total_expected_expirations;     ///< Total number of expected expirations across all timers
    uint64_t total_actual_expirations;       ///< Total number of actual expirations across all timers
    uint64_t highest_expiration_diff;        ///< Absolute highest deviation from expected expiration count across all timers

    uint32_t longest_timer_expirations;          ///< Number of timers t he longerst timer expired
    uint32_t longest_timer_expected_expirations; ///< Number of timers t he longerst timer expired
    uint32_t longest_timer_period_ms;            ///< Longest timer period in milliseconds
    uint32_t longest_timer_error_us;             ///< Longest timer total accumulated error rate, in microseconds
} test_results_summary_t;


static test_timer_t _test_timers[NUM_TEST_TIMERS] = {0};

uint64_t _start_us = 0u;


#define NUMSUFFIXES (7)

static const char *size_suffixes[NUMSUFFIXES] =
{
    "EB", "PB", "TB", "GB", "MB", "KB", "B"
};

#define EXABYTES                (1024ULL * 1024ULL * 1024ULL * 1024ULL * \
                                 1024ULL * 1024ULL)

int sizesprint(size_t size, char *buf, unsigned int bufsize)
{
    int ret = 0;
    uint64_t mult = EXABYTES;

    for (int i = 0; i < NUMSUFFIXES; i++, mult /= 1024ULL)
    {
        if (size < mult)
        {
            continue;
        }

        if (mult && (size % mult) == 0)
        {
            ret = snprintf(buf, bufsize, "%"PRIu64"%s", size / mult, size_suffixes[i]);
        }
        else
        {
            ret = snprintf(buf, bufsize, "%.2f%s", (float) size / mult, size_suffixes[i]);
        }

        break;
    }

    return ret;
}


static void _log(const char *fmt, ...)
{
    char buf[MAX_LOG_MSG_SIZE];
    uint64_t usecs = timing_usecs_elapsed() - _start_us;
    uint32_t secs = (uint32_t) (usecs / 1000000u);
    uint32_t msecs_remaining = ((usecs % 1000000u) / 1000u);
    int written = snprintf(buf, sizeof(buf), "[%05us %03ums] ", secs, msecs_remaining);

    va_list args;
    va_start(args, fmt);
    (void) vsnprintf(buf + written, sizeof(buf) - written, fmt, args);
    va_end(args);

    printf("%s", buf);
}


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

    if (t->first)
    {
        // If first expiration, save first_us timestamp
        t->first = false;
        t->first_us = now_us;
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
        _log("app_timer_create failed, err: 0x%x", err);
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
    results->highest_expiration_diff = 0u;

    results->highest_avg_percent = 0.0f;
    results->lowest_avg_percent = 100.0f;
    results->highest_percent = 0.0f;
    results->lowest_percent = 100.0f;
    double sum_percent = 0.0f;

    results->highest_avg_ms = 0.0f;
    results->lowest_avg_ms = 99999999.0f;
    double sum_ms = 0.0f;

    results->highest_ms = 0.0f;
    results->lowest_ms = 99999999.0f;

    uint64_t total_time_ms = TOTAL_TEST_TIME_SECONDS * 1000u;

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

            const char *msg_type = "unset";

            if (abs_diff > results->highest_expiration_diff)
            {
                results->highest_expiration_diff = abs_diff;
            }

            if (abs_diff > 1UL)
            {
                msg_type = "ERROR";
                results->expirations_not_plus1_count += 1UL;
            }
            else
            {
                msg_type = "WARNING";
                results->expirations_plus1_count += 1UL;
            }

            _log("%s timer #%u: %"PRIu64" expirations, but expected %"PRIu64" (diff=%"PRIi64", period=%ums)\n",
                 msg_type, i, _test_timers[i].expirations, expected_expirations, int_diff, _test_timers[i].ms);
        }

#if VERBOSE
        _log("timer #%u", i);

        if (i < NUM_SINGLE_TIMERS)
        {
            printf("(single): ");
        }
        else
        {
            printf("(repeat): ");
        }

        printf("period=%ums ", _test_timers[i].ms);
#endif // VERBOSE

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
                results->lowest_percent = lowest_diff_ms / (((double) _test_timers[i].ms) / 100.0f);
            }

            if (highest_diff_ms > results->highest_ms)
            {
                results->highest_ms = highest_diff_ms;
                results->highest_percent = highest_diff_ms / (((double) _test_timers[i].ms) / 100.0f);
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

#if VERBOSE
            printf("expirations=%"PRIu64", avg_diff=%.2fms (%.2f%% of period)\n",
                   _test_timers[i].expirations, avg_diff_ms, percent_of_period);
#endif // VERBOSE
        }
        else
        {
#if VERBOSE
            printf("(never expired)\n");
#endif // VERBOSE
        }
    }

    results->average_avg_percent = sum_percent / ((double) results->total_actual_expirations);
    results->average_avg_ms = sum_ms / ((double) results->total_actual_expirations);

    // Grab the last timer in the table, which should have the longest period
    test_timer_t *longest = &_test_timers[NUM_TEST_TIMERS - 1u];

    // Calculate total accumulated timing error for this timer, based on first
    // and last expiration timestamps
    uint64_t period_us = longest->ms * 1000u;
    uint64_t total_time_us = longest->last_us - longest->first_us;
    uint32_t error_us = (uint32_t) (total_time_us % period_us);

    results->longest_timer_expirations = longest->expirations;
    results->longest_timer_expected_expirations = (total_time_ms - 1UL) / longest->ms;
    results->longest_timer_period_ms = longest->ms;
    results->longest_timer_error_us = error_us;
}


int main(int argc, char *argv[])
{
    // Initialize polling_app_timer
    polling_app_timer_init();

    _start_us = timing_usecs_elapsed();

    app_timer_error_e err = APP_TIMER_OK;

    uint32_t period_ms = SINGLE_PERIOD_START_MS;
    uint32_t lowest_period_ms = period_ms;
    uint32_t highest_period_ms = period_ms;

    // Initialize all single-shot timer instances
    printf("\n");
    uint32_t start_usecs = (uint32_t) timing_usecs_elapsed();
    for (uint32_t i = 0u; i < NUM_SINGLE_TIMERS; i++)
    {
        _test_timers[i].ms = period_ms;
        _test_timers[i].last_us = timing_usecs_elapsed();
        _test_timers[i].sum_diff_us = 0LL;
        _test_timers[i].highest_diff_us = 0LL;
        _test_timers[i].lowest_diff_us = 0LL;
        _test_timers[i].expirations = 0UL;
        _test_timers[i].first = true;

        err = app_timer_create(&_test_timers[i].timer, &_single_timer_callback, APP_TIMER_TYPE_SINGLE_SHOT);
        if (APP_TIMER_OK != err)
        {
            _log("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        // Start timer instance
        err = app_timer_start(&_test_timers[i].timer, period_ms, &_test_timers[i]);
        if (APP_TIMER_OK != err)
        {
            _log("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        if (period_ms > highest_period_ms)
        {
            highest_period_ms = period_ms;
        }

        period_ms += SINGLE_PERIOD_INCREMENT_MS;
    }

    period_ms = REPEAT_PERIOD_START_MS;

    // Index of the middle point of all repeating timers
    uint32_t repeat_timers_half = NUM_SINGLE_TIMERS + ((NUM_TEST_TIMERS - NUM_SINGLE_TIMERS) / 2u);

    // Initialize all repeating timer instances
    for (uint32_t i = NUM_SINGLE_TIMERS; i < NUM_TEST_TIMERS; i++)
    {
        _test_timers[i].ms = period_ms;
        _test_timers[i].last_us = timing_usecs_elapsed();
        _test_timers[i].sum_diff_us = 0LL;
        _test_timers[i].highest_diff_us = 0LL;
        _test_timers[i].lowest_diff_us = 0LL;
        _test_timers[i].expirations = 0UL;
        _test_timers[i].first = true;

        /* Half of the repeating timers will have a callback that does not restart itself,
         * and the other half will have a callback that does restart itself. Restarting a repeating
         * timer in the handler should not break anything. */
        app_timer_handler_t handler = (i >= repeat_timers_half) ? _repeat_timer_callback : _single_timer_callback;

        err = app_timer_create(&_test_timers[i].timer, handler, APP_TIMER_TYPE_REPEATING);
        if (APP_TIMER_OK != err)
        {
            _log("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        // Start timer instance
        err = app_timer_start(&_test_timers[i].timer, period_ms, &_test_timers[i]);
        if (APP_TIMER_OK != err)
        {
            _log("app_timer_create failed, err: 0x%x", err);
            return err;
        }

        if (period_ms > highest_period_ms)
        {
            highest_period_ms = period_ms;
        }

        period_ms += REPEAT_PERIOD_INCREMENT_MS;
    }

    _log("initializing %u single-shot timers & %u repeating timers, with periods from %u-%ums\n",
         NUM_SINGLE_TIMERS, NUM_REPEAT_TIMERS, lowest_period_ms, highest_period_ms);
    char sizestrbuf[32];
    (void) sizesprint(sizeof(_test_timers), sizestrbuf, sizeof(sizestrbuf));
    _log("%s of memory used\n", sizestrbuf);
    _log("running %u timers for %u seconds...\n", NUM_TEST_TIMERS, TOTAL_TEST_TIME_SECONDS);

    uint64_t start_us = timing_usecs_elapsed();
    uint64_t total_test_time_us = TOTAL_TEST_TIME_SECONDS * 1000000ULL;

    uint64_t usecs_elapsed = 0u;
    uint64_t last_time_log_secs = 0u;

    uint64_t highest_poll_time_us = 0u;

    // Enter polling loop-- polling_app_timer_poll must be called regularly
    while (usecs_elapsed <= total_test_time_us)
    {
        uint64_t before_poll = timing_usecs_elapsed();
        polling_app_timer_poll();
        uint64_t poll_time = timing_usecs_elapsed() - before_poll;

        if (poll_time > highest_poll_time_us)
        {
            highest_poll_time_us = poll_time;
        }

        usecs_elapsed = timing_usecs_elapsed() - start_us;
        uint64_t secs_elapsed = usecs_elapsed / 1000000ULL;
        if ((secs_elapsed - last_time_log_secs) >= TIME_LOG_INTERVAL_SECS)
        {
            last_time_log_secs = secs_elapsed;
            uint64_t secs_remaining = TOTAL_TEST_TIME_SECONDS - secs_elapsed;
            _log("%"PRIu64" seconds remaining\n", secs_remaining);
        }
    }

    _log("test complete, stopping all timers...\n");

    // Stop all the timers
    for (uint32_t i = 0u; i < NUM_TEST_TIMERS; i++)
    {
        err = app_timer_stop(&_test_timers[i].timer);
        if (APP_TIMER_OK != err)
        {
            _log("app_timer_stop failed, err: 0x%x", err);
            return err;
        }
    }

    _log("all timers stopped, starting analysis...\n");

    test_results_summary_t results;

    _dump_test_results(&results);

    app_timer_stats_t stats;
    (void) app_timer_stats(&stats);

    _log("analysis done\n\n");
    printf("------------ Summary ------------\n\n");
    printf("starting microseconds timestamp (uint32): %u\n", start_usecs);
    printf("ending microseconds timestamp (uint32)  : %u\n\n", (uint32_t) timing_usecs_elapsed());
    printf("Ran %u timers at once (%u single-shot and %u repeating), all with different\n",
           NUM_TEST_TIMERS, NUM_SINGLE_TIMERS, NUM_REPEAT_TIMERS);
    printf("periods between %u-%u milliseconds, for %u seconds total.\n\n",
           lowest_period_ms, highest_period_ms, TOTAL_TEST_TIME_SECONDS);

    printf("Active timers high watermark:\n");
    printf("- %u\n\n", stats.num_timers_high_watermark);

    printf("Expiry overflows in app_timer_target_count_reached:\n");
    printf("- %u\n\n", stats.num_expiry_overflows);

    printf("No. of timers that differ from expected expiration counts by 1:\n");
    printf("- %"PRIu64"\n\n", results.expirations_plus1_count);

    printf("No. of timers that differ from expected expiration counts by more than 1:\n");
    printf("- %"PRIu64"\n\n", results.expirations_not_plus1_count);

    printf("Absolute highest deviation seen from expected expiration counts:\n");
    printf("- %"PRIu64"\n\n", results.highest_expiration_diff);

    printf("Highest app_timer_target_count_reached execution time:\n");
    printf("- %.4f milliseconds\n\n", ((float) (highest_poll_time_us)) / 1000.0f);

    int64_t total_expected_exp = results.total_expected_expirations;
    int64_t total_actual_exp = results.total_actual_expirations;
    int64_t exp_diff = total_actual_exp - total_expected_exp;
    const char *desc = (exp_diff < 0) ? "fewer" : "more";
    uint64_t abs_diff = (exp_diff < 0) ? (uint64_t) (exp_diff * -1) : (uint64_t) exp_diff;
    double diff_percent = ((double) abs_diff) / (((double) results.total_expected_expirations) / 100.0);

    printf("Diff. between expected and actual total expiration count:\n");
    printf("- %"PRIu64" total expirations occurred, out of expected %"PRIu64"\n",
           results.total_actual_expirations, results.total_expected_expirations);

    printf("- Saw %.3f%% %s expirations than expected\n\n",
           diff_percent, desc);

    printf("Diff. between expected and measured period (as a relative percentage of timer period):\n");
    printf("- Highest average seen for a single timer  : %.2f\n", results.highest_avg_percent);
    printf("- Lowest average seen for a single timer   : %.2f\n", results.lowest_avg_percent);
    printf("- Average across all timers                : %.2f\n", results.average_avg_percent);
    printf("- Absolute lowest seen across all timers   : %.2f\n", results.lowest_percent);
    printf("- Absolute highest seen across all timers  : %.2f\n\n", results.highest_percent);

    printf("Diff. between expected and measured period (in milliseconds):\n");
    printf("- Highest average seen for a single timer  : %.2f\n", results.highest_avg_ms);
    printf("- Lowest average seen for a single timer   : %.2f\n", results.lowest_avg_ms);
    printf("- Average across all timers                : %.2f\n", results.average_avg_ms);
    printf("- Absolute lowest seen across all timers   : %.2f\n", results.lowest_ms);
    printf("- Absolute highest seen across all timers  : %.2f\n\n", results.highest_ms);

    printf("Accumulated error of longest timer:\n");
    printf("- Longest timer period in milliseconds     : %u\n", results.longest_timer_period_ms);
    printf("- Longest timer expected expirations       : %u\n", results.longest_timer_expected_expirations);
    printf("- Longest timer expirations                : %u\n", results.longest_timer_expirations);
    printf("- Longest timer error in microseconds      : %u\n\n", results.longest_timer_error_us);
}

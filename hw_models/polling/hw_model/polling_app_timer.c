/**
 * @file polling_app_timer.c
 * @author Erik Nyquist
 *
 * @brief Implements an app_timer HW model for any C program running on a modern OS,
 *        using a polling approach
 */

#include <stdint.h>
#include <stdbool.h>
#include "polling_app_timer.h"


#ifdef __cplusplus
extern "C" {
#endif


#ifdef _WIN32
#include <Windows.h>
static uint64_t _perf_freq;
#endif // _WIN32


#define MAX_COUNT (0xffffffu)

static bool _running = false;
static app_timer_count_t _last_timer_counts = 0u;
static uint64_t _last_timer_usecs = 0u;


static uint64_t _get_usecs_elapsed(void)
{
#ifdef _WIN32
    LARGE_INTEGER tcounter = {0};
    uint64_t tick_value = 0u;
    if (QueryPerformanceCounter(&tcounter) != 0)
    {
        tick_value = tcounter.QuadPart;
    }

    return (uint64_t) (tick_value / (_perf_freq / 1000000));
#else
#error "Platform not supported"
#endif
}


static app_timer_running_count_t _ms_to_timer_counts(uint32_t ms)
{
    return ((app_timer_running_count_t) ms) * 1000ULL;
}


static app_timer_count_t _read_timer_counts(void)
{
    if (!_running)
    {
        return 0u;
    }

    return _get_usecs_elapsed() - _last_timer_usecs;
}


static void _set_timer_period_counts(app_timer_count_t counts)
{
    _last_timer_counts = counts;
}


static void _set_timer_running(bool enabled)
{
    if (_running == enabled)
    {
        // Nothing to do
        return;
    }

    _running = enabled;

    if (enabled)
    {
        _last_timer_usecs = _get_usecs_elapsed();
    }
}


static void _set_interrupts_enabled(bool enabled, app_timer_int_status_t *int_status)
{
    ; // Nothing needed here
}


// Initialize hardware model
static bool _init(void)
{
#ifdef _WIN32
    LARGE_INTEGER tcounter = {0};
    if (QueryPerformanceFrequency(&tcounter) != 0)
    {
        _perf_freq = tcounter.QuadPart;
    }
#endif // _WIN32
    return true;
}


// Hardware model definition
static app_timer_hw_model_t _polling_hw_model = {
    .init = _init,
    .ms_to_timer_counts = _ms_to_timer_counts,
    .read_timer_counts = _read_timer_counts,
    .set_timer_period_counts = _set_timer_period_counts,
    .set_timer_running = _set_timer_running,
    .set_interrupts_enabled = _set_interrupts_enabled,
    .max_count = MAX_COUNT
};


/**
 * @see polling_app_timer.h
 */
app_timer_error_e polling_app_timer_init(void)
{
    return app_timer_init(&_polling_hw_model);
}

/**
 * @see polling_app_timer.h
 */
void polling_app_timer_poll(void)
{
    app_timer_count_t now = _read_timer_counts();
    if (now >= _last_timer_counts)
    {
        app_timer_on_interrupt();
    }
}

#ifdef __cplusplus
}
#endif

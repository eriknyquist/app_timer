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
#include "timing.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * 60 minutes in microseconds-- leaves a good 11 minutes of slack to ensure we don't overflow
 * a uint32 before the polling loop makes it around
 */
#define MAX_COUNT (60u * 60 * 1000u * 1000u)


static bool _running = false;
static app_timer_count_t _last_timer_counts = 0u;
static app_timer_running_count_t _last_timer_usecs = 0u;


static app_timer_running_count_t _units_to_timer_counts(app_timer_period_t ms)
{
    return ((app_timer_running_count_t) ms) * 1000ULL;
}


static app_timer_count_t _read_timer_counts(void)
{
    if (!_running)
    {
        return 0u;
    }

    return (app_timer_count_t) (((app_timer_running_count_t) timing_usecs_elapsed()) - _last_timer_usecs);
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

    if (enabled)
    {
        _last_timer_usecs = (app_timer_running_count_t) timing_usecs_elapsed();
    }

    _running = enabled;
}


static void _set_interrupts_enabled(bool enabled, app_timer_int_status_t *int_status)
{
    ; // Nothing needed here
}


// Initialize hardware model
static bool _init(void)
{
    timing_init();
    return true;
}


// Hardware model definition
static app_timer_hw_model_t _polling_hw_model = {
    .init = _init,
    .units_to_timer_counts = _units_to_timer_counts,
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
        app_timer_target_count_reached();
    }
}

#ifdef __cplusplus
}
#endif

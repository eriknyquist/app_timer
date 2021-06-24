/**
 * @file app_timer.c
 * @author Erik Nyquist
 *
 * @brief Implements a friendly abstraction layer for arbitrary non-blocking delays,
 *        or "application timers", based on a single HW timer/counter peripheral that
 *        can generate an interrupt when a specific count value is reached. Can handle
 *        any number of application timers running simultaneously (active application
 *        timers are maintained as a linked list, so if a very large number of
 *        application timers are active at once, this may degrade performance since
 *        the linked list of active application timers must be traversed each time
 *        the HW timer/counter interrupt fires).
 *
 *        How to use this module;
 *        1. Implement a HW model (app_timer_hw_model_t) for the specific timer/counter
 *           hardware you wish to use for generating interrupts
 *
 *        2. Ensure "app_timer_on_interrupt" is called in the interrupt handler for the
 *           timer/counter hardware being used
 *
 *        3. Ensure the typedef for app_timer_count_t (at the top of app_timer_api.h)
 *           maps to an unsigned fixed-width integer type that matches the size of your
 *           timer/counter (the default is uint16_t for a 16-bit timer/counter)
 *
 *        4. Call app_timer_init() and pass in a pointer to the HW model you created
 *
 *        5. Now, app_timer_create and app_timer_start can be used to create as
 *           many timers as needed (see app_timer_api.h)
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_timer_api.h"

/* Keeps track of total elapsed timer counts, regardless of overflows, while there
 * are active timers */
static uint32_t _running_timer_count = 0u;

// Points to the timer that will expire soonest
static app_timer_t *_active_timers_head = NULL;

// Points to the timer that will expire last
static app_timer_t *_active_timers_tail = NULL;

// The last value that was passed to set_timer_period_counts
static app_timer_count_t _last_timer_period = 0u;

// HW timer/counter value after it was last started (some timer/counters do not start counting from 0)
static app_timer_count_t _counts_after_last_start = 0u;

// True when app_timer_on_interrupt is executing
static volatile bool _isr_running = false;

// True when app_timer_init has completed successfully
static bool _initialized = false;

// Pointer to the HW model in use
static app_timer_hw_model_t *_hw_model = NULL;


/**
 * Checks if a timer is currently active
 *
 * @return timer  Pointer to timer instance to check
 *
 * @return True if timer is active
 */
static bool _timer_active(app_timer_t *timer)
{
    if (timer == _active_timers_head)
    {
        return true;
    }

    return ((NULL != timer->next) || (NULL != timer->previous));
}


/**
 * Inserts a new timer into the doubly-linked list of active timers, ensuring that the order of the
 * list is maintained (the next timer to expire must always be the head of the list).
 *
 * @note The #start_counts and #total_counts fields of the timer must be set before calling
 *       this function
 *
 * @param timer Pointer to timer instance to insert
 */
static void _insert_timer(app_timer_t *timer)
{
    if (NULL == _active_timers_head)
    {
        // No other active timers
        _active_timers_head = timer;
        _active_timers_tail = timer;
        return;
    }

    // timer->start_counts is assumed to be set to the current timestamp
    uint32_t now = timer->start_counts;

    app_timer_t *curr = _active_timers_head;

    /* Pending timers are maintained as a doubly-linked list, in ascending order
     * of expiry time, such that the timer set to expire next is always the head of
     * the list. In order to maintain this, we just need to look for the first timer
     * (starting from the head) with an expiry time *later* than that of the new timer,
     * and insert the new timer before that one. */
    while (NULL != curr)
    {
        // Timestamp for when this timer will expire
        uint32_t expiry = curr->start_counts + curr->total_counts;

        // Timer ticks until this timer expires (0u if it should have already expired)
        uint32_t ticks_until_expiry = (expiry > now) ? expiry - now : 0u;

        if (ticks_until_expiry > timer->total_counts)
        {
            // First timer seen that expires later than new timer, break out
            break;
        }

        curr = curr->next;
    }

    if (NULL == curr)
    {
        /* Traversed the list without finding any timers that expire later than new timer,
         * so the new timer goes at the end and becomes the new tail of the list. */
        _active_timers_tail->next = timer;
        timer->previous = _active_timers_tail;
        timer->next = NULL;
        _active_timers_tail = timer;
    }
    else
    {
        // Found a timer that expires later than the new timer; insert new timer before it
        if (NULL != curr->previous)
        {
            curr->previous->next = timer;
        }

        timer->previous = curr->previous;
        timer->next = curr;
        curr->previous = timer;

        if (curr == _active_timers_head)
        {
            _active_timers_head = timer;
        }
    }
}


/**
 * Removes a timer from the doubly-linked list of active timers.
 *
 * @param timer  Pointer to timer instance to unlink
 */
static void _remove_timer(app_timer_t *timer)
{
    if (_active_timers_head == timer)
    {
        // Deleting head timer
        _active_timers_head = timer->next;
    }

    if (_active_timers_tail == timer)
    {
        _active_timers_tail = timer->previous;
    }

    if (NULL != timer->next)
    {
        timer->next->previous = timer->previous;
    }

    if (NULL != timer->previous)
    {
        timer->previous->next = timer->next;
    }

    timer->next = NULL;
    timer->previous = NULL;
}


/**
 * Helper function to configure the HW timer/counter and start it
 *
 * @param total_counts   Total timer/counter counts until expiry (this value may be larger
 *                       than _hw_model.max_count)
 */
static void _configure_timer(uint32_t total_counts)
{
    app_timer_count_t counts_from_now = (total_counts > ((uint32_t) _hw_model->max_count)) ?
                                       _hw_model->max_count :
                                       (app_timer_count_t) total_counts;

    _hw_model->set_timer_period_counts(counts_from_now);
    _last_timer_period = counts_from_now;
}


static uint32_t _total_timer_counts(void)
{
    app_timer_count_t ticks_elapsed = _hw_model->read_timer_counts() - _counts_after_last_start;
    return _running_timer_count + ((uint32_t) ticks_elapsed);
}

/**
 * @see app_timer_api.h
 */
void app_timer_on_interrupt(void)
{
    // Sanity check, this should never be NULL
    if (NULL == _active_timers_head)
    {
        return;
    }


    // Set flag indicating the ISR is running
    _isr_running = true;

    _running_timer_count += (uint32_t) _last_timer_period;
    uint32_t now = _running_timer_count;

    // Stop the timer counter, re-start it to time how long ISR takes
    _hw_model->set_timer_running(false);
    _configure_timer(_hw_model->max_count);
    _hw_model->set_timer_running(true);
    _counts_after_last_start = _hw_model->read_timer_counts();

    /* Run handlers for all expired timers; keep removing the timer at the head of the list,
     * and running its handler, until the head of list is an unexpired timer */
    app_timer_t *head = _active_timers_head;
    while ((NULL != head) && ((head->start_counts + head->total_counts) <= now))
    {
        // Unlink timer from list
        _remove_timer(head);

        if (NULL != head->handler)
        {
            head->handler(head->context);
        }

        // Update our notion of "now", handler may have taken significant time
        now = _total_timer_counts();

        if (APP_TIMER_TYPE_REPEATING == head->type)
        {
            // Timer is repeating, must be re-inserted with a new start time
            head->start_counts = now;
            _insert_timer(head);
        }

        head = _active_timers_head;
    }

    // Update running timer count with time taken to run expired handlers
    _running_timer_count += (_hw_model->read_timer_counts() - _counts_after_last_start);
    _hw_model->set_timer_running(false);

    if (NULL == _active_timers_head)
    {
        // No more active timers, don't re-start the counter
        _running_timer_count = 0u;
    }
    else
    {
        // Configure timer for the next expiration and re-start
        uint32_t expiry = _active_timers_head->start_counts + _active_timers_head->total_counts;
        uint32_t counts_until_expiry = expiry - now;
        _configure_timer(counts_until_expiry);
        _hw_model->set_timer_running(true);
        _counts_after_last_start = _hw_model->read_timer_counts();
    }

    _isr_running = false;
}


/**
 * @see timer_api.h
 */
app_timer_error_e app_timer_create(app_timer_t *timer, app_timer_handler_t handler, app_timer_type_e type)
{
    if (!_initialized)
    {
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == timer)
    {
        return APP_TIMER_INVALID_PARAM;
    }

    timer->type = type;
    timer->handler = handler;
    timer->start_counts = 0u;
    timer->total_counts = 0u;
    timer->next = NULL;
    timer->previous = NULL;

    return APP_TIMER_OK;
}


/**
 * @see timer_api.h
 */
app_timer_error_e app_timer_start(app_timer_t *timer, uint32_t ms_from_now, void *context)
{
    if (!_initialized)
    {
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == timer)
    {
        return APP_TIMER_INVALID_PARAM;
    }

    if (_timer_active(timer))
    {
        // Timer is already active
        return APP_TIMER_OK;
    }

    uint32_t total_counts = _hw_model->ms_to_timer_counts(ms_from_now);
    timer->context = context;
    timer->total_counts = total_counts;

    // Disable interrupts, don't want the timer ISR to interrupt this
    _hw_model->set_interrupts_enabled(false);

    // Were any timers running before this one?
    bool only_timer = (NULL == _active_timers_head);

    /* timer->start_counts must be set before calling _insert_timer; the expiry
     * time if the timer must be known in order to position the new timer correctly
     * within the list */
    if (only_timer && !_isr_running)
    {
        /* No other timers are running, and we're not being called from
         * app_timer_on_interrupt, so start_counts should be 0. */
        timer->start_counts = 0u;
    }
    else
    {
        /* Other timers are already running, or we are being called from
         * app_timer_on_interrupt. Calculate timestamp for start_counts based on
         * the current HW timer/counter value. */
        timer->start_counts = _total_timer_counts();
    }

    // Insert timer into list
    _insert_timer(timer);

    /* If this is the new head of the list, we need to re-configure the HW timer/counter */
    if ((timer == _active_timers_head) && !_isr_running)
    {
        if (!only_timer)
        {
            /* If we've replaced another timer as the head timer, then we need to
             * update _running_timer_count with the number of ticks that have elapsed
             * for the previous head timer. */
            _running_timer_count += (_hw_model->read_timer_counts() - _counts_after_last_start);
        }

        _hw_model->set_timer_running(false);
        _configure_timer(timer->total_counts);
        _hw_model->set_timer_running(true);
        _counts_after_last_start = _hw_model->read_timer_counts();
    }

    _hw_model->set_interrupts_enabled(true);

    return APP_TIMER_OK;
}


/**
 * @see timer_api.h
 */
app_timer_error_e app_timer_stop(app_timer_t *timer)
{
    if (!_initialized)
    {
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == timer)
    {
        return APP_TIMER_INVALID_PARAM;
    }

    _hw_model->set_interrupts_enabled(false);
    _remove_timer(timer);

    if (NULL == _active_timers_head)
    {
        // If this was the only active timer, stop the counter
        _hw_model->set_timer_running(false);
        _running_timer_count = 0u;
    }

    _hw_model->set_interrupts_enabled(true);
    return APP_TIMER_OK;
}


/**
 * @see timer_api.h
 */
app_timer_error_e app_timer_init(app_timer_hw_model_t *model)
{
    if (_initialized)
    {
        // Already initialized
        return APP_TIMER_OK;
    }

    if (NULL == model)
    {
        return APP_TIMER_INVALID_PARAM;
    }

    if ((0u == model->max_count) ||
        (NULL == model->init) ||
        (NULL == model->ms_to_timer_counts) ||
        (NULL == model->read_timer_counts) ||
        (NULL == model->set_timer_period_counts) ||
        (NULL == model->set_timer_running) ||
        (NULL == model->set_interrupts_enabled))
    {
        return APP_TIMER_INVALID_PARAM;
    }

    _hw_model = model;

    if (!_hw_model->init())
    {
        return APP_TIMER_ERROR;
    }

    _hw_model->set_timer_running(false);
    _hw_model->set_interrupts_enabled(true);

    _initialized = true;

    return APP_TIMER_OK;
}

#ifdef __cplusplus
}
#endif

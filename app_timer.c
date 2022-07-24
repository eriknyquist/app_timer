/**
 * @file app_timer.c
 *
 * @author Erik K. Nyquist
 *
 * @brief Implements "application timers", allowing an arbitrary number of timed
 *        events to be driven by a single timer/counter source.
 *
 *        How to use this module;
 *
 *        1. Implement a hardware model for your specific time source, or use one of the samples
 *           in "example_hw_models" if there is an appropriate one. In this case, we'll use the
 *           arduino UNO hardware model, "arduino_app_timer.c" and "arduino_app_timer.h",
 *           for discussion's sake.
 *
 *        2. In your application code, ensure that "app_timer_init" is being called, and that
 *           a pointer to the app_timer_hw_model_t struct for your hardware model is passed in.
 *           The arduino hardware model provides a "arduino_app_timer_init" function which
 *           does exactly this.
 *
 *        3. Ensure that "app_timer.c" and "arduino_app_timer.c" (or whatever hardware model
 *           you are using, if not arduino) are compiled and linked in along with the rest of your
 *           application.
 *
 *        4. That's it. Now that "app_timer" has been initialized with a hardware model,
 *           you can use the functions from "app_timer_api.h" in your application code to
 *           create and run "app_timer_t" instances.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_timer_api.h"


/**
 * Bit mask and bit position for timer state
 */
#define FLAGS_STATE_MASK (0x3u)
#define FLAGS_STATE_POS  (0x0u)


/**
 * Bit mask and bit position for timer type
 */
#define FLAGS_TYPE_MASK (0xCu)
#define FLAGS_TYPE_POS  (0x2u)


#ifdef APP_TIMER_STATS_ENABLE
static app_timer_stats_t _stats =
{
    .num_timers=0u,
    .num_timers_high_watermark=0u,
    .num_expiry_overflows=0u,
    .next_active_timer=NULL,
    .running_timer_count=0u,
    .inside_target_count_reached=false
};
#endif // APP_TIMER_STATS_ENABLE


/**
 * Represents a doubly-linked list of app_timer_t instances
 */
typedef struct
{
    app_timer_t *head;
    app_timer_t *tail;
} _timer_list_t;


/**
 * Represents all possible states that an app_timer_t instance can be in
 */
typedef enum
{
    TIMER_STATE_STOPPED = 0,  ///< Timer has not yet been started, or was stopped by app_timer_stop
    TIMER_STATE_EXPIRED,      ///< Timer was started and has since expired
    TIMER_STATE_ACTIVE,       ///< Timer has been started and not yet expired
} _timer_state_e;


/**
 * Keeps track of total elapsed timer counts, regardless of overflows, while there
 * are active timers
 */
static volatile app_timer_running_count_t _running_timer_count = 0u;

/**
 * The list of active timers stores all timer instances that have been started with
 * 'app_timer_start' but not yet expired.
 */
static volatile _timer_list_t _active_timers = { .head=NULL, .tail=NULL };

/**
 * The last value that was passed to set_timer_period_counts
 */
static volatile app_timer_count_t _last_timer_period = 0u;

/**
 * Hardware timer/counter value after it was last started (some timer/counters do not start counting from 0)
 */
static volatile app_timer_count_t _counts_after_last_start = 0u;

/**
 * True when app_timer_target_count_reached is executing
 */
static volatile bool _inside_target_count_reached = false;

/**
 * True when app_timer_init has completed successfully
 */
static bool _initialized = false;

/**
 * Pointer to the hardware model in use
 */
static app_timer_hw_model_t *_hw_model = NULL;


/**
 * Calculate number of ticks until an active timer expires. Should only be used on
 * timers that are known to be active (i.e. timers linked into the active timers list).
 *
 * @param now    Current timestamp in ticks
 * @param timer  Pointer to timer instance
 *
 * @return Ticks until timer should expire (will be 0 if timer should have already expired)
 */
static inline app_timer_running_count_t _ticks_until_expiry(app_timer_running_count_t now, app_timer_t *timer)
{
    app_timer_running_count_t expiry = timer->start_counts + timer->total_counts;

    if (expiry < now)
    {
        // Expiry was in the past
        return 0u;
    }
    else
    {
        return expiry - now;
    }
}


/**
 * Inserts a new timer into the doubly-linked list of active timers, ensuring that the order of the
 * list is maintained (the next timer to expire must always be the head of the list).
 *
 * @note The #start_counts and #total_counts fields of the timer must be set before calling
 *       this function. #start_counts should be set to the timestamp in counts when the timer
 *       was added via #app_timer_start, and #total_counts should be set to the timer period
 *       in counts.
 *
 * @param timer Pointer to timer instance to insert
 * @param now   Current timestamp in timer counts
 */
static void _insert_active_timer(app_timer_t *timer, app_timer_running_count_t now)
{
    // Set timer state to active
    timer->flags &= ~FLAGS_STATE_MASK;
    timer->flags |= (TIMER_STATE_ACTIVE << FLAGS_STATE_POS);

#ifdef APP_TIMER_STATS_ENABLE
    _stats.num_timers += 1u;

    if (_stats.num_timers_high_watermark < _stats.num_timers)
    {
        _stats.num_timers_high_watermark = _stats.num_timers;
    }
#endif // APP_TIMER_STATS_ENABLE

    if (NULL == _active_timers.head)
    {
        // No other active timers
        _active_timers.head = timer;
        _active_timers.tail = timer;
        return;
    }

    app_timer_t *curr = _active_timers.head;

    /* Pending timers are maintained as a doubly-linked list, in ascending order
     * of expiry time, such that the timer set to expire next is always the head of
     * the list. In order to maintain this, we just need to walk the list and look
     * for the first timer with an expiry time *later* than that of the new timer,
     * and insert the new timer before that one. */
    while (NULL != curr)
    {
        // Timer ticks until this timer expires (0u if it should have already expired)
        if (_ticks_until_expiry(now, curr) > timer->total_counts)
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
        timer->previous = _active_timers.tail;

        if (NULL != _active_timers.tail)
        {
            _active_timers.tail->next = timer;
        }

        _active_timers.tail = timer;
        timer->next = NULL;
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

        if (curr == _active_timers.head)
        {
            _active_timers.head = timer;
        }
    }
}


/**
 * Removes a timer from a doubly-linked list of timers.
 *
 * @param list   Pointer to list containing timer to be removed
 * @param timer  Pointer to timer instance to unlink
 */
static void _remove_timer_from_list(volatile _timer_list_t *list, app_timer_t *timer)
{
    if (list->head == timer)
    {
        // Removing head timer
        list->head = timer->next;
    }

    if (list->tail == timer)
    {
        // Removing tail timer
        list->tail = timer->previous;
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
 * Helper function to configure the hardware timer/counter to expire after a certain
 * number of counts.
 *
 * @param total_counts   Total timer/counter counts until expiry (if this value is larger
 *                       than _hw_model.max_count, then the timer/counter will be configured
 *                       for _hw_model.max_count instead)
 */
static void _configure_timer(app_timer_running_count_t total_counts)
{
    app_timer_count_t counts_from_now = (total_counts > ((app_timer_running_count_t) _hw_model->max_count)) ?
                                       _hw_model->max_count :
                                       (app_timer_count_t) total_counts;

    _hw_model->set_timer_period_counts(counts_from_now);
    _last_timer_period = counts_from_now;
}


/**
 * Returns the total number of ticks elapsed since the first of the currently active
 * app_timer instances were started (Should return 0 when no app_timer instances are running)
 */
static inline app_timer_running_count_t _total_timer_counts(void)
{
    app_timer_count_t ticks_elapsed = _hw_model->read_timer_counts() - _counts_after_last_start;
    return _running_timer_count + ((app_timer_running_count_t) ticks_elapsed);
}


/**
 * @see app_timer_api.h
 */
void app_timer_target_count_reached(void)
{
    /* Set flag indicating we are in the function handling an elapsed count, so
     * that any calls to app_timer_start inside handlers will know not to configure
     * the timer, since we will do that at the end of this function. This does not
     * need to be protected from interrupts. */
    _inside_target_count_reached = true;

    // Disable interrupts to update _running_timer_count and pop expired timers off the list
    app_timer_int_status_t int_status = 0u;
    _hw_model->set_interrupts_enabled(false, &int_status);

    // The tick on which the head active timer should have expired
    app_timer_running_count_t expiry_count = _running_timer_count + _last_timer_period;

    // Update _running_timer_count with ticks elapsed since last update
#ifdef APP_TIMER_FREERUNNING_COUNTER
    _running_timer_count += (_hw_model->read_timer_counts() - _counts_after_last_start);
#else
    _running_timer_count += (app_timer_running_count_t) _last_timer_period;
#endif // APP_TIMER_FREERUNNING_COUNTER

    // Stop the timer counter, re-start it to time how long it takes to handle all expired timers
#ifndef APP_TIMER_RECONFIG_WITHOUT_STOPPING
    _hw_model->set_timer_running(false);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING
    _configure_timer(_hw_model->max_count);
#ifndef APP_TIMER_RECONFIG_WITHOUT_STOPPING
    _hw_model->set_timer_running(true);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING
    _counts_after_last_start = _hw_model->read_timer_counts();

    // Remove all expired timers from the active list, and run their handlers
    while ((NULL != _active_timers.head) && (_ticks_until_expiry(expiry_count, _active_timers.head) == 0u))
    {
        app_timer_t *curr = _active_timers.head;

        // Unlink timer from active list
        _remove_timer_from_list(&_active_timers, curr);

#ifdef APP_TIMER_STATS_ENABLE
        _stats.num_timers -= 1u;
#endif // APP_TIMER_STATS_ENABLE

        // Clear state bits, set timer state to expired
        curr->flags &= ~FLAGS_STATE_MASK;
        curr->flags |= (TIMER_STATE_EXPIRED << FLAGS_STATE_POS);

        // Run the handler
        if (NULL != curr->handler)
        {
#ifdef APP_TIMER_ENABLE_INTERRUPTS_FOR_HANDLER
            _hw_model->set_interrupts_enabled(true, &int_status);
#endif // APP_TIMER_ENABLE_INTERRUPTS_FOR_HANDLER

            curr->handler(curr->context);

#ifdef APP_TIMER_ENABLE_INTERRUPTS_FOR_HANDLER
            _hw_model->set_interrupts_enabled(false, &int_status);
#endif // APP_TIMER_ENABLE_INTERRUPTS_FOR_HANDLER

        }

        // Extract timer type from flags var
        app_timer_type_e type = (app_timer_type_e) ((curr->flags & FLAGS_TYPE_MASK) >> FLAGS_TYPE_POS);

        // Extract timer state from flags var
        _timer_state_e state = (_timer_state_e) ((curr->flags & FLAGS_STATE_MASK) >> FLAGS_STATE_POS);

        if ((APP_TIMER_TYPE_REPEATING == type) && (TIMER_STATE_EXPIRED == state))
        {
            /* Timer is repeating, and was not-restarted or stopped by the handler,
             * so must be re-inserted with a new start time */
            curr->start_counts = expiry_count;
            _insert_active_timer(curr, _total_timer_counts());
        }
    }

    if (NULL == _active_timers.head)
    {
        // No more active timers, stop the counter
        _running_timer_count = 0u;
        _hw_model->set_timer_running(false);
    }
    else
    {
        // Update running timer count with time taken to run expired handlers
        _running_timer_count += (_hw_model->read_timer_counts() - _counts_after_last_start);

        // Configure timer for the next expiration and re-start
        app_timer_running_count_t ticks_until_expiry = _ticks_until_expiry(_running_timer_count, _active_timers.head);

        /* If the head timer should have already expired (it expired while we were handling
         * other expired timers in the loop above), just configure the hardware for 1 tick,
         * and the head timer will be handled in the next call (although it does have the downside
         * that the head timer will expire at least 1 tick late) */
        bool expiry_overflow = (ticks_until_expiry == 0u);

#ifndef APP_TIMER_RECONFIG_WITHOUT_STOPPING
    _hw_model->set_timer_running(false);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING

        _configure_timer(expiry_overflow ? 1u : ticks_until_expiry);
#ifdef APP_TIMER_STATS_ENABLE
        _stats.num_expiry_overflows += (uint32_t) expiry_overflow;
#endif // APP_TIMER_STATS_ENABLE

#ifndef APP_TIMER_RECONFIG_WITHOUT_STOPPING
        _hw_model->set_timer_running(true);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING
        _counts_after_last_start = _hw_model->read_timer_counts();
    }

    _hw_model->set_interrupts_enabled(true, &int_status);

    _inside_target_count_reached = false;
}


/**
 * @see app_timer_api.h
 */
app_timer_error_e app_timer_create(app_timer_t *timer, app_timer_handler_t handler, app_timer_type_e type)
{
    if (!_initialized)
    {
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == timer)
    {
        return APP_TIMER_NULL_PARAM;
    }

    if (APP_TIMER_TYPE_COUNT <= type)
    {
        return APP_TIMER_INVALID_PARAM;
    }

    timer->handler = handler;
    timer->start_counts = 0u;
    timer->total_counts = 0u;
    timer->next = NULL;
    timer->previous = NULL;

    /* Set timer type. Other flags should be 0 by default */
    timer->flags = ((((uint8_t) type) << FLAGS_TYPE_POS) & FLAGS_TYPE_MASK);

    return APP_TIMER_OK;
}


/**
 * @see app_timer_api.h
 */
app_timer_error_e app_timer_start(app_timer_t *timer, app_timer_period_t time_from_now, void *context)
{
    if (!_initialized)
    {
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == timer)
    {
        return APP_TIMER_NULL_PARAM;
    }

    if (0u == time_from_now)
    {
        return APP_TIMER_INVALID_PARAM;
    }

    // Read timer state
    _timer_state_e state = (_timer_state_e) ((timer->flags & FLAGS_STATE_MASK) >> FLAGS_STATE_POS);

    if (TIMER_STATE_ACTIVE == state)
    {
        // Timer is already active
        return APP_TIMER_OK;
    }

    app_timer_running_count_t total_counts = _hw_model->units_to_timer_counts(time_from_now);

    /* Disable interrupts, don't want another app_timer function being called from ISR
     * context to interrupt modification of the list of active timers */
    app_timer_int_status_t int_status = 0u;
    _hw_model->set_interrupts_enabled(false, &int_status);

    timer->context = context;
    timer->total_counts = total_counts;

    // Were any timers running before this one?
    bool only_timer = (NULL == _active_timers.head);

    /* timer->start_counts must be set before calling _insert_active_timer; the expiry
     * time of the timer must be known in order to position the new timer correctly
     * within the list */
    if (only_timer && !_inside_target_count_reached)
    {
        /* No other timers are running, and we're not being called from
         * app_timer_target_count_reached, so start_counts should be 0. */
        timer->start_counts = 0u;
    }
    else
    {
        /* Other timers are already running, or we are being called from
         * app_timer_target_count_reached. Calculate timestamp for start_counts based on
         * the current hardware timer/counter value. */
        timer->start_counts = _total_timer_counts();
    }

    // Insert timer into list
    _insert_active_timer(timer, timer->start_counts);

    /* If this is the new head of the list, we need to re-configure the hardware timer/counter */
    if ((timer == _active_timers.head) && !_inside_target_count_reached)
    {
        if (!only_timer)
        {
            /* If we've replaced another timer as the head timer, then we need to
             * update _running_timer_count with the number of ticks that have elapsed
             * for the previous head timer. */
            _running_timer_count += (_hw_model->read_timer_counts() - _counts_after_last_start);
        }

#ifndef APP_TIMER_RECONFIG_WITHOUT_STOPPING
        // We should stop the counter before re-configuring it
        _hw_model->set_timer_running(false);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING
        _configure_timer(timer->total_counts);
#ifdef APP_TIMER_RECONFIG_WITHOUT_STOPPING
        /* Since we're not stopping/restarting the counter with each timer period,
         * we may need to start the counter if this is the only active timer */
        if (only_timer)
        {
            _hw_model->set_timer_running(true);
        }
#else
        _hw_model->set_timer_running(true);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING
        _counts_after_last_start = _hw_model->read_timer_counts();
    }

    _hw_model->set_interrupts_enabled(true, &int_status);

    return APP_TIMER_OK;
}


/**
 * @see app_timer_api.h
 */
app_timer_error_e app_timer_stop(app_timer_t *timer)
{
    if (!_initialized)
    {
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == timer)
    {
        return APP_TIMER_NULL_PARAM;
    }

    /* Disable interrupts, don't want another app_timer function being called from ISR
     * context to interrupt modification of the list of active timers */
    app_timer_int_status_t int_status = 0u;
    _hw_model->set_interrupts_enabled(false, &int_status);

    // Read timer state
    _timer_state_e state = (_timer_state_e) ((timer->flags & FLAGS_STATE_MASK) >> FLAGS_STATE_POS);

    if ((TIMER_STATE_ACTIVE == state) || (TIMER_STATE_EXPIRED == state))
    {
        // Remove from active timers list
        bool head_removed  = (_active_timers.head == timer);
        _remove_timer_from_list(&_active_timers, timer);

#ifdef APP_TIMER_STATS_ENABLE
        _stats.num_timers -= 1u;
#endif // APP_TIMER_STATS_ENABLE

        // Clear state bits to set timer state to stopped
        timer->flags &= ~FLAGS_STATE_MASK;

        // Don't want to touch the hardware if called from app_timer_target_count_reached
        if (!_inside_target_count_reached)
        {
            if (NULL == _active_timers.head)
            {
                // If this was the only active timer, stop the counter
                _hw_model->set_timer_running(false);
                _running_timer_count = 0u;
            }
            else if (head_removed)
            {
                /* Head timer removed, and there are more active timers. Need to update
                 * _running_timer_count and re-configure counter (unless we're being called
                 * from inside app_timer_target_count_reached, which will re-config the counter
                 * as needed when it finishes). */
                _running_timer_count += (_hw_model->read_timer_counts() - _counts_after_last_start);
#ifndef APP_TIMER_RECONFIG_WITHOUT_STOPPING
                _hw_model->set_timer_running(false);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING
                _configure_timer(_ticks_until_expiry(_running_timer_count, _active_timers.head));
#ifndef APP_TIMER_RECONFIG_WITHOUT_STOPPING
                _hw_model->set_timer_running(true);
#endif // APP_TIMER_RECONFIG_WITHOUT_STOPPING
                _counts_after_last_start = _hw_model->read_timer_counts();
            }
            else
            {
                ; // Nothing to do if removed timer wasn't the head, or if inside target_count_reached
            }
        }
    }

    _hw_model->set_interrupts_enabled(true, &int_status);
    return APP_TIMER_OK;
}


/**
 * @see app_timer_api.h
 */
app_timer_error_e app_timer_is_active(app_timer_t *timer, bool *is_active)
{
    if (!_initialized)
    {
        // Not initialized
        return APP_TIMER_INVALID_STATE;
    }

    if ((NULL == timer) || (NULL == is_active))
    {
        return APP_TIMER_NULL_PARAM;
    }

    // Read timer state
    _timer_state_e state = (_timer_state_e) ((timer->flags & FLAGS_STATE_MASK) >> FLAGS_STATE_POS);

    // Report true if timer is in active state
    *is_active = (TIMER_STATE_ACTIVE == state);

    return APP_TIMER_OK;
}


#ifdef APP_TIMER_STATS_ENABLE
/**
 * @see app_timer_api.h
 */
app_timer_error_e app_timer_stats(app_timer_stats_t *stats)
{
    if (!_initialized)
    {
        // Not initialized
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == stats)
    {
        return APP_TIMER_NULL_PARAM;
    }

    _stats.running_timer_count = _running_timer_count;
    _stats.inside_target_count_reached = _inside_target_count_reached;
    _stats.next_active_timer = _active_timers.head;

    *stats = _stats;

    return APP_TIMER_OK;
}
#endif // APP_TIMER_STATS_ENABLE


/**
 * @see app_timer_api.h
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
        return APP_TIMER_NULL_PARAM;
    }

    if ((0u == model->max_count) ||
        (NULL == model->init) ||
        (NULL == model->units_to_timer_counts) ||
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

    // Enable interrupt(s) initially
    app_timer_int_status_t int_status = 0u;
    _hw_model->set_interrupts_enabled(true, &int_status);

    _initialized = true;

    return APP_TIMER_OK;
}

#ifdef __cplusplus
}
#endif

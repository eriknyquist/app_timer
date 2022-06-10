/**
 * @file app_timer.c
 * @author Erik Nyquist
 *
 * @brief Implements "application timers", allowing an arbitrary number of timed
 *        events to be driven by a single timer/counter source.
 *
 *        How to use this module;
 *
 *        1. Implement a hardware model (app_timer_hw_model_t) for the specific timer/counter
 *           hardware you wish to use for generating interrupts.
 *
 *        2. Ensure "app_timer_target_count_reached" is called in the interrupt handler for the
 *           timer/counter hardware being used.
 *
 *        3. Ensure that either APP_TIMER_COUNT_UINT16 or APP_TIMER_COUNT_UINT32 is set --
 *           pick one that is large enough to hold all the bits of your hardware counter.
 *           For example, if you had a 24-bit counter, you could use APP_TIMER_COUNT_UINT32,
 *           but not APP_TIMER_COUNT_UINT16. If you don't define one of these options, the
 *           default is APP_TIMER_COUNT_UINT32.
 *
 *        4. Call app_timer_init() and pass in a pointer to the hardware model you created.
 *
 *        5. Now, app_timer_create and app_timer_start can be used to create as
 *           many application timers as needed (see app_timer_api.h).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_timer_api.h"


/**
 * Bit mask and bit position for active flag
 */
#define FLAGS_ACTIVE_BIT (0x1u)


/**
 * Bit mask and bit position for timer type
 */
#define FLAGS_TYPE_MASK (0x6u)
#define FLAGS_TYPE_POS  (0x1u)


/**
 * Keeps track of total elapsed timer counts, regardless of overflows, while there
 * are active timers
 */
static volatile app_timer_running_count_t _running_timer_count = 0u;


/**
 * Head of the list of active timers, points to the timer that will expire soonest.
 * The list of active timers stores all timer instances that have been started with
 * 'app_timer_start' but not yet expired.
 *
 * We never traverse this list in reverse, and whenever we add to it we need to look
 * at each item starting from the head (to maintain active timers in order of expiry
 * time), so there is no need to track the tail item of this list.
 */
static app_timer_t *volatile _active_timers_head = NULL;


/**
 * Head and tail of the list of expired timers. This list holds all timers that
 * have expired and not yet had their handlers run. The reason we have a separate list
 * for expired timers, instead of just running handlers for expired timers right away
 * as we pull them off the active timers list, is so that we can disable interrupts while
 * removing expired timers from the active timers list, but re-enable interrupts while running
 * the handlers for all expired timers.
 *
 * In other words, any accesses to the list of active timers need to be protected in a
 * critical section, but the handlers for all those expired timers should not be executed
 * until the critical section is exited.
 *
 * We only add items to the tail of this list, and we only remove them from the head,
 * so we need to track *both * the head and tail items of this list.
 */
static app_timer_t *volatile _expired_timers_head = NULL;
static app_timer_t *volatile _expired_timers_tail = NULL;


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
static volatile bool _isr_running = false;

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
 *       this function
 *
 * @param timer Pointer to timer instance to insert
 */
static void _insert_active_timer(app_timer_t *timer)
{
    // Set active flag
    timer->flags |= FLAGS_ACTIVE_BIT;

    if (NULL == _active_timers_head)
    {
        // No other active timers
        _active_timers_head = timer;
        return;
    }

    // timer->start_counts is assumed to be set to the current timestamp
    app_timer_running_count_t now = timer->start_counts;

    app_timer_t *curr = _active_timers_head;
    app_timer_t *prev = NULL;

    /* Pending timers are maintained as a doubly-linked list, in ascending order
     * of expiry time, such that the timer set to expire next is always the head of
     * the list. In order to maintain this, we just need to walk the list and look
     * for the first timer with an expiry time *later* than that of the new timer,
     * and insert the new timer before that one. */
    while (NULL != curr)
    {
        // Timer ticks until this timer expires (0u if it should have already expired)
        app_timer_running_count_t ticks_until_expiry = _ticks_until_expiry(now, curr);

        if (ticks_until_expiry > timer->total_counts)
        {
            // First timer seen that expires later than new timer, break out
            break;
        }

        prev = curr;
        curr = curr->next;
    }

    if (NULL == curr)
    {
        /* Traversed the list without finding any timers that expire later than new timer,
         * so the new timer goes at the end and becomes the new tail of the list. */
        prev->next = timer; // 'prev' will not be NULL if we reach here
        timer->previous = prev;
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

        if (curr == _active_timers_head)
        {
            _active_timers_head = timer;
        }
    }
}


/**
 * Removes a timer from the doubly-linked list of active timers.
 * The timer being removed may be at any position in the list (which is the
 * only reason for the list being doubly linked instead of singly linked).
 *
 * @param timer  Pointer to timer instance to unlink
 */
static void _remove_active_timer(app_timer_t *timer)
{
    if (_active_timers_head == timer)
    {
        // Deleting head timer
        _active_timers_head = timer->next;
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
    timer->flags &= ~FLAGS_ACTIVE_BIT;
}


/**
 * Helper function to configure the hardware timer/counter and start it
 *
 * @param total_counts   Total timer/counter counts until expiry (this value may be larger
 *                       than _hw_model.max_count)
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
 * Walks the list of active timers, and moves each expired timer to the
 * 'expired timers' list, until the head of the list of active timers is a timer that has
 * yet to expire.
 *
 * This function does not need to return anything; the caller will know if any timers
 * have expired by checking whether _expired_timers_head is not NULL.
 *
 * @param now  Current running time in ticks
 */
static void _remove_expired_timers(app_timer_running_count_t now)
{
    app_timer_t *head = _active_timers_head;
    while ((NULL != head) && (_ticks_until_expiry(now, head) == 0u))
    {
        // Unlink timer from active list
        _remove_active_timer(head);

        // Add timer to the expired list
        if (NULL == _expired_timers_head)
        {
            _expired_timers_head = head;
        }
        else
        {
            _expired_timers_tail->next = head;
        }

        _expired_timers_tail = head;
        head = head->next;
    }
}


/**
 * Traverse the expired timers list, run the handler for each timer, and remove
 * the timer from the list
 *
 * @param now  Current running time in ticks
 */
static void _handle_expired_timers(app_timer_running_count_t now)
{
    while (NULL != _expired_timers_head)
    {
        // Remove handler from the list, update head
        app_timer_t *curr = _expired_timers_head;
        _expired_timers_head = _expired_timers_head->next;
        curr->next = NULL;

        // Run the handler
        if (NULL != curr->handler)
        {
            curr->handler(curr->context);
        }

        // Extract timer type from flags var
        app_timer_type_e type = (app_timer_type_e) ((curr->flags & FLAGS_TYPE_MASK) >> FLAGS_TYPE_POS);

        if ((APP_TIMER_TYPE_REPEATING == type) && !(curr->flags & FLAGS_ACTIVE_BIT))
        {
            /* Timer is repeating, and was not-restarted by the handler,
             * so must be re-inserted with a new start time */
            curr->start_counts = now;
            _insert_active_timer(curr);
        }
    }

    // Traversed the entire list of expired timers, NULL-ify the tail item
    _expired_timers_tail = NULL;
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
    _isr_running = true;

    // Disable interrupts to update _running_timer_count and pop expired timers off the list
    app_timer_int_status_t int_status = 0u;
    _hw_model->set_interrupts_enabled(false, &int_status);

    _running_timer_count += (app_timer_running_count_t) _last_timer_period;
    app_timer_running_count_t now = _running_timer_count;

    // Stop the timer counter, re-start it to time how long it takes to handle all expired timers
    _hw_model->set_timer_running(false);
    _configure_timer(_hw_model->max_count);
    _hw_model->set_timer_running(true);
    _counts_after_last_start = _hw_model->read_timer_counts();

    // Remove all expired timers from the active list
    _remove_expired_timers(now);

    // Re-enable interrupts to run handlers for expired timers
    _hw_model->set_interrupts_enabled(true, &int_status);

    // Run callbacks for all expired timers
    _handle_expired_timers(now);

    // Disable interrupts to modify _running_timer_count and inspect active timers list
    _hw_model->set_interrupts_enabled(false, &int_status);

    // Update running timer count with time taken to run expired handlers
    _running_timer_count += (_hw_model->read_timer_counts() - _counts_after_last_start);
    now = _running_timer_count;
    _hw_model->set_timer_running(false);

    if (NULL == _active_timers_head)
    {
        // No more active timers, don't re-start the counter
        _running_timer_count = 0u;
    }
    else
    {
        // Configure timer for the next expiration and re-start
        app_timer_running_count_t ticks_until_expiry = _ticks_until_expiry(now, _active_timers_head);

        /* If the head timer should have already expired, that means that it had not
         * expired when _remove_expired_timers initially walked the list of active
         * timers, but then it DID expire while we were executing timer callbacks
         * in _handle_expired_timers. If that is the case, just configure the hardware
         * for 1 tick, and the head timer will be handled in the next call (although
         * it does have the downside that the head timer will expire at least 1 tick late) */
        _configure_timer((ticks_until_expiry == 0u) ? 1u : ticks_until_expiry);

        _hw_model->set_timer_running(true);
        _counts_after_last_start = _hw_model->read_timer_counts();
    }

    _hw_model->set_interrupts_enabled(true, &int_status);

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

    timer->handler = handler;
    timer->start_counts = 0u;
    timer->total_counts = 0u;
    timer->next = NULL;
    timer->previous = NULL;
    timer->flags = ((type << FLAGS_TYPE_POS) & FLAGS_TYPE_MASK);

    return APP_TIMER_OK;
}


/**
 * @see timer_api.h
 */
app_timer_error_e app_timer_start(app_timer_t *timer, app_timer_period_t time_from_now, void *context)
{
    if (!_initialized)
    {
        return APP_TIMER_INVALID_STATE;
    }

    if (NULL == timer)
    {
        return APP_TIMER_INVALID_PARAM;
    }

    if (timer->flags & FLAGS_ACTIVE_BIT)
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
    bool only_timer = (NULL == _active_timers_head);

    /* timer->start_counts must be set before calling _insert_active_timer; the expiry
     * time of the timer must be known in order to position the new timer correctly
     * within the list */
    if (only_timer && !_isr_running)
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
    _insert_active_timer(timer);

    /* If this is the new head of the list, we need to re-configure the hardware timer/counter */
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

    _hw_model->set_interrupts_enabled(true, &int_status);

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

    /* Disable interrupts, don't want another app_timer function being called from ISR
     * context to interrupt modification of the list of active timers */
    app_timer_int_status_t int_status = 0u;
    _hw_model->set_interrupts_enabled(false, &int_status);

    _remove_active_timer(timer);

    if (NULL == _active_timers_head)
    {
        // If this was the only active timer, stop the counter
        _hw_model->set_timer_running(false);
        _running_timer_count = 0u;
    }

    _hw_model->set_interrupts_enabled(true, &int_status);
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

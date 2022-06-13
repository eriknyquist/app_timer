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

#ifndef APP_TIMER_H
#define APP_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Defines the datatype used to represent the period for a timer (e.g. the
 * 'time_from_now' parameter passed to app_timer_start)
 */
 #if !defined(APP_TIMER_PERIOD_UINT32) && !defined(APP_TIMER_PERIOD_UINT64)
 #define APP_TIMER_PERIOD_UINT32
 #endif

/**
 * Defines the datatype used to represent a count value for the underlying hardware counter.
 */
#if !defined(APP_TIMER_COUNT_UINT16) && !defined(APP_TIMER_COUNT_UINT32)
#define APP_TIMER_COUNT_UINT32  // Store hardware counter value in 32 bits by default
#endif


/**
 * Defines the datatype used to represent a running counter that spans across multiple hardware counter overflows
 */
#if !defined(APP_TIMER_RUNNING_COUNT_UINT32) && !defined(APP_TIMER_RUNNING_COUNT_UINT64)
#define APP_TIMER_RUNNING_COUNT_UINT32  // Store running counter value in 32 bits by default
#endif


/**
 * Defines the datatype used to represent the interrupt status passed to 'set_interrupts_enabled'
 */
#if !defined(APP_TIMER_INT_UINT32) && !defined(APP_TIMER_INT_UINT64)
#define APP_TIMER_INT_UINT32  // Store interrupt status value in 32 bits by default
#endif

/**
 * Datatype used to represent the period for a timer (e.g. the 'time_from_now' parameter
 * passed to app_timer_start).
 *
 * For example, if you are using a hardware model that expects milliseconds for timer periods,
 * and uint32_t is used for timer periods, then the max. timer period you can pass to app_timer_start
 * is 2^32 milliseconds, or about 49 days
 */
#if defined(APP_TIMER_PERIOD_UINT32)
typedef uint32_t app_timer_period_t;
#elif defined(APP_TIMER_PERIOD_UINT64)
typedef uint64_t app_timer_period_t;
#else
#error "Timer period width is not defined"
#endif // APP_TIMER_PERIOD_*


/**
 * Datatype used to represent a count value for the underlying hardware counter.

 * This should be set to an unsigned fixed-width integer type that is large enough
 * to hold the number of bits the counter has. For example, if using a 24-bit counter,
 * uint32_t would be sufficient, but not uint16_t.
 */
#if defined(APP_TIMER_COUNT_UINT16)
typedef uint16_t app_timer_count_t;
#elif defined(APP_TIMER_COUNT_UINT32)
typedef uint32_t app_timer_count_t;
#else
#error "Hardware counter width is not defined"
#endif // APP_TIMER_COUNTER_*


/**
 * Datatype used to represent a running counter that tracks total elapsed time
 * since one or more active timers have been running continuously.
 *
 * You should pick this according to the expected lifetime of your system. Let's
 *  say, for example, that you are using a counter driven by a 32KHz clock; this
 * would mean using uint32_t for the running counter allows the app_timer module
 * to have timers running continuously for up to 2^32(-1) ticks, before the running
 * counter overflows. 2^32(-1) ticks at 32KHz is about 36 hours. Using
 * uint64_t for the running counter, so 2^64(-1) ticks before overflow, with the same
 * setup would get you over a million years before overflow.
 *
 * This running counter also gets reset to 0 when there are no active timers, so the overflow
 * condition will only occur when there have been one or more active timers continuously for
 * the maximum number of ticks.
 */
#if defined(APP_TIMER_RUNNING_COUNT_UINT32)
typedef uint32_t app_timer_running_count_t;
#elif defined(APP_TIMER_RUNNING_COUNT_UINT64)
typedef uint64_t app_timer_running_count_t;
#else
#error "Running counter width is not defined"
#endif // APP_TIMER_RUNNING_COUNTER_*


/**
 * Datatype used to represent the interrupt status passed to 'set_interrupts_enabled'
 */
#if defined(APP_TIMER_INT_UINT32)
typedef uint32_t app_timer_int_status_t;
#elif defined(APP_TIMER_INT_UINT64)
typedef uint64_t app_timer_int_status_t;
#else
#error "Interrupt status type is not defined"
#endif // APP_TIMER_INT_*


/**
 * Callback for timer expiry
 */
typedef void (*app_timer_handler_t)(void *);


/**
 * Enumerates all error codes returned by timer functions
 */
typedef enum
{
    APP_TIMER_OK,                   ///< Operation successful
    APP_TIMER_NULL_PARAM,           ///< NULL pointer passed as parameter
    APP_TIMER_INVALID_PARAM,        ///< Invalid data passed as parameter
    APP_TIMER_INVALID_STATE,        ///< Operation not allowed in current state (has app_timer_init been called?)
    APP_TIMER_ERROR                 ///< Unspecified internal error
} app_timer_error_e;


/**
 * Enumerates all possible timer types
 */
typedef enum
{
    APP_TIMER_TYPE_SINGLE_SHOT,   ///< Timer expires once, no reloading
    APP_TIMER_TYPE_REPEATING      ///< Continue reloading the timer on expiry, until stopped
} app_timer_type_e;


/**
 * Holds all information required to track a single timer instance
 */
typedef struct _app_timer_t
{
    volatile app_timer_running_count_t start_counts;  ///< Timer counts when timer was started
    volatile app_timer_running_count_t total_counts;  ///< Total timer counts until the next expiry
    struct _app_timer_t *volatile next;               ///< Timer scheduled to expire after this one
    struct _app_timer_t *volatile previous;           ///< Timer scheduled to expire before this one
    app_timer_handler_t handler;                      ///< Handler to run on expiry
    void *context;                                    ///< Optional pointer to extra data

    /**
     * Bit flags for timer
     *
     * Bit 0     : timer active. 1 when timer is active, 0 otherwise.
     * Bits 1-2  : timer type, one of app_timer_type_e
     * Bits 3-7  : unused
     */
    volatile uint8_t flags;
} app_timer_t;


/**
 * Defines an interface for interacting with arbitrary timer/counter hardware
 */
typedef struct
{
    /**
     * Initialize the hardware timer/counter
     *
     * @return  True if successful, otherwise false
     */
    bool (*init)(void);

    /**
     * Convert milliseconds to hardware timer/counter counts
     *
     * @param time   Time in arbitrary units
     *
     * @return  Time in hardware timer/counter counts
     */
    app_timer_running_count_t (*units_to_timer_counts)(app_timer_period_t time);

    /**
     * Read the current hardware timer/counter counts
     *
     * @return  Current hardware timer/counter counts
     */
    app_timer_count_t (*read_timer_counts)(void);

    /**
     * Configure the hardware timer/counter to generate an interrupt after a specific number of counts
     *
     * @param counts  Number of counts after which a timer interrupt should be generated
     *                (this will always be <= #max_count)
     */
    void (*set_timer_period_counts)(app_timer_count_t counts);

    /**
     * Start/Stop the hardware counter running
     *
     * @param enabled  If true, start counting. If false, stop counting.
     */
    void (*set_timer_running)(bool enabled);

    /**
     * Enable/disable interrupts to protect access to the list of active timers
     *
     * @param enabled     If true, enable interrupts. If false, disable interrupts.
     *                    This function should disable/enable whatever interrupts are necessary
     *                    to protect access to the list of active timers, and that really depends
     *                    on your specific system setup; 'app_timer_start', 'app_timer_stop' and
     *                    'app_timer_target_count_reached' all modify the list of active timers, and if you
     *                    only ever call these functions in the same context (e.g. an ISR which is
     *                    always the same priority), then you may not need to disable any interrupts
     *                    here at all. Conversely, if you call 'app_timer_target_count_reached' in the
     *                    lowest-priority interrupt context, and 'app_timer_start'/'app_timer_stop'
     *                    in higher-priority interrupt contexts, then you might need to disable all
     *                    the higher-priority interrupts, or perhaps just all interrupts entirely.
     *
     * @param int_status  Pointer to interrupt status value. This is guaranteed to point
     *                    to the same location for both the 'enable' and 'disable' calls
     *                    for any one instance where interrupts are disabled to protect
     *                    access to the list of active timer instances. You may want to use
     *                    this value, for example, to save/restore interrupt status.
     */
    void (*set_interrupts_enabled)(bool enabled, app_timer_int_status_t *int_status);

    /**
     * The maximum value that the timer/counter can count up to before overflowing
     */
    app_timer_count_t max_count;
} app_timer_hw_model_t;


/**
 * This function must be called whenever the timer/counter period set by the
 * last call to set_timer_period_counts (in the hardware model) has elapsed. For example,
 * If you are implementing an interrupt-driven app_timer layer, you probably want to
 * call this function inside the interrupt handler for expiration of the timer/counter
 * peripheral you are using.
 */
void app_timer_target_count_reached(void);


/**
 * Initialize a timer instance. Must be called at least once before a timer can be
 * started with #app_timer_start.
 *
 * @param timer    Pointer to timer instance to initialize
 * @param handler  Handler to run on timer expiry. The handler will be called by 'app_timer_target_count_reached'.
 *                 The handler should return as quickly as possible; If the handler takes longer than
 *                 hw_model.max_count to return, then the app_timer module may fail to maintain an
 *                 accurate notion of time, which may cause future timer instances to expire at
 *                 unexpected times.
 * @param type     Type of timer to create
 *
 * @return #APP_TIMER_OK if successful
 */
app_timer_error_e app_timer_create(app_timer_t *timer, app_timer_handler_t handler, app_timer_type_e type);


/**
 * Start a timer. The timer instance provided must have already been initialized
 * by #app_timer_create, and the memory holding the timer instance must remain accessible
 * throughout the lifetime of the timer (don't put an app_timer_t on the stack). The
 * handler passed to #app_timer_create will be invoked when the timer expires.
 *
 * Calling #app_timer_start on a timer that has already been started will have no effect
 * on the timer.
 *
 * @param timer          Pointer to timer instance to start. Must have already been
 *                       initialized by #app_timer_create).
 * @param time_from_now  Timer expiration time, relative to now, in units determined by the
 *                       hardware model in use (the units_to_timer_ticks function in the hardware
 *                       model is responsible for converting this value to timer/counter ticks).

 * @param context        Optional pointer to pass to handler function when it is called.
 *
 * @return #APP_TIMER_OK if successful
 */
app_timer_error_e app_timer_start(app_timer_t *timer, app_timer_period_t time_from_now, void *context);


/**
 * Stop a running timer instance.
 *
 * @param timer  Pointer to timer instance to stop.
 *
 * @return #APP_TIMER_OK if successful
 */
app_timer_error_e app_timer_stop(app_timer_t *timer);


/**
 * Initialize the app_timer module.
 *
 * @param model  Pointer to timer hardware model to use
 *
 * @return #APP_TIMER_OK if successful
 */
app_timer_error_e app_timer_init(app_timer_hw_model_t *model);


#ifdef __cplusplus
}
#endif

#endif // APP_TIMER_H

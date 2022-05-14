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
 *        3. Ensure that either APP_TIMER_COUNT_UINT8, APP_TIMER_COUNT_UINT16, or
 *           APP_TIMER_COUNT_UINT32 is set -- pick one that is large enough to hold
 *           all the bits of your hardware counter. For example, if you had a 12-bit
 *           counter, you could use either APP_TIMER_COUNT_UINT16 or APP_TIMER_COUNT_UINT32.
 *           If you don't define one of these options, the default is APP_TIMER_COUNT_UINT32.
 *
 *        4. Call app_timer_init() and pass in a pointer to the HW model you created
 *
 *        5. Now, app_timer_create and app_timer_start can be used to create as
 *           many timers as needed (see app_timer_api.h)
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
 * Defines the datatype used to represent a count value for the underlying hardware counter.
 */
#if !defined(APP_TIMER_COUNT_UINT8) && !defined(APP_TIMER_COUNT_UINT16) && !defined(APP_TIMER_COUNT_UINT32)
#define APP_TIMER_COUNT_UINT32  // Store hardware counter value in 32 bits by default
#endif


/**
 * Defines the datatype used to represent a running counter that spans across multiple hardware counter overflows
 */
#if !defined(APP_TIMER_RUNNING_COUNT_UINT16) && !defined(APP_TIMER_RUNNING_COUNT_UINT32)
#define APP_TIMER_RUNNING_COUNT_UINT32  // Store running counter value in 32 bits by default
#endif


/**
 * Defines the datatype used to represent the interrupt status passed to 'set_interrupts_enabled'
 */
#if !defined(APP_TIMER_INT_UINT32) && !defined(APP_TIMER_INT_UINT64)
#define APP_TIMER_INT_UINT32  // Store interrupt status value in 32 bits by default
#endif


/**
 * Datatype used to represent a count value for the underlying hardware counter.
 * This should be set to an unsigned fixed-width integer type that is large enough
 * to hold the number of bits the counter has.
 */
#if defined(APP_TIMER_COUNT_UINT8)
typedef uint8_t app_timer_count_t;
#elif defined(APP_TIMER_COUNT_UINT16)
typedef uint16_t app_timer_count_t;
#elif defined(APP_TIMER_COUNT_UINT32)
typedef uint32_t app_timer_count_t;
#else
#error "Hardware counter width is not defined"
#endif // APP_TIMER_COUNTER_*


/**
 * Datatype used to represent a running counter that spans across multiple hardware counter overflows
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
    APP_TIMER_OK,
    APP_TIMER_INVALID_PARAM,
    APP_TIMER_INVALID_STATE,
    APP_TIMER_ERROR
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
    struct _app_timer_t *volatile next;               ///< Timer scheduled to expire after this one
    struct _app_timer_t *volatile previous;           ///< Timer scheduled to expire before this one
    volatile app_timer_running_count_t start_counts;  ///< Timer counts when timer was started
    volatile app_timer_running_count_t total_counts;  ///< Total timer counts until the next expiry
    app_timer_handler_t handler;                      ///< Handler to run on expiry
    void *context;                                    ///< Optional pointer to extra data
    app_timer_type_e type;                            ///< Type of timer
} app_timer_t;


/**
 * Defines an interface for interacting with arbitrary timer/counter hardware
 */
typedef struct
{
    /**
     * Initialize the HW timer/counter
     *
     * @return  True if successful, otherwise false
     */
    bool (*init)(void);

    /**
     * Convert milliseconds to HW timer/counter counts
     *
     * @param ms   Time in millseconds
     *
     * @return  Time in HW timer/counter counts
     */
    app_timer_running_count_t (*ms_to_timer_counts)(uint32_t ms);

    /**
     * Read the current HW timer/counter counts
     *
     * @return  Current HW timer/counter counts
     */
    app_timer_count_t (*read_timer_counts)(void);

    /**
     * Configure the HW timer/counter to expire after a certain number of counts
     *
     * @param counts  Number of counts to configure the HW timer/counter for
     *                (this will always be <= #max_count)
     */
    void (*set_timer_period_counts)(app_timer_count_t counts);

    /**
     * Enable/disable the HW timer to start/stop counting
     *
     * @param enabled  If true, start counting. If false, stop counting.
     */
    void (*set_timer_running)(bool enabled);

    /**
     * Enable/disable interrupts for the HW timer/counter
     *
     * @param enabled     If true, enable interrupts. If false, disable interrupts.
     * @param int_status  Pointer to interrupt status value. This is guaranteed to point
     *                    to the same location for both the 'enable' and 'disable' calls
     *                    for any one instance where interrupts are disabled to protect
     *                    access to the list of active timer instances. You may want to use
     *                    this value, for example, to save/restore interrupt status.
     */
    void (*set_interrupts_enabled)(bool enabled, app_timer_int_status_t *int_status);

    /**
     * The maximum value that the HW timer/counter can count up to before overflowing
     */
    app_timer_count_t max_count;
} app_timer_hw_model_t;


/**
 * Interrupt handler for the HW timer/counter. This function should be called by
 * application-specific code inside the interrupt handler for timer/counter expiration.
 */
void app_timer_on_interrupt(void);


/**
 * Initialize a timer instance. Must be called at least once before a timer can be 
 * started with #app_timer_start.
 *
 * @param timer    Pointer to timer instance to initialize
 * @param handler  Handler to run on timer expiry. The handler should return as quickly as possible. If
 *                 the handler takes longer than hw_model.max_count to return, then the app_timer module may
 *                 fail to maintain an accurate notion of time, which may cause future timer instances to
 *                 expire at an unexpected times.
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
 * @param timer        Pointer to timer instance to start. Must have already been
 *                     initialized by #app_timer_create).
 * @param ms_from_now  Timer expiration time, relative to now, in milliseconds.
 * @param context      Optional pointer to pass to handler function when it is called.
 *
 * @return #APP_TIMER_OK if successful
 */
app_timer_error_e app_timer_start(app_timer_t *timer, uint32_t ms_from_now, void *context);


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

/**
 * @file arduino_app_timer.c
 * @author Erik Nyquist
 * 
 * @brief Implements an app_timer HW model for the Arduino UNO platform, making use of the TIMER1
 *        peripheral to generate interrupts.
 * 
 *        Note that this HW model is not suitable for projects that are already using TIMER1; for example,
 *        some motor/servo libraries use TIMER1 to generate a PWM signal.
 */

#include <Arduino.h> 
#include <stdint.h>
#include "arduino_app_timer.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * The Arduino UNO's clock frequency; the speed at which the timer will count if
 * prescaler is 0
 */
#define HW_SYS_CLK_FREQ         (16000000UL)

/**
 * Using a 16-bit timer/counter
 */
#define HW_TIMER_MAX_COUNT      ((app_timer_count_t) 0xffffu)


// Convert milliseconds to TIMER1 counts. Timer1 uses a 16MHz clock with a
// prescaler of 1024, resulting in a tick rate of 15,625Hz
static app_timer_running_count_t _ms_to_timer_counts(uint32_t ms)
{
    return ((HW_SYS_CLK_FREQ / 1024UL / 100UL) * ms) / 10UL;
}


// Read the TIMER1 counter
static app_timer_count_t _read_timer_counts(void)
{
    return TCNT1;
}


// Configure TIMER1 to overflow after a specific number of counts
static void _set_timer_period_counts(app_timer_count_t counts)
{
    uint16_t preload = (HW_TIMER_MAX_COUNT + 1u) - counts;
    TCNT1 = preload;
}


// Start/stop TIMER1 from counting
static void _set_timer_running(bool enabled)
{
    if (enabled)
    {
        TIMSK1 |= (1 << TOIE1); // enable timer overflow interrupt
    }
    else
    {
        TIMSK1 &= ~(1 << TOIE1); // disable timer overflow interrupt
    }
}


// Enable/disable interrupts
static void _set_interrupts_enabled(bool enabled, app_timer_int_status_t *int_status)
{
	(void) int_status; // Unused for this hardware model

    if (enabled)
    {
        interrupts();
    }
    else
    {
        noInterrupts();
    }
}


// Initialize hardware model
static bool _init(void)
{
    app_timer_int_status_t int_status = 0u;
    _set_interrupts_enabled(false, &int_status);
    TCCR1A = 0;
    TCCR1B = 0;
    TCCR1B |= (1 << CS12) | (1 << CS10); // 1024 prescaler
    _set_interrupts_enabled(true, &int_status);
    return true;
}


// ISR for timer interrupt
ISR(TIMER1_OVF_vect)
{
    app_timer_on_interrupt();
}


// Hardware model definition
static app_timer_hw_model_t _arduino_hw_model = {
    .init = _init,
    .ms_to_timer_counts = _ms_to_timer_counts,
    .read_timer_counts = _read_timer_counts,
    .set_timer_period_counts = _set_timer_period_counts,
    .set_timer_running = _set_timer_running,
    .set_interrupts_enabled = _set_interrupts_enabled,
    .max_count = HW_TIMER_MAX_COUNT
};


/**
 * @see arduino_app_timer.h
 */
app_timer_error_e arduino_app_timer_init(void)
{
    return app_timer_init(&_arduino_hw_model);
}
#ifdef __cplusplus
}
#endif

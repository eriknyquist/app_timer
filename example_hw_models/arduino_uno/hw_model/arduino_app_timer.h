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


#ifndef ARDUINO_APP_TIMER_H
#define ARDUINO_APP_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_timer_api.h"

/**
 * Intitializes the app_timer module with a HW model suitable for
 * the Arduino UNO platform. Call this in the 'setup' function of your sketch.
 *
 * @return #APP_TIMER_OK if initialized successfully
 */
app_timer_error_e arduino_app_timer_init(void);

#ifdef __cplusplus
}
#endif

#endif // ARDUINO_APP_TIMER_H

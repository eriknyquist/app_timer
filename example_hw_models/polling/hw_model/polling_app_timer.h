/**
 * @file arduino_app_timer.c
 * @author Erik Nyquist
 *
 * @brief Implements an app_timer HW model for any C program running on a modern OS,
 *        using a polling approach
 */


#ifndef POLLING_APP_TIMER_H
#define POLLING_APP_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_timer_api.h"

/**
 * Intitializes the app_timer module with the 'polling' hardware model
 *
 * @return #APP_TIMER_OK if initialized successfully
 */
app_timer_error_e polling_app_timer_init(void);

/**
 * Checks the current time and handles any expired timers. Call this as often
 * as possible in your main loop.
 */
void polling_app_timer_poll(void);

#ifdef __cplusplus
}
#endif

#endif // POLLING_APP_TIMER_H

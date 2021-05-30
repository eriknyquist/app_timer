/**
 * Example sketch showing how to use the app_timer module to re-create
 * the "blinky" sketch without a blocking/polling loop
 */

#include "arduino_app_timer.h"

static app_timer_t timer;

// Called when "timer" expires
void timer_callback(void *context)
{
    // Toggle the LED
    digitalWrite(13, digitalRead(13) ^ 1);
}

void setup()
{
    // Initialize the pin to control the LED
    pinMode(13, OUTPUT);

    // Initialize the app_timer library
    arduino_app_timer_init();

    // Create a new timer that will repeat until we stop it
    app_timer_create(&timer, &timer_callback, APP_TIMER_TYPE_REPEATING);

    // Start the timer to expire every 1000 milliseconds
    app_timer_start(&timer, 1000u, NULL);
}

void loop()
{
    // Don't need anything here, timer_callback will be called automatically when time is up
}

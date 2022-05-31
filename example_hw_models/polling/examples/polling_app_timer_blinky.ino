/**
 * Example sketch showing how to use the app_timer module to re-create
 * the "blinky" sketch with a polling app_timer hardware model
 */

#include "polling_app_timer.h"

static app_timer_t blink_timer;
static app_timer_t print_timer;

// tracks when the print timer has fired, so we can do the printing in the main loop and
// not in timer interrupt context
static volatile bool print_timer_fired = false;


// Called every time "blink_timer" expires
void blink_timer_callback(void *context)
{
    // Toggle the LED
    digitalWrite(13, digitalRead(13) ^ 1);
}

// Called every time "print_timer" expires
void print_timer_callback(void *context)
{
    // Nothing happening in interrupt context, we can print right inside the timer callback
    Serial.println("print");
}

void setup()
{
    // Initialize the pin to control the LED
    pinMode(13, OUTPUT);

    // Initialize Serial so we can print
    Serial.begin(115200);

    // Initialize the app_timer library (calls app_timer_init with the polling hardware model)
    polling_app_timer_init();

    // Create a new timer that will repeat until we stop it, for blinking
    app_timer_create(&blink_timer, &blink_timer_callback, APP_TIMER_TYPE_REPEATING);

    // Create a new timer that will repeat until we stop it, for blinking
    app_timer_create(&print_timer, &print_timer_callback, APP_TIMER_TYPE_REPEATING);

    // Start the blink timer to expire every 1000 milliseconds
    app_timer_start(&blink_timer, 1000u, NULL);

    // Start the print timer to expire every 1250 milliseconds
    app_timer_start(&print_timer, 1250u, NULL);
}

void loop()
{
    // Polling hardware model requires polling_app_timer_poll() to be called frequently
    polling_app_timer_poll();
}

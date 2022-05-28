/**
 * Basic smoke-test script that starts as many timers as can safely fit in the UNO's memory (about 60),
 * some single-shot and some repeating, all with different periods. There is also one
 * which blinks the LED a consistent rate, so I can leave this running on an arduino UNO
 * and watch to see if it gets hung up, to see if I have broken anything obvious....
 */

#include "arduino_app_timer.h"

// Number of app_timers to start running at once
#define NUM_SINGLE_TEST_TIMERS (32u)
#define NUM_REPEAT_TEST_TIMERS (32u)
#define NUM_TEST_TIMERS (NUM_SINGLE_TEST_TIMERS + NUM_REPEAT_TEST_TIMERS)

// First test timer will have this period
#define TEST_TIMER_PERIOD_START_MS (10u)

// Period will increment by this much for each subsequent test timer
#define TEST_TIMER_PERIOD_INCREMENT (2u)

// Struct to hold a timer instance and related data
typedef struct
{
    app_timer_t timer;
    volatile bool flag;
    volatile uint8_t period;
} test_timer_t;


// Timer structs
static test_timer_t test_timers[NUM_TEST_TIMERS];
static app_timer_t blink_timer;


// Called every time a single-shot test timer expires
void single_test_timer_callback(void *context)
{
    // Set flag for this timer
    test_timer_t *t = (test_timer_t *) context;
    t->flag = true;

    // Re-start timer
    app_timer_start(&t->timer, t->period, context);
}


// Called every time a repeating test timer expires
void repeat_test_timer_callback(void *context)
{
    // Set flag for this timer
    test_timer_t *t = (test_timer_t *) context;
    t->flag = true;
}


// Called every time "blink_timer" expires
void blink_timer_callback(void *context)
{
    // Toggle the LED
    digitalWrite(13, digitalRead(13) ^ 1);
}


void setup()
{
    // Initialize the pin to control the LED
    pinMode(13, OUTPUT);

    // Initialize Serial so we can print
    Serial.begin(115200);


    // Initialize the app_timer library (calls app_timer_init with the hardware model for arduino uno)
    arduino_app_timer_init();

    uint32_t period_ms = TEST_TIMER_PERIOD_START_MS;

    // Start all the single-shot timers
    for (uint32_t i = 0u; i < NUM_SINGLE_TEST_TIMERS; i++)
    {
        test_timers[i].flag = false;
        test_timers[i].period = (uint8_t) period_ms;
        app_timer_create(&test_timers[i].timer, &single_test_timer_callback, APP_TIMER_TYPE_SINGLE_SHOT);
        app_timer_start(&test_timers[i].timer, period_ms, &test_timers[i]);
        period_ms += TEST_TIMER_PERIOD_INCREMENT;
    }

    // Start all the repeating timers
    for (uint32_t i = NUM_SINGLE_TEST_TIMERS; i < NUM_TEST_TIMERS; i++)
    {
        test_timers[i].flag = false;
        test_timers[i].period = (uint8_t) period_ms;
        app_timer_create(&test_timers[i].timer, &repeat_test_timer_callback, APP_TIMER_TYPE_REPEATING);
        app_timer_start(&test_timers[i].timer, period_ms, &test_timers[i]);
        period_ms += TEST_TIMER_PERIOD_INCREMENT;
    }

    // Blink the LED every 250ms so we know if we're still running
    app_timer_create(&blink_timer, &blink_timer_callback, APP_TIMER_TYPE_REPEATING);
    app_timer_start(&blink_timer, 250u, NULL);
}

void loop()
{
    for (uint32_t i = 0u; i < NUM_TEST_TIMERS; i++)
    {
        // If ISR has set the flag, clear it. That's all. Just to give the main loop
        // something to do....
        if (test_timers[i].flag)
        {
            test_timers[i].flag = false;
        }
    }
}

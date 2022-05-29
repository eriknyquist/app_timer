#include <stdio.h>

#include "app_timer_api.h"
#include "polling_app_timer.h"


// Timer callback, will be invoked by polling_app_timer_poll when the timer expires
static void print_timer_callback(void *context)
{
    (void) context;
    printf("timer exired\n");
}

int main(int argc, char *argv[])
{
    // Initialize polling_app_timer
    polling_app_timer_init();

    app_timer_t print_timer;
    app_timer_error_e err = APP_TIMER_OK;

    // Create a new timer instance
    err = app_timer_create(&print_timer, &print_timer_callback, APP_TIMER_TYPE_REPEATING);
    if (APP_TIMER_OK != err)
    {
        printf("app_timer_create failed, err: 0x%x", err);
        return err;
    }

    // Start timer instance
    err = app_timer_start(&print_timer, 1000, NULL);
    if (APP_TIMER_OK != err)
    {
        printf("app_timer_create failed, err: 0x%x", err);
        return err;
    }

    // Enter polling loop-- polling_app_timer_poll must be called regularly
    while (true)
    {
        polling_app_timer_poll();
    }
}

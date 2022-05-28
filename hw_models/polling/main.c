#include <stdio.h>

#include "app_timer_api.h"
#include "polling_app_timer.h"


static void print_timer_callback(void *context)
{
    (void) context;
    printf("hey!\n");
}

int main(int argc, char *argv[])
{
    polling_app_timer_init();

    app_timer_t print_timer;

    app_timer_create(&print_timer, &print_timer_callback, APP_TIMER_TYPE_REPEATING);
    app_timer_start(&print_timer, 1000, NULL);

    while (true)
    {
        polling_app_timer_poll();
    }
}

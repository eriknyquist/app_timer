Hardware-agnostic  application timer layer in C
###############################################

This module implements an application timer layer in pure C, which allows you to
use a single timer/counter source to drive an arbitrary number of timed events, and provides
a flexible abstraction for the timer/counter source used to measure time (referred to as
the "hardware model"). Wether your source of time is a timer interrupt in an embedded system,
or a read-only monotonic counter that cannot generate interrupts, you can write a hardware
model that will allow app_timer to work with it.

If you are writing C or C++ for an embedded device that has a timer/counter peripheral that can
be configured to generate an interrupt when a certain count value is reached, and you would
like to implement an application timer layer driven by those interrupts, then this module might
be useful to you.

If you are writing a desktop application in C or C++, and you don't have any hardware timer interrupts
but you would like to implement an application timer layer driven by a polling loop, then this
module might also be useful to you.

It's the same idea as the "app_timer" library provided by Nordic's nRf5 SDK,
except with an abstraction layer for the actual timer/counter hardware, so that it can
be easily ported to other projects or embedded systems.

See API documentation in ``app_timer_api.h`` for more details.

Features / limitations
----------------------

- Flexible interface-- implement an interrupt-based app_timer layer for a microcontroller,
  or a completely synchronous app_timer layer that relies on a polling loop, or something else
  entirely. See examples in ``example_hw_models``.

- No dynamic memory allocation, and no hard limit on the number of timers that can be running simultaneously; when you call
  app_timer_start, your app_timer_t struct is linked into to a linked list of active timers, so all of your app_timer_t instances
  can be statically allocated if you want them to be, and there is no limit to how many app_timer_t instances can be running
  simulatenously (depending, that is, on your performance requirements; see next item...)

- Can handle any number of application timers running simultaneously, BUT the entire linked list of active timers may
  need to be traversed each time ``app_timer_on_interrupt`` is invoked (specifically, it will traverse the list until it
  finds a timer that has not expired yet), so in the case where there are a very large number of simultaneous active application
  timers, ``app_timer_on_interrupt`` may take noticeably more time to execute.

- Automatic handling of timer/counter overflow; if you are using a timer/counter, for example, which overflows after
  12 hours with your specific configuration, and you call ``app_timer_start`` with an expiry time of 14 hours,
  then the overflow will be handled behind the scenes by the ``app_timer`` module and your callback will still be
  invoked after 14 hours (as long as your have correctly set the ``max_count`` value in your hardware model).

Included hardware model and example sketch for Arduino UNO
----------------------------------------------------------

The ``example_hw_models/arduino_uno/`` directory contains an implementation of a hardware model for
the Arduino UNO, and also an example Arduino sketch (.ino file) that uses two app timer instances.

Example sketch- app_timer_blinky.ino
====================================

.. code:: cpp

    /**
     * Example sketch showing how to use the app_timer module to re-create
     * the "blinky" sketch without a blocking/polling loop
     */

    #include "arduino_app_timer.h"

    static app_timer_t blink_timer;
    static app_timer_t print_timer;

    // tracks when the print timer has fired, so we can do the printing in the main loop and
    // not in timer interrupt context
    static volatile bool print_timer_fired = false;


    // Called whenever "blink_timer" expires
    void blink_timer_callback(void *context)
    {
        // Toggle the LED
        digitalWrite(13, digitalRead(13) ^ 1);
    }

    // Called whenever "print_timer" expires
    void print_timer_callback(void *context)
    {
        // Printing takes a long time, so just a set a flag here and do the
        // actual printing in the main loop
        print_timer_fired = true;
    }

    void setup()
    {
        // Initialize the pin to control the LED
        pinMode(13, OUTPUT);

        // Initialize Serial so we can print
        Serial.begin(115200);

        // Initialize the app_timer library (calls app_timer_init with the hardware model for arduino uno)
        arduino_app_timer_init();

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
        // Check and see if print timer expired
        if (print_timer_fired)
        {
            print_timer_fired = false;
            Serial.println("print");
        }
    }

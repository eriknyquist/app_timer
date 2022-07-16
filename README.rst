Hardware-agnostic  application timer layer in C
###############################################

This is an "application timers" implementation in C, allowing you to use a single
timer/counter source to drive an arbitrary number of timed events. ``app_timer`` provides
a flexible abstraction for the timer/counter source used to measure time (referred to as
the "hardware model"). Whether your source of time is a timer interrupt in an embedded system,
or a read-only monotonic counter that cannot generate interrupts, or something else entirely,
you can write a hardware model that will allow app_timer to work with it.

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

- Use a single monotonic timer/counter source to drive many timed events.

- Written in pure C99, with no external dependencies, just the standard C lib
  (specifically, ``stdlib.h``, ``stdbool.h``, and ``stdint.h`` are required).

- Flexible interface-- implement an interrupt-based app_timer layer for a microcontroller,
  or a completely synchronous app_timer layer that relies on a polling loop with no interupts,
  or something else entirely. There are no OS-specific or hardware-specific dependencies. You
  can adapt ``app_timer`` to many different systems and situations (see examples in ``example_hw_models``).

- No dynamic memory allocation, and no limit on the number of timers that can be running simultaneously. When you call
  app_timer_start, your app_timer_t struct is linked into to a linked list with all other active timers (timers that
  have been started but not yet expired). The number of timers you can have running at once is really only limited by
  how much memory your system has available for declaring app_timer_t structs, whether statically or otherwise.

- Automatic handling of timer/counter overflow; if you are using a timer/counter, for example, which overflows after
  30 minutes with your specific configuration, and you call ``app_timer_start`` with an expiry time of, say, 72 hours,
  then the overflow will be handled behind the scenes by the ``app_timer`` module and your timer handler function will
  still only be called after 72 hours.

Getting started
---------------

#. Implement a hardware model for your specific time source, or use one of the samples
   in ``example_hw_models`` if there is an appropriate one. In this case, we'll use the
   arduino UNO hardware model, ``arduino_app_timer.c`` and ``arduino_app_timer.h``,
   for discussion's sake.

#. In your application code, ensure that ``app_timer_init`` is being called, and that
   a pointer to the app_timer_hw_model_t struct for your hardware model is passed in.
   The arduino hardware model provides a ``arduino_app_timer_init`` function which
   does exactly this.

#. Ensure that ``app_timer.c`` and ``arduino_app_timer.c`` (or whatever hardware model
   you are using, if not arduino) are compiled and linked in along with the rest of your
   application.

#. That's it. Now that ``app_timer`` has been initialized with a hardware model,
   you can use the functions from ``app_timer_api.h`` in your application code to
   create and run ``app_timer_t`` instances.

Build options
-------------

There are several preprocessor symbols you can define to change various things at compile time.
The following sections provide some details about those options.

Re-configure counter without stopping & restarting it
=====================================================

The default behaviour is to always stop the counter before setting a new period via
``hw_model->set_timer_period_counts``, and then re-start the counter, like so:

::

    hw_model->set_timer_running(false);
    hw_model->set_timer_period_counts(new_timer_period);
    hw_model->set_timer_running(true);

Alternatively, if you want the counter to be re-configured for a new period without
stopping and re-starting, such that ``hw_model->set_timer_running(false)`` will only be called
to stop the counter in the event that there are no active timers, you can define the following option;

+---------------------------------------------+--------------------------------------------------------------------------------------------------+
| **Symbol name**                             | **What you get if you define this symbol**                                                       |
+=============================================+==================================================================================================+
| ``APP_TIMER_RECONFIG_WITHOUT_STOPPING``     | Counter will not be stopped to set a new timer period with ``hw_model->set_timer_period_counts`` |
+---------------------------------------------+--------------------------------------------------------------------------------------------------+

Enable interrupts when running timer handler functions
======================================================

``app_timer_target_count_reached`` disables interrupts via the hardware model ``set_interrupt_enabled``
function, in order to protect access to the timer hardware and the list of active timers. By default,
interrupts will be disabled the entire time that ``app_timer_target_count_reached`` is executing,
including when timer handler functions are called. If you need interrupts to be functional
when timer handler functions are called, you can define the following option;

+---------------------------------------------+---------------------------------------------------------------------------------------------+
| **Symbol name**                             | **What you get if you define this symbol**                                                  |
+=============================================+=============================================================================================+
| ``APP_TIMER_ENABLE_INTERRUPTS_FOR_HANDLER`` | ``app_timer_target_count_reached`` will re-enable interrupts to run timer handler functions |
+---------------------------------------------+---------------------------------------------------------------------------------------------+

Datatype used for app_timer_period_t
====================================

Determines the datatype used to represent the period for a timer (e.g. the
'time_from_now' parameter passed to app_timer_start).

For example, if you are using a hardware model that expects milliseconds for timer periods,
and uint32_t is used for timer periods, then the max. timer period you can pass to app_timer_start
is 2^32 milliseconds, or about 49 days.

Define one of the following options;

+---------------------------------------+------------------------------------------------------------+
| **Symbol name**                       | **What you get if you define this symbol**                 |
+=======================================+============================================================+
| ``APP_TIMER_PERIOD_UINT32``           | ``app_timer_period_t`` is type ``uint32_t`` **(default)**  |
+---------------------------------------+------------------------------------------------------------+
| ``APP_TIMER_PERIOD_UINT64``           | ``app_timer_period_t`` is type ``uint64_t``                |
+---------------------------------------+------------------------------------------------------------+


Datatype used for app_timer_count_t
===================================

Determines the datatype used to represent a count value for the underlying hardware counter.
This should be set to a type that is large enough to hold the largest hardware counter value.
For example, if using a 24-bit counter, uint32_t would be sufficient, but not uint16_t.

Define one of the following options;

+---------------------------------------+------------------------------------------------------------+
| **Symbol name**                       | **What you get if you define this symbol**                 |
+=======================================+============================================================+
| ``APP_TIMER_COUNT_UINT16``            | ``app_timer_count_t`` is type ``uint16_t``                 |
+---------------------------------------+------------------------------------------------------------+
| ``APP_TIMER_COUNT_UINT32``            | ``app_timer_count_t`` is type ``uint32_t`` **(default)**   |
+---------------------------------------+------------------------------------------------------------+


Datatype used for app_timer_running_count_t
===========================================

Determines the datatype used to represent a running counter that tracks total elapsed time
since one or more active timers have been running continuously.

You should pick this according to the expected lifetime of your system. Let's
say, for example, that you are using a counter driven by a 32KHz clock; this
would mean using uint32_t for the running counter allows the app_timer module
to have timers running continuously for up to 2^32(-1) ticks, before the running
counter overflows. 2^32(-1) ticks at 32KHz is about 36 hours. Using
uint64_t for the running counter, so 2^64(-1) ticks before overflow, with the same
setup would get you over a million years before overflow.

This running counter also gets reset to 0 when there are no active timers, so the overflow
condition will only occur when there have been one or more active timers continuously for
the maximum number of ticks.

Define one of the following options;

+---------------------------------------+--------------------------------------------------------------------+
| **Symbol name**                       | **What you get if you define this symbol**                         |
+=======================================+====================================================================+
| ``APP_TIMER_RUNNING_COUNT_UINT32``    | ``app_timer_running_count_t`` is type ``uint32_t`` **(default)**   |
+---------------------------------------+--------------------------------------------------------------------+
| ``APP_TIMER_RUNNING_COUNT_UINT64``    | ``app_timer_running_count_t`` is type ``uint64_t``                 |
+---------------------------------------+--------------------------------------------------------------------+


Datatype used for app_timer_int_status_t
========================================

Determines the datatype used to represent the interrupt status passed to 'set_interrupts_enabled'.

Define one of the following options;

+---------------------------------------+--------------------------------------------------------------------+
| **Symbol name**                       | **What you get if you define this symbol**                         |
+=======================================+====================================================================+
| ``APP_TIMER_INT_UINT32``              | ``app_timer_int_status_t`` is type ``uint32_t`` **(default)**      |
+---------------------------------------+--------------------------------------------------------------------+
| ``APP_TIMER_INT_UINT64``              | ``app_timer_int_status_t`` is type ``uint64_t``                    |
+---------------------------------------+--------------------------------------------------------------------+


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

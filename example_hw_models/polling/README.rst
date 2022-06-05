Example HW model implementation and example programs
----------------------------------------------------

These example files show how to implement a hardware model that depends on
polling a counter/timer value, which may be useful in cases where you don't have easy
access to hardware timer interrupts. This hardware model specifically supports Windows,
Linux, and Arduino.

How to run Arduino example sketch
=================================

#. Ensure you have the `Arduino IDE <https://www.arduino.cc/en/software>`_ installed

#. Double-click on the ``example_hw_models/polling/examples/app_timer_blinky.ino``
   file (Click 'Yes' if you get a window asking if you want to create a new folder for the sketch).

#. Once the Arduino IDE has opened, ensure you have an Arduino UNO connected,
   and the correct board and port are selected in the Arduino IDE

#. Drag the following additional files into the files tab:

   * ``app_timer.c``
   * ``app_timer_api.h``
   * ``example_hw_models/polling/hw_model/polling_app_timer.c``
   * ``example_hw_models/polling/hw_model/polling_app_timer.h``
   * ``example_hw_models/polling/hw_model/timing.c``
   * ``example_hw_models/polling/hw_model/timing.h``

#. You can now compile/upload the sketch.

How to run examples for Linux and Windows
=========================================

Windows-only setup
##################

#. You need some version of GCC installed and on your system PATH-- easiest way to do that is
   `install MinGW <http://www.codebind.com/cprogramming/install-mingw-windows-10-gcc/>`_.

#. You need some version of GNU Make installed and on your system PATH-- if you did not get
   that from the MinGW install in the previous step, then you can install it separately
   `here <http://gnuwin32.sourceforge.net/packages/make.htm>`_

Linux-only setup
################

There should be no setup required on most major linux distributions.


Build example_main.c example program, both Windows and Linux
############################################################

This program creates a single timer that writes to stdout via ``printf`` once every second,
starts the timer, and then enters an infinite loop running the ``polling_app_timer_poll`` function.

#. Run make with the 'example' target:

   ::

       make example

#. The output will be a program called ``build/example_main``.

Build test_main.c example program, both Windows and Linux
#########################################################

This program creates hundreds of timers with different periods, runs them all for 80 minutes,
and then prints out some collected metrics about their measured accuracy and behaviour.

#. Run make with the 'test' target:

   ::

       make test

#. The output will be a program called ``build/test_main``.


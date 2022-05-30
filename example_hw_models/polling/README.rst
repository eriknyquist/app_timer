Example HW model implementation and example programs
----------------------------------------------------

These example files show how to implement a hardware model that depends on 
polling a counter/timer value, which may be useful in cases where you don't have easy
access to hardware timer interrupts. This hardware model is specifically for use on a
modern Windows or Linux OS.

How to set up build environment
===============================

Windows setup
#############

#. You need some version of GCC installed and on your system PATH-- easiest way to do that is
   `install MinGW <http://www.codebind.com/cprogramming/install-mingw-windows-10-gcc/>`_.

#. You need some version of GNU Make installed, if you did not get that from the
   MinGW install in the previous step, then you can install it separately
   `here <http://gnuwin32.sourceforge.net/packages/make.htm>`_

Linux setup
###########

There should be no setup required on most major linux distributions.

Build example_main.c example program
====================================

This program creates a single timer that writes to stdout via ``printf`` once every second,
and then enters an infinite loop running the ``polling_app_timer_poll`` function.

#. Run make with the 'example' target:

   ::

       make example

#. The output will be a program called ``build/example_main``.

Build test_main.c example program
=================================

This program creates hundreds of timers with different periods, runs them all for 1 minute,
and then prints out some metrics about their measured accuracy / consistency.

#. Run make with the 'test' target:

   ::

       make test

#. The output will be a program called ``build/test_main``.


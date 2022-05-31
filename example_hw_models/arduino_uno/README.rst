Arduino HW model implementation and example arduino sketches
------------------------------------------------------------

These example files show how to implement an app_timer hardware model driven by
the TIMER1 interrupt on an arduino UNO. An example arduino sketch is also provided,
which shows how to implement the classic "blinky" sketch using an app_timer instead
of blocking delays.

How to run Arduino example sketch
=================================

#. Ensure you have the `Arduino IDE <https://www.arduino.cc/en/software>`_ installed

#. Double-click on the ``example_hw_models/arduino_uno/examples/app_timer_blinky.ino``
   file (Click 'Yes' if you get a window asking if you want to create a new folder for the sketch).

#. Once the Arduino IDE has opened, ensure you have an Arduino UNO connected,
   and the correct board and port are selected in the Arduino IDE

#. Drag the following additional files into the files tab:

   * ``app_timer.c``
   * ``app_timer_api.h``
   * ``example_hw_models/arduino_uno/hw_model/arduino_app_timer.c``
   * ``example_hw_models/arduino_uno/hw_model/arduino_app_timer.h``

#. You can now compile/upload the sketch.

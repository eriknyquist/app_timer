## app_timer abstraction layer for any timer/counter HW that can generate interrupts

Implements a friendly abstraction layer for arbitrary non-blocking delays,
or "application timers", based on a single HW timer/counter peripheral that
can generate an interrupt when a specific count value is reached. Can handle
any number of application timers running simultaneously.

Basically, it's the same idea as the "app_timer" library provided by Nordic's nRf5 SDK,
except with an abstraction layer for the actual timer/counter hardware, so that it can
be easily ported to other projects or embedded systems.

See API documentation in `app_timer_api.h` for more details.

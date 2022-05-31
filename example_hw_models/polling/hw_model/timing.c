/**
 * @file timing.c
 * @author Erik Nyquist
 *
 * @brief Implements platform-specific time measurement function
 */


#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

#if defined(_WIN32)
#include <Windows.h>
static uint64_t _perf_freq;
#elif defined(__linux__)
#include <sys/time.h>
#elif defined(ARDUINO)
#include <Arduino.h>
#else
#error "Platform not supported"
#endif // _WIN32


/**
 * @see timing.h
 */
uint64_t timing_usecs_elapsed(void)
{
#if defined(_WIN32)
    LARGE_INTEGER tcounter = {0};
    uint64_t tick_value = 0u;
    if (QueryPerformanceCounter(&tcounter) != 0)
    {
        tick_value = tcounter.QuadPart;
    }

    return (uint64_t) (tick_value / (_perf_freq / 1000000ULL));
#elif defined(__linux__)
    struct timeval timer = {.tv_sec=0LL, .tv_usec=0LL};
    (void) gettimeofday(&timer, NULL);
    return (uint64_t) ((timer.tv_sec * 1000000LL) + timer.tv_usec);
#elif defined(ARDUINO)
    // This will actually wrap at (2^32)-1, since micros() returns a 32-bit
    // value. Make sure to use APP_TIMER_COUNT_UINT32 when using this polling
    // model on arduino platforms.
    return (uint64_t) micros();
#else
#error "Platform not supported"
#endif
}


/**
 * @see timing.h
 */
void timing_init(void)
{
#if defined(_WIN32)
    LARGE_INTEGER tcounter = {0};
    if (QueryPerformanceFrequency(&tcounter) != 0)
    {
        _perf_freq = tcounter.QuadPart;
    }
#elif defined(__linux__)
    // Nothing to do
#elif defined(ARDUINO)
    // Nothing to do
#else
#error "Platform not supported"
#endif // _WIN32
}

#ifdef __cplusplus
}
#endif

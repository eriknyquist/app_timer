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

    return (uint64_t) (tick_value / (_perf_freq / 1000000));
#elif defined(__linux__)
    struct timeval timer = {.tv_sec=0ll, .tv_usec=0ll};
    (void) gettimeofday(&timer, NULL);
    return (uint64_t) ((timer.tv_sec * 1000000ll) + timer.tv_usec);
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
#else
#error "Platform not supported"
#endif // _WIN32
}

#ifdef __cplusplus
}
#endif

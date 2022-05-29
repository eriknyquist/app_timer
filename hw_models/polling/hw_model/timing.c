/**
 * @file timing.c
 * @author Erik Nyquist
 *
 * @brief Implements platform-specific time measurement function
 */


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifdef _WIN32
#include <Windows.h>
static uint64_t _perf_freq;
#endif // _WIN32


/**
 * @see timing.h
 */
uint64_t timing_usecs_elapsed(void)
{
#ifdef _WIN32
    LARGE_INTEGER tcounter = {0};
    uint64_t tick_value = 0u;
    if (QueryPerformanceCounter(&tcounter) != 0)
    {
        tick_value = tcounter.QuadPart;
    }

    return (uint64_t) (tick_value / (_perf_freq / 1000000));
#else
#error "Platform not supported"
#endif
}


/**
 * @see timing.h
 */
void timing_init(void)
{
#ifdef _WIN32
    LARGE_INTEGER tcounter = {0};
    if (QueryPerformanceFrequency(&tcounter) != 0)
    {
        _perf_freq = tcounter.QuadPart;
    }
#else
#error "Platform not supported"
#endif // _WIN32
}

#ifdef __cplusplus
}
#endif

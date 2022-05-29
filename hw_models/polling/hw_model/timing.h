/**
 * @file timing.h
 * @author Erik Nyquist
 *
 * @brief Implements platform-specific time measurement function
 */


#ifdef __cplusplus
extern "C" {
#endif

#ifndef TIMING_H
#define TIMING_H

/**
 * Get current time in microseconds
 *
 * @return Current time in microseconds
 */
uint64_t timing_usecs_elapsed(void);


/**
 * Initialize timing
 */
void timing_init(void);

#endif // TIMING_H

#ifdef __cplusplus
}
#endif

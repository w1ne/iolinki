#ifndef IOLINK_PLATFORM_H
#define IOLINK_PLATFORM_H

/**
 * @file platform.h
 * @brief Platform encapsulation for RTOS integration.
 *
 * This file provides weak definitions or macros for critical sections.
 * Platforms/RTOS integrations should override these to ensure thread safety.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter a critical section (disable interrupts/scheduler).
 *
 * This function must prevent context switches or ISRs that could
 * corrupt shared data structures (Events, ISDU).
 */
void iolink_critical_enter(void);

/**
 * @brief Exit a critical section (restore interrupts/scheduler).
 */
void iolink_critical_exit(void);

/* Default (Weak) Implementation typically provided in a platform source file.
 * If strictly header-only or macro-based is preferred, use #define macros here.
 * For this stack, we declere functions that the user must implement or we provide weak defaults.
 */

#ifdef __cplusplus
}
#endif

#endif // IOLINK_PLATFORM_H

#ifndef IOLINKI_IOLINK_H_
#define IOLINKI_IOLINK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the IO-Link stack
 *
 * @return 0 on success, error code otherwise
 */
int iolink_init(void);

#ifdef __cplusplus
}
#endif

#endif  // IOLINKI_IOLINK_H_

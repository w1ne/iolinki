# Platform Porting Guide

## What a port includes

A port is an `iolink_phy_api_t` plus the application that owns the stack context. The host demo and the Zephyr samples for `nucleo_l476rg` and `nucleo_f103rb` are the builds this repository exercises. An IAR EWARM build for STM32G0B1RE plus TI TIOL112 is not verified here. The renewal artefact is the directory written by `tools/evidence_bundle.py`: stack SBOM, conformance log hash, and the 12-month update window. That directory does not mark the device CRA-compliant. Firmware update and secure boot stay in the device.

## Overview

For the current context-based PHY API and a working transceiver control
implementation, start with [TIOL112 integration](hardware/TIOL112.md) and
[the reference device](../examples/reference_device/README.md). The older snippets
below predate the current callback signatures and are conceptual illustrations;
do not copy them unchanged. In particular, a millisecond-derived microsecond
clock and a one-millisecond polling delay do not establish physical IO-Link timing.

iolinki is designed to be portable across different platforms and RTOSes. This guide explains how to port the stack to your target platform.

## Platform Abstraction Layers

### 0. Context-based API (Architecture)
The stack uses a context-based API to avoid global state and ensure reentrancy.
All public API functions take a context pointer (e.g., `iolink_dll_ctx_t*`).
When porting, ensure your application manages this context storage (stack or static).

### 1. PHY Layer (Required)

The PHY layer provides hardware abstraction for UART communication.

**Location**: `include/iolinki/phy.h`

**Interface**: copy `iolink_phy_api_t` from `include/iolinki/phy.h`. Every callback takes `void* user` first. `send` returns the number of bytes sent, or negative on error. `recv_byte` returns 1 when a byte was read, 0 when none is waiting, or negative on error. `detect_wakeup`, `set_cq_line`, `get_voltage_mv`, and `is_short_circuit` are optional and may be NULL. `init`, `set_mode`, and `set_baudrate` are required.

**Implementation Steps**:

1. Create `src/platform/<your_platform>/phy_<your_platform>.c`
2. Implement all required PHY functions
3. Export PHY API structure

**Example** (STM32 HAL). `init`, `set_mode`, and `set_baudrate` are still required and must take `void* user` first. Do not invent transceiver register addresses.

```c
static int stm32_send(void* user, const uint8_t* data, size_t len)
{
    UART_HandleTypeDef* uart = user;
    if (HAL_UART_Transmit(uart, (uint8_t*)data, (uint16_t)len, 10) != HAL_OK) {
        return -1;
    }
    return (int)len;
}

static int stm32_recv_byte(void* user, uint8_t* byte)
{
    UART_HandleTypeDef* uart = user;
    if (HAL_UART_Receive(uart, byte, 1, 0) != HAL_OK) {
        return 0;
    }
    return 1;
}
```

### 2. Time Utilities (Required)

Provide timing functions for your platform.

**Location**: `src/platform/<platform>/time_utils.c`

**Interface**:
```c
uint32_t iolink_time_get_ms(void);
uint64_t iolink_time_get_us(void);
```

**Example** (FreeRTOS):
```c
#include "iolinki/time_utils.h"
#include "FreeRTOS.h"
#include "task.h"

uint32_t iolink_time_get_ms(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint64_t iolink_time_get_us(void) {
    return IOLINK_US_FROM_MS(iolink_time_get_ms());
}
```

**Example** (Bare Metal with SysTick):
```c
static volatile uint32_t g_tick_ms = 0;

void SysTick_Handler(void) {
    g_tick_ms++;
}

uint32_t iolink_time_get_ms(void) {
    return g_tick_ms;
}

uint64_t iolink_time_get_us(void) {
    return IOLINK_US_FROM_MS(g_tick_ms);
}
```

### 3. Data Storage (Optional)

Implement persistent storage for Device parameters.

**Interface**:
```c
typedef struct {
    int (*load)(uint8_t *data, uint16_t *len);
    int (*store)(const uint8_t *data, uint16_t len);
} iolink_ds_storage_api_t;
```

**Example** (EEPROM):
```c
static int eeprom_load(uint8_t *data, uint16_t *len) {
    *len = EEPROM_Read(IOLINK_DS_ADDR, data, IOLINK_DS_MAX_SIZE);
    return 0;
}

static int eeprom_store(const uint8_t *data, uint16_t len) {
    return EEPROM_Write(IOLINK_DS_ADDR, data, len);
}

const iolink_ds_storage_api_t g_storage_eeprom = {
    .load = eeprom_load,
    .store = eeprom_store
};
```

## CMake Integration

### Option 1: Add Platform to iolinki CMake

Edit `CMakeLists.txt`:
```cmake
elseif(IOLINK_PLATFORM STREQUAL "STM32")
    target_sources(iolinki PRIVATE
        src/platform/stm32/phy_stm32.c
        src/platform/stm32/time_utils.c
    )
    target_link_libraries(iolinki PRIVATE stm32_hal)
endif()
```

Build:
```bash
cmake -B build -DIOLINK_PLATFORM=STM32
cmake --build build
```

### Option 2: Use iolinki as Submodule

In your project's `CMakeLists.txt`:
```cmake
add_subdirectory(external/iolinki)

add_executable(my_app
    src/main.c
    src/phy_custom.c
)

target_link_libraries(my_app PRIVATE iolinki)
```

## RTOS Integration

### FreeRTOS

```c
#include "FreeRTOS.h"
#include "task.h"
#include "iolinki/iolink.h"

void iolink_task(void *pvParameters) {
    iolink_init(&g_phy_stm32);

    while (1) {
        iolink_process();
        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms cycle
    }
}

int main(void) {
    xTaskCreate(iolink_task, "IOLink", 512, NULL, 2, NULL);
    vTaskStartScheduler();
}
```

### Zephyr RTOS

```c
#include <zephyr/kernel.h>
#include "iolinki/iolink.h"

void iolink_thread(void) {
    iolink_init(&g_phy_zephyr);

    while (1) {
        iolink_process();
        k_sleep(K_MSEC(1));
    }
}

K_THREAD_DEFINE(iolink_tid, 1024, iolink_thread, NULL, NULL, NULL, 5, 0, 0);
```

### Bare Metal

```c
#include "iolinki/iolink.h"

int main(void) {
    // Initialize SysTick for 1ms interrupt
    SysTick_Config(SystemCoreClock / 1000);

    iolink_init(&g_phy_baremetal);

    while (1) {
        iolink_process();
        __WFI();  // Wait for interrupt
    }
}

void SysTick_Handler(void) {
    g_tick_ms++;
    // Could call iolink_process() here instead
}
```

## Memory Requirements

For detailed RAM/ROM calculations and stack depth analysis, please refer to the [Memory Usage Guide](MEMORY_GUIDE.md).

### Quick Summary

- **Minimum RAM**: ~300 bytes (with optimized config)
- **Minimum Flash**: ~5 KB

### Configuration

The stack can be tuned via `include/iolinki/config.h`. Override the defaults by defining these macros in your build system (e.g., `-DIOLINK_ISDU_BUFFER_SIZE=64`):

- `IOLINK_ISDU_BUFFER_SIZE`: Size of ISDU transfer buffers.
- `IOLINK_EVENT_QUEUE_SIZE`: Number of events to queue.
- `IOLINK_PD_IN_MAX_SIZE`: Max process data input size.

## Hardware Requirements

### Minimum MCU Specs

- **CPU**: 16 MHz+ ARM Cortex-M0 or equivalent
- **RAM**: 4 KB minimum
- **Flash**: 16 KB minimum
- **Peripherals**: 1x UART with configurable baudrate

### Recommended MCU Families

- **STM32**: F0, F1, F4, L4, G0 series
- **NXP**: LPC, Kinetis, i.MX RT series
- **Nordic**: nRF52, nRF53 series
- **Espressif**: ESP32, ESP32-C3
- **Microchip**: SAM, PIC32 series

## Debugging

### Enable Debug Logging

Currently uses `printf()`. Replace with platform-specific logging:

```c
// In phy_stm32.c
#ifdef IOLINK_DEBUG
#define IOLINK_LOG(...) printf(__VA_ARGS__)
#else
#define IOLINK_LOG(...)
#endif
```

### Common Issues

1. **No communication**: Check UART baudrate and pin configuration
2. **CRC errors**: Verify byte order and timing
3. **Stack overflow**: Increase task stack size
4. **Timing issues**: Ensure `iolink_process()` called every 1ms

## Testing on New Platform

1. **Build test**: Verify compilation
2. **PHY test**: Test UART loopback
3. **CRC test**: Run CRC unit tests
4. **Integration test**: Use Virtual Master
5. **Hardware test**: Connect to real IO-Link Master

## Example Ports

See `src/platform/` for reference implementations:
- `linux/` - POSIX-based (development)
- `zephyr/` - Zephyr RTOS
- `baremetal/` - No-OS example

## Contributing Ports

If you port iolinki to a new platform, please contribute back:

1. Create `src/platform/<platform>/`
2. Implement PHY and time utilities
3. Add CMake support
4. Document hardware requirements
5. Submit pull request

## Support

For porting assistance, open an issue on GitHub with:
- Target platform/MCU
- RTOS (if any)
- Compiler toolchain
- Error messages or build logs

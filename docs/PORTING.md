# Platform Porting Guide

## Overview

iolinki is designed to be portable across different platforms and RTOSes. This guide explains how to port the stack to your target platform.

## Platform Abstraction Layers

### 1. PHY Layer (Required)

The PHY layer provides hardware abstraction for UART communication.

**Location**: `include/iolinki/phy.h`

**Interface**:
```c
typedef struct {
    int (*init)(void);
    void (*set_mode)(iolink_phy_mode_t mode);
    void (*set_baudrate)(iolink_baudrate_t baudrate);
    int (*send)(const uint8_t *data, size_t len);
    int (*recv_byte)(uint8_t *byte);
} iolink_phy_api_t;
```

**Implementation Steps**:

1. Create `src/platform/<your_platform>/phy_<your_platform>.c`
2. Implement all PHY functions
3. Export PHY API structure

**Example** (STM32 HAL):
```c
#include "iolinki/phy.h"
#include "stm32f4xx_hal.h"

static UART_HandleTypeDef huart;

static int stm32_phy_init(void) {
    huart.Instance = USART1;
    huart.Init.BaudRate = 4800;
    huart.Init.WordLength = UART_WORDLENGTH_8B;
    huart.Init.StopBits = UART_STOPBITS_1;
    huart.Init.Parity = UART_PARITY_NONE;
    huart.Init.Mode = UART_MODE_TX_RX;
    
    return (HAL_UART_Init(&huart) == HAL_OK) ? 0 : -1;
}

static void stm32_phy_set_baudrate(iolink_baudrate_t baudrate) {
    huart.Init.BaudRate = baudrate;
    HAL_UART_Init(&huart);
}

static int stm32_phy_send(const uint8_t *data, size_t len) {
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart, (uint8_t*)data, len, 100);
    return (status == HAL_OK) ? 0 : -1;
}

static int stm32_phy_recv_byte(uint8_t *byte) {
    HAL_StatusTypeDef status = HAL_UART_Receive(&huart, byte, 1, 0);
    return (status == HAL_OK) ? 1 : 0;
}

const iolink_phy_api_t g_phy_stm32 = {
    .init = stm32_phy_init,
    .set_mode = NULL,  // Optional
    .set_baudrate = stm32_phy_set_baudrate,
    .send = stm32_phy_send,
    .recv_byte = stm32_phy_recv_byte
};
```

### 2. Time Utilities (Required)

Provide timing functions for your platform.

**Location**: `src/platform/<platform>/time_utils.c`

**Interface**:
```c
uint32_t iolink_time_get_ms(void);
void iolink_time_delay_us(uint32_t us);
```

**Example** (FreeRTOS):
```c
#include "iolinki/time_utils.h"
#include "FreeRTOS.h"
#include "task.h"

uint32_t iolink_time_get_ms(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void iolink_time_delay_us(uint32_t us) {
    vTaskDelay(pdMS_TO_TICKS(us / 1000));
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

void iolink_time_delay_us(uint32_t us) {
    uint32_t start = g_tick_ms;
    while ((g_tick_ms - start) < (us / 1000));
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

### Minimum Requirements

- **RAM**: ~2 KB (stack + buffers)
  - DLL context: ~256 bytes
  - ISDU buffer: 256 bytes
  - Event queue: ~64 bytes
  - Stack usage: ~1 KB
  
- **Flash**: ~10 KB (code)
  - Core stack: ~6 KB
  - CRC tables: ~256 bytes
  - PHY implementation: ~2 KB

### Optimization Tips

1. **Reduce ISDU buffer**: Edit `IOLINK_ISDU_BUFFER_SIZE` (future config)
2. **Disable features**: Comment out unused ISDU handlers
3. **Use -Os**: Optimize for size in compiler flags

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

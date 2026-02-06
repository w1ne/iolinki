# API Reference

## Core API

### Initialization

```c
#include "iolinki/iolink.h"

int iolink_init(const iolink_phy_api_t *phy, const iolink_config_t *config);
```

Initialize the IO-Link stack with a PHY implementation and protocol configuration.

**Parameters**:
- `phy`: Pointer to PHY API structure
- `config`: Pointer to `iolink_config_t` structure (copied internally)

**Returns**: 0 on success, negative error code otherwise

#### Stack Configuration

```c
typedef struct {
    iolink_m_seq_type_t m_seq_type; /**< M-sequence type support */
    uint8_t min_cycle_time;         /**< Minimum cycle time (encoded per spec) */
    uint8_t pd_in_len;              /**< Process Data Input length (bytes) */
    uint8_t pd_out_len;             /**< Process Data Output length (bytes) */
} iolink_config_t;
```

**M-Sequence Types**:
- `IOLINK_M_SEQ_TYPE_0`: On-request data only.
- `IOLINK_M_SEQ_TYPE_1_1`: PD (fixed) + OD (1 byte).
- `IOLINK_M_SEQ_TYPE_1_2`: PD (fixed) + OD (1 byte) + ISDU.
- `IOLINK_M_SEQ_TYPE_2_1`: PD (fixed) + OD (2 bytes).
- `IOLINK_M_SEQ_TYPE_2_2`: PD (fixed) + OD (2 bytes) + ISDU.

**Example**:
```c
extern const iolink_phy_api_t g_phy_virtual;

int main(void) {
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_2,
        .pd_in_len = 2,
        .pd_out_len = 2
    };

    if (iolink_init(&g_phy_virtual, &config) != 0) {
        // Handle error
        return -1;
    }

    while (1) {
        iolink_process();
        // Sleep 1ms
    }
}
```

### Main Loop

```c
void iolink_process(void);
```

Process IO-Link stack logic. Must be called periodically (e.g., every 1ms).

## PHY Layer API

### PHY API Structure

```c
typedef struct {
    int (*init)(void);
    void (*set_mode)(iolink_phy_mode_t mode);
    void (*set_baudrate)(iolink_baudrate_t baudrate);
    int (*send)(const uint8_t *data, size_t len);
    int (*recv_byte)(uint8_t *byte);
} iolink_phy_api_t;
```

### PHY Modes

```c
typedef enum {
    IOLINK_PHY_MODE_INACTIVE,
    IOLINK_PHY_MODE_SIO,
    IOLINK_PHY_MODE_SDCI
} iolink_phy_mode_t;
```

### Baudrates

```c
typedef enum {
    IOLINK_BAUDRATE_COM1 = 4800,
    IOLINK_BAUDRATE_COM2 = 38400,
    IOLINK_BAUDRATE_COM3 = 230400
} iolink_baudrate_t;
```

### Implementing a PHY

```c
static int my_phy_init(void) {
    // Initialize UART hardware
    return 0;
}

static void my_phy_set_mode(iolink_phy_mode_t mode) {
    // Configure pin mode
}

static void my_phy_set_baudrate(iolink_baudrate_t baudrate) {
    // Configure UART baudrate
}

static int my_phy_send(const uint8_t *data, size_t len) {
    // Transmit bytes
    return 0;
}

static int my_phy_recv_byte(uint8_t *byte) {
    // Receive one byte (non-blocking)
    // Return 1 if byte received, 0 if no data
    return 0;
}

const iolink_phy_api_t g_my_phy = {
    .init = my_phy_init,
    .set_mode = my_phy_set_mode,
    .set_baudrate = my_phy_set_baudrate,
    .send = my_phy_send,
    .recv_byte = my_phy_recv_byte
};
```

## Application Layer API

### Callbacks

```c
typedef struct {
    void (*on_startup)(void);
    void (*on_preoperate)(void);
    void (*on_operate)(void);
    void (*on_pd_input)(const uint8_t *data, uint8_t len);
    void (*on_pd_output)(uint8_t *data, uint8_t len);
} iolink_app_callbacks_t;
```

### Registration

```c
void iolink_app_register(const iolink_app_callbacks_t *callbacks);
```

**Example**:
```c
static void on_operate(void) {
    printf("Device entered OPERATE state\n");
}

static void on_pd_input(const uint8_t *data, uint8_t len) {
    // Process input data from Master
}

const iolink_app_callbacks_t app_callbacks = {
    .on_operate = on_operate,
    .on_pd_input = on_pd_input
};

int main(void) {
    iolink_app_register(&app_callbacks);
    iolink_init(&g_phy_virtual);
    // ...
}
```

## ISDU API

### Reading ISDU

```c
int iolink_isdu_read(uint16_t index, uint8_t subindex,
                     uint8_t *data, uint8_t *len);
```

**Parameters**:
- `index`: ISDU index (0-65535)
- `subindex`: ISDU subindex (0-255)
- `data`: Buffer for data
- `len`: Input: buffer size, Output: actual data length

**Returns**: 0 on success, negative error code otherwise

### Writing ISDU

```c
int iolink_isdu_write(uint16_t index, uint8_t subindex,
                      const uint8_t *data, uint8_t len);
```

### ISDU Handler Registration

```c
typedef int (*iolink_isdu_handler_t)(uint16_t index, uint8_t subindex,
                                     uint8_t *data, uint8_t *len,
                                     bool is_write);

void iolink_isdu_register_handler(uint16_t index,
                                   iolink_isdu_handler_t handler);
```

**Example**:
```c
static int vendor_name_handler(uint16_t index, uint8_t subindex,
                                uint8_t *data, uint8_t *len,
                                bool is_write) {
    if (is_write) {
        return -1; // Read-only
    }

    const char *vendor = "MyCompany";
    *len = strlen(vendor);
    memcpy(data, vendor, *len);
    return 0;
}

void app_init(void) {
    iolink_isdu_register_handler(0x0010, vendor_name_handler);
}
```

## Event API

### Triggering Events

```c
void iolink_event_trigger(uint16_t event_code, iolink_event_type_t type);
```

**Event Types**:
```c
typedef enum {
    IOLINK_EVENT_TYPE_NOTIFICATION = 0,
    IOLINK_EVENT_TYPE_WARNING = 1,
    IOLINK_EVENT_TYPE_ERROR = 2
} iolink_event_type_t;
```

**Example**:
```c
// Trigger temperature warning
iolink_event_trigger(0x5012, IOLINK_EVENT_TYPE_WARNING);
```

### Checking Event Status

```c
bool iolink_events_pending(void);
```

## Data Storage API

### Initialization

```c
typedef struct {
    int (*load)(uint8_t *data, uint16_t *len);
    int (*store)(const uint8_t *data, uint16_t len);
} iolink_ds_storage_api_t;

void iolink_ds_init(const iolink_ds_storage_api_t *storage);
```

### Checksum Validation

```c
void iolink_ds_check(uint16_t master_checksum);
```

## Error Codes

```c
#define IOLINK_ERR_INVALID_PARAM    -1
#define IOLINK_ERR_NOT_READY        -2
#define IOLINK_ERR_TIMEOUT          -3
#define IOLINK_ERR_CRC              -4
#define IOLINK_ERR_BUSY             -5
#define IOLINK_ERR_NOT_SUPPORTED    -6
```

## Platform Abstraction

### Time API

```c
uint32_t iolink_time_get_ms(void);
uint64_t iolink_time_get_us(void);
```

Implement these functions for your platform in `src/platform/<platform>/time_utils.c`.

## Build Configuration

### CMake Options

```cmake
option(IOLINK_PLATFORM "Target platform" "LINUX")
option(IOLINK_ENABLE_TESTS "Enable unit tests" ON)
option(IOLINK_ENABLE_EXAMPLES "Enable examples" ON)
```

### Compile-Time Configuration

Create `iolink_config.h` (future feature):
```c
#define IOLINK_MAX_PD_SIZE 32
#define IOLINK_ISDU_BUFFER_SIZE 256
#define IOLINK_EVENT_QUEUE_SIZE 8
```

## Thread Safety

**Current Status**: Not thread-safe. All API calls must be from the same thread or protected by mutex.

**Future**: Context-based API will enable multi-instance support.

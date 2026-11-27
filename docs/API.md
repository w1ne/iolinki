# API Reference

## Core API

### Initialization

```c
#include "iolinki/device.h"
#include "iolinki/phy_virtual.h"

int iolink_device_init(iolink_device_ctx_t *ctx,
                       const iolink_device_config_t *config);
```

Initialize a Device context with a PHY implementation and protocol configuration.

**Parameters**:
- `ctx`: Storage for the Device runtime context
- `config`: Device setup, including its PHY and `iolink_config_t` in `stack`

**Returns**: 0 on success, negative error code otherwise

#### Stack Configuration

```c
typedef struct {
    iolink_m_seq_type_t m_seq_type; /**< M-sequence type support */
    uint8_t min_cycle_time;         /**< Minimum cycle time (encoded per spec) */
    uint8_t pd_in_len;              /**< Process Data Input length (bytes) */
    uint8_t pd_out_len;             /**< Process Data Output length (bytes) */
    uint32_t t_pd_us;               /**< Power-on delay in microseconds */
    uint8_t od_len;                 /**< OD width; 0 selects the type default */
    iolink_device_rate_t transmission_rate; /**< Fixed COM rate; 0 keeps COM2 */
} iolink_config_t;
```

`transmission_rate` configures the Device's fixed rate. Select
`IOLINK_DEVICE_RATE_COM1`, `IOLINK_DEVICE_RATE_COM2`, or
`IOLINK_DEVICE_RATE_COM3`. Its zero value,
`IOLINK_DEVICE_RATE_DEFAULT`, preserves the existing COM2 behavior for
zero-initialized and older partial aggregate configurations. The Device keeps
this rate across startup, wake-up and all fallback/recovery paths; it does not
negotiate a different rate with the Master.

**M-Sequence Types**:
- `IOLINK_M_SEQ_TYPE_0`: On-request data only.
- `IOLINK_M_SEQ_TYPE_1_1`: PD (fixed) + OD (1 byte).
- `IOLINK_M_SEQ_TYPE_1_2`: PD (fixed) + OD (1 byte) + ISDU.
- `IOLINK_M_SEQ_TYPE_2_1`: PD (fixed) + OD (2 bytes).
- `IOLINK_M_SEQ_TYPE_2_2`: PD (fixed) + OD (2 bytes) + ISDU.

**Example**:
```c
int main(void) {
    iolink_device_ctx_t device;
    iolink_device_config_t config = {0};
    iolink_phy_virtual_set_port("/dev/ttyACM0"); /* Supply the connected UART/PTY. */
    config.phy = *iolink_phy_virtual_get();
    config.stack = (iolink_config_t) {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
        .pd_in_len = 3U,
        .pd_out_len = 0U,
        .transmission_rate = IOLINK_DEVICE_RATE_COM3
    };

    if (iolink_device_init(&device, &config) != 0) {
        // Handle error
        return -1;
    }

    while (1) {
        iolink_device_process(&device);
        // Sleep 1ms
    }
}
```

### OD1 on a Variable M-sequence

For `IOLINK_M_SEQ_TYPE_2_V`, set `od_len` to `1` to use OD1. OD1 uses the
code-4 variant and requires at least three Process Data octets in either the
input or output direction. `od_len = 0` keeps the type's default OD2 setting.

```c
iolink_config_t config = {
    .m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
    .pd_in_len = 3,
    .od_len = 1,
    .transmission_rate = IOLINK_DEVICE_RATE_COM2
};
```

### Migration to Configurable Transmission Rate

No change is needed for configurations that are zero-initialized or omit the
new trailing field: they continue to start at COM2. To select another fixed
Device rate, set `transmission_rate` to one of the three `IOLINK_DEVICE_RATE_*`
values. Rebuild applications and libraries together because `iolink_config_t`
has gained a field.

### Main Loop

```c
void iolink_process(void);
```

Process IO-Link stack logic. Must be called periodically (e.g., every 1ms).

## PHY Layer API

### PHY API Structure

Copy `iolink_phy_api_t` from `include/iolinki/phy.h`. Every callback takes `void* user` first. `init`, `set_mode`, and `set_baudrate` are required. `send` returns the number of bytes sent, or a negative error. `recv_byte` returns 1 when a byte was read, 0 when none is waiting, or a negative error. `detect_wakeup`, `set_cq_line`, `get_voltage_mv`, and `is_short_circuit` may be NULL.

`IOLINK_BAUDRATE_COM1`, `COM2`, and `COM3` are 0, 1, and 2. They select 4.8, 38.4, and 230.4 kbit/s. They are not the baud number itself.

```c
static int my_phy_send(void* user, const uint8_t* data, size_t len) {
    (void)user;
    (void)data;
    return (int)len;
}

static int my_phy_recv_byte(void* user, uint8_t* byte) {
    (void)user;
    (void)byte;
    return 0;
}
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
    iolink_init(&g_phy_virtual, NULL); /* NULL = default configuration */
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

### Event Lifecycle (V1.1.5 10.10.2)

```c
iolink_events_ctx_t* ev = iolink_device_get_events_ctx(&device);

/* Error/Warning: "appears" now, "disappears" on explicit clear. */
iolink_event_appear(ev, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);
iolink_event_disappear(ev, IOLINK_EVENTCODE_SHORT_CIRCUIT);

/* Notification: single shot. */
iolink_event_notify(ev, IOLINK_EVENTCODE_NO_MALFUNCTION);
```

- A clear less than `IOLINK_EVENT_MIN_APPEAR_MS` (50 ms) after "appears" is
  deferred; the condition stays active until "disappears" is queued.
- The same EventCode and EventQualifier is not reported more than once per
  `IOLINK_EVENT_REPEAT_MS` (1000 ms). Suppressed or lost Events increment
  `iolink_events_dropped()`.
- `iolink_dll_process()` calls `iolink_events_process()` to queue deferred
  Events. The `_at` variants take an explicit millisecond time; elapsed time
  is computed with wrap-around-safe unsigned subtraction.
- DeviceStatus (0x0024) and DetailedDeviceStatus (0x0025) report the active
  conditions (Table B.13, Table B.14).
- On entry to STARTUP the DLL calls `iolink_events_startup_reset()`: queued
  Events and the Event flag are discarded, active conditions are reported
  again.

## Data Storage API

### Initialization

```c
typedef struct {
    int (*read)(uint32_t addr, uint8_t *buf, size_t len);
    int (*write)(uint32_t addr, const uint8_t *buf, size_t len);
    int (*erase)(uint32_t addr, size_t len); /* optional */
} iolink_ds_storage_api_t;

void iolink_ds_init(iolink_ds_ctx_t *ctx, const iolink_ds_storage_api_t *storage);
void iolink_ds_bind_params(iolink_ds_ctx_t *ctx, iolink_params_ctx_t *params_ctx);
```

`iolink_device_init()` binds the device's DS context to its own parameter
context (`iolink_device_get_ds_ctx()`). The DS storage backend
(`iolink_device_config_t.ds_storage`) is optional; parameters are always
committed through `iolink_nvm_write()` at the device's parameter record.

### Sessions

- `ParamDownloadStart` (SystemCommand 0x03) or `DS_DownloadStart`
  (0x0003 Subindex 1 = 0x03) opens a download session. Tag writes and
  0x0003 Subindex 0 image writes are checked and staged only (Table 98
  rules 1-3). A rejected write marks the set invalid.
- `ParamDownloadEnd` (0x04), `ParamDownloadStore` (0x05) or
  `DS_DownloadEnd` commit all staged values with one parameter-record NVM
  write. An invalid set answers 0x8041 and an NVM failure answers 0x8000; in
  both cases the previous set stays active and no upload request is raised.
- `ParamBreak` (0x06), `DS_Break` or a communication restart (DLL STARTUP)
  discards the staged values.
- `iolink_ds_process()` (called by `iolink_dll_process()`) only moves
  `*_REQ` to the active state. It never commits, closes or cancels a session.
- `ParamDownloadStore` sets DS_UPLOAD_FLAG (State_Property bit 7) and raises
  the single-shot Notification DS_UPLOAD_REQ (0xFF91) unless the Data Storage
  lock is set (Table 100 T2/T7).

### DataStorageIndex 0x0003 (Table B.10)

| Subindex | Access | Content |
|----------|--------|---------|
| 0 | R/W | Device-specific extension: serialized parameter image (not the Table F.19 record) |
| 1 | W | DS_Command 0x01-0x05 (reserved values: 0x8030; wrong state: 0x8036) |
| 2 | R | State_Property (bits 1-2 state, bit 7 DS_UPLOAD_FLAG) |
| 3 | R | Data_Storage_Size, UIntegerT32 |
| 4 | R | Parameter_Checksum, UIntegerT32 (Fletcher-16, zero-extended) |
| 5 | R | Index_List (Table B.11) with Termination_Marker 0x0000 |

With the Data Storage lock (0x000C bit 1) set, every write to 0x0003 is
refused with 0x8023. Block Parameterization (SystemCommand 0x01-0x06) is not
blocked by that lock (B.2.4).

### Checksum Validation

```c
void iolink_ds_check(iolink_ds_ctx_t *ctx, uint16_t master_checksum);
```

## SystemCommand and Reset Handler (Index 0x0002, Table B.9)

```c
void iolink_device_set_reset_handler(iolink_device_ctx_t *ctx, iolink_reset_handler_t handler);
```

| Command | Without handler | With handler |
|---------|-----------------|--------------|
| 0x80 Device reset | 0x8035 | (+); handler(`IOLINK_RESET_DEVICE`) after the response was read and 3 more M-sequences |
| 0x81 Application reset | 0x8035 | Access locks to default (checked NVM), handler(`IOLINK_RESET_APPLICATION`), DS upload request, then (+) |
| 0x82 Restore factory settings | (+) | Same, plus handler(`IOLINK_RESET_RESTORE_FACTORY`) before (+) |
| 0x83 Back-to-box | 0x8035 | Factory reset (checked NVM), (+); after the response and 3 more M-sequences handler(`IOLINK_RESET_BACK_TO_BOX`), then the DLL stops communication and sets the PHY to `IOLINK_PHY_MODE_INACTIVE` until re-initialization |

Restore factory settings (and Back-to-box) reset tags and DeviceAccessLocks
with one checked NVM write. Only on success they clear ErrorCount (0x0020),
send "disappears" for active Events, clear DeviceStatus/DetailedDeviceStatus
and delete the DS upload request. A failed NVM write answers 0x8000 and
changes nothing. Reserved, profile and vendor values answer 0x8035.

## Process Data Objects (Index 0x0028/0x0029)

- 0x0028 ProcessDataInput returns the last PDin published as valid by
  `iolink_device_pd_input_update(..., true)` (or a valid
  `pd_input_select` cycle). Invalid data keeps the previous snapshot.
- 0x0029 ProcessDataOutput returns the last PDout received while the Master
  declared it valid with MasterCommand 0x98 (ProcessDataOutputOperate,
  Table 55). 0x99 in OPERATE or leaving OPERATE resets the validity, not the
  snapshot.
- Before the first valid data: 0x8020. No PD in that direction: 0x8011. A PD
  length change drops the snapshot.

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

Compile-time buffer sizes are defined in `include/iolinki/config.h` and may be
overridden by defining them before the header is included (each is guarded with
`#ifndef`):
```c
#define IOLINK_PD_IN_MAX_SIZE   32   /* Max input Process Data bytes */
#define IOLINK_PD_OUT_MAX_SIZE  32   /* Max output Process Data bytes */
#define IOLINK_ISDU_BUFFER_SIZE 256  /* ISDU request/response buffer */
#define IOLINK_EVENT_QUEUE_SIZE 4    /* Event FIFO depth */
```

## Thread and ISR Safety

`iolink_device_process()` and `iolink_dll_process()` are not ISR-safe. Call
them periodically from a main loop or task, and serialize all operations that
access the same context. Processing polls the PHY and may invoke application,
ISDU, and storage callbacks. Interrupt handlers should latch events or data for
the task to consume; they must not call the process functions.

The optional device lock callbacks protect the documented public data exchange
helpers. They do not make concurrent calls to `iolink_device_process()` safe.

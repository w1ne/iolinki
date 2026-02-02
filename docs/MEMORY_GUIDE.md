# iolinki Memory Usage Guide

This guide helps embedded developers estimate the RAM and Flash (ROM) requirements for the `iolinki` stack and provides tips for optimization.

## 1. RAM Usage (Static)

The stack avoids dynamic memory allocation (`malloc`). All state is held in context structures.

### Base Requirements

The core memory footprint is determined by `iolink_dll_ctx_t`, which includes all sub-modules (ISDU, Events, Data Storage).

| Component | Size (approx) | Description |
| :--- | :--- | :--- |
| **DLL Context** | ~100 bytes | Core state, timers, counters |
| **PHY API** | 20-30 bytes | Function pointers (usually const/flash, but pointer storage in RAM) |
| **Buffers** | *Configurable* | Dependent on `iolink_config.h` |

### Configurable Memory (via `iolink_config.h`)

You can tune these values to fit your MCU.

| Macro | Default | Bytes Used | Description |
| :--- | :--- | :--- | :--- |
| `IOLINK_ISDU_BUFFER_SIZE` | 256 | **512** bytes | Two buffers: Request (256) + Response (256) |
| `IOLINK_EVENT_QUEUE_SIZE` | 4 | ~12 bytes | Event queue storage (3 bytes per event) |
| `IOLINK_PD_IN_MAX_SIZE` | 32 | 32 bytes | Input Process Data buffer |
| `IOLINK_PD_OUT_MAX_SIZE` | 32 | 32 bytes | Output Process Data buffer |

### Total RAM Calculation

```text
Total RAM ~= Base (150B) + (2 * ISDU_BUF) + (EVENTS * 3) + PD_IN + PD_OUT
```

**Default Configuration:**
~150 + 512 + 12 + 32 + 32 = **~738 bytes**

**Minimal Configuration (Tiny MCU):**
*Settings:* ISDU=64, Events=2, PD=8
~150 + (2*64) + (2*3) + 8 + 8 = **~300 bytes**

## 2. Stack Usage (Call Depth)

The stack is designed to be shallow. The deepest call path typically occurs during ISDU processing or Event triggering.

*   `iolink_process` -> `iolink_dll_process` -> `iolink_isdu_process` -> `handle_standard_commands`
*   **Estimated Max Stack Depth**: ~256 bytes (standard frame)

> **Recommendation**: Allocate at least **512 bytes** for the stack to be safe.

### RTOS Stack Guidelines

When using an RTOS (e.g., FreeRTOS, Zephyr), `iolink_process` typically runs in its own thread.

- **Stack Size**: 1024 bytes recommended (allow overhead for context switching and platform API calls).
- **Critical Sections**: The stack uses `iolink_critical_enter/exit`. These should map to fast IRQ-disable routines to minimize latency.
- **Queue Storage**: Events are stored in `iolink_events_ctx_t` (RAM), not on the task stack.

Code size varies heavily by architecture (ARM Cortex-M vs AVR vs RISC-V) and compiler optimization (`-Os`, `-O3`).

**Estimated Sizes (ARM Cortex-M, -Os):**

| Module | Size (approx) |
| :--- | :--- |
| **Core (DLL/ISDU/Events)** | 4 - 6 KB |
| **CRC Tables** | 256 bytes (can be optimized to calculation-only) |
| **Data Storage** | ~1 KB |
| **Total** | **~5 - 7 KB** |

## 4. Optimization Tips

### Reducing RAM
1.  **Reduce ISDU Buffer**: Use `64` or `32` bytes if you don't need large parameter transfers.
    *   *Define* `IOLINK_ISDU_BUFFER_SIZE` in your build system.
2.  **Shrink Event Queue**: standard IO-Link devices often only need a queue of 1 or 2 events.
3.  **Process Data**: Set `IOLINK_PD_IN_MAX_SIZE` to exactly what your device needs (e.g., 2 bytes).

### Reducing Flash
1.  **Compiler Flags**: Always use `-Os` or `-Oz` (for Clang).
2.  **LTO**: Enable Link Time Optimization (`-flto`).
3.  **Remove Unused Features**: The linker will garbage-collect unused functions if you compile with `-ffunction-sections -fdata-sections` and link with `--gc-sections`.

## 5. Verification

To verify memory usage on GCC-based toolchains:

```bash
size -A libiolinki.a
```

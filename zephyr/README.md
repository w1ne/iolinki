<!--
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later
-->

# iolinki as a Zephyr module

`iolinki` is a Zephyr module that provides a device-side IO-Link stack. Zephyr
currently ships no IO-Link device stack, so this module fills that gap: add it
to your west workspace, enable it in Kconfig, point it at a UART, and build an
IO-Link device for real hardware.

## Adding iolinki to a west workspace

### Option A — add to an existing Zephyr workspace (recommended)

Add a project entry to the `west.yml` of your workspace's manifest repository:

```yaml
# In your west manifest (manifest: projects:)
manifest:
  remotes:
    - name: w1ne
      url-base: https://github.com/w1ne
  projects:
    - name: iolinki
      remote: w1ne
      revision: develop
      path: modules/lib/iolinki
```

Then:

```bash
west update
west build -b native_sim modules/lib/iolinki/samples/iolink_device
```

West discovers the module automatically via `iolinki/zephyr/module.yml`.

### Option B — use iolinki as the manifest repo (standalone)

The repository ships a top-level `west.yml` that pulls in a known-good Zephyr:

```bash
west init -m https://github.com/w1ne/iolinki --mr develop iolinki-ws
cd iolinki-ws
west update
west build -b native_sim iolinki/samples/iolink_device
```

## Kconfig options

Enable the stack with `CONFIG_IOLINKI=y`. The remaining options:

| Kconfig symbol | Maps to (config.h) | Default | Purpose |
| --- | --- | --- | --- |
| `CONFIG_IOLINK_PHY_VIRTUAL` | — | y | Virtual PHY (simulation, `native_sim`) |
| `CONFIG_IOLINK_PHY_UART` | — | n | UART PHY for real hardware |
| `CONFIG_IOLINK_PHY_UART_RX_RING_SIZE` | — | 256 | UART RX ring buffer size (bytes) |
| `CONFIG_IOLINK_DEMO_MASTER` | — | n | Build as a simple test Master |
| `CONFIG_IOLINK_ISDU_BUFFER_SIZE` | `IOLINK_ISDU_BUFFER_SIZE` | 256 | Max ISDU transaction size |
| `CONFIG_IOLINK_EVENT_QUEUE_SIZE` | `IOLINK_EVENT_QUEUE_SIZE` | 4 | Diagnostic event queue depth |
| `CONFIG_IOLINK_PD_IN_MAX_SIZE` | `IOLINK_PD_IN_MAX_SIZE` | 32 | Max input PD (Device→Master) |
| `CONFIG_IOLINK_PD_OUT_MAX_SIZE` | `IOLINK_PD_OUT_MAX_SIZE` | 32 | Max output PD (Master→Device) |
| `CONFIG_IOLINK_M_SEQ_DEFAULT_TYPE_*` | `IOLINK_M_SEQ_DEFAULT_TYPE` | Type 0 | Default M-sequence type for the sample |

The four capacity tunables are pushed to the core's `#ifndef`-guarded
`config.h` macros via `zephyr_compile_definitions()` in `zephyr/CMakeLists.txt`,
so they apply consistently to the library and the application.

The M-sequence choice sets the integer symbol `CONFIG_IOLINK_M_SEQ_DEFAULT_TYPE`
(matching `iolink_m_seq_type_t`); the sample reads it into
`iolink_config_t.m_seq_type`. Applications may still override the value at
runtime when calling `iolink_init()`.

## Devicetree contract for the UART PHY

When `CONFIG_IOLINK_PHY_UART=y`, the PHY resolves its UART from the devicetree:

1. The `zephyr,iolink-uart` **chosen** node (preferred), or
2. an `iolink-uart` **alias** (fallback).

```dts
/ {
    chosen {
        zephyr,iolink-uart = &usart1;
    };
};

&usart1 {
    status = "okay";
    current-speed = <38400>; /* COM2 */
};
```

Call `iolink_phy_uart_init_default()` to bind to that node, or
`iolink_phy_uart_init(dev)` to bind an explicit `const struct device *`.

### PHY porting note

The UART PHY uses Zephyr's interrupt-driven UART API: an ISR drains the RX FIFO
into a ring buffer, `recv_byte()` pops from that buffer (falling back to polled
reads), and `send()` uses `uart_poll_out()`.

A plain UART **cannot** observe or generate the IO-Link wake-up pulse on the C/Q
line, and it does not switch SIO/SDCI line modes — those belong to the IO-Link
transceiver front-end (e.g. MAX14827A, L6362A). Therefore this PHY targets links
where COM mode is already established by such a transceiver, and the optional
`detect_wakeup` / `set_cq_line` / `get_voltage_mv` / `is_short_circuit` vtable
entries are left `NULL`. To support full wake-up and SIO behaviour, implement a
transceiver-specific PHY against the same `iolink_phy_api_t` vtable
(`include/iolinki/phy.h`).

## Building the sample

### Via west (Zephyr SDK installed)

```bash
# Simulation (Virtual PHY)
west build -b native_sim samples/iolink_device

# Real hardware (UART PHY)
west build -b nucleo_l476rg samples/iolink_device -- -DCONFIG_IOLINK_PHY_UART=y
west flash
```

### Via Docker (no host SDK)

```bash
./tools/build_zephyr_docker.sh
```

This boots the official Zephyr build container, initializes a workspace with
iolinki mounted as a module, and builds for `native_sim`. See
`Dockerfile.zephyr`, `Dockerfile.zephyr-base`, and `tools/build_zephyr_docker.sh`.

## Samples and examples

- `samples/iolink_device/` — idiomatic Zephyr sample (this is the entry point).
- `examples/zephyr_app/` — original demo app, also usable as a Nucleo-to-Nucleo
  Master/Device test harness via `CONFIG_IOLINK_DEMO_MASTER`.

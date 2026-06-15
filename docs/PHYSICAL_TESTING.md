# Physical IO-Link Testing on Nucleo Boards

This guide explains how to set up and run the IO-Link stack on two Nucleo L476RG boards using a UART-based physical layer simulation.

## Prerequisites

- 2x **Nucleo L476RG** boards
- 3x Jumper wires (Female-Female or Male-Female depending on headers)
- Micro-USB cables for both boards
- Host computer with Zephyr SDK and `west` installed

## Wiring

Since we are simulating the IO-Link physical layer using UART, we need to cross-connect the TX and RX lines between the two boards.

**Connection Diagram:**

| Board A (Master/Device) | Connection | Board B (Device/Master) |
| :--- | :---: | :--- |
| **GND** (CN7 Pin 20 or CN6 Pin 6) | <--> | **GND** |
| **UART TX** (PA9 / D8) | <--> | **UART RX** (PA10 / D2) |
| **UART RX** (PA10 / D2) | <--> | **UART TX** (PA9 / D8) |

> **Note:** on Nucleo L476RG, `usart1` is typically available on pins D8 (PA9) and D2 (PA10). Double-check your specific board's pinout if communication fails. The default configuration uses the standard Zephyr pin mapping for `usart1`.

## Quick Start (Automated)

I've consolidated the automation scripts into the `examples/zephyr_app/scripts/` directory:

1.  **Fix USB Permissions** (Run once):
    ```bash
    ./examples/zephyr_app/scripts/fix_usb_perms.sh
    newgrp plugdev
    ```

2.  **Build and Flash Both Boards**:
    ```bash
    ./examples/zephyr_app/scripts/setup_and_flash.sh
    ```

3.  **Monitor the Test**:
    ```bash
    python3 examples/zephyr_app/scripts/monitor_nucleos.py
    ```

## Manual Build and Flash

The application is configured to use the UART PHY by default when building for this board (via `prj.conf` and `nucleo_l476rg.overlay`).

1.  **Build for Nucleo L476RG:**

    ```bash
    west build -p always -b nucleo_l476rg examples/zephyr_app
    ```

2.  **Flash Board A:**
    Connect Board A via USB.

    ```bash
    west flash
    ```

3.  **Flash Board B:**
    Connect Board B via USB (and disconnect Board A to avoid confusion, or specify serial number).

    ```bash
    west flash
    ```

## Running the Test

1.  Open a serial terminal for Board A (e.g., `minicom -D /dev/ttyACM0`).
2.  Open a serial terminal for Board B (e.g., `minicom -D /dev/ttyACM1`).
3.  Reset both boards.

### Expected Output

You should see logs indicating the IO-Link stack initialization and the use of the UART PHY:

```text
[00:00:00.000,000] <inf> iolink_demo: Starting IO-Link Zephyr Demo
[00:00:00.000,000] <inf> iolink_phy_uart: Baudrate set to 38400
[00:00:00.000,000] <inf> iolink_demo: Using UART PHY with device: USART_1
```

If the stack logic is running, you will see periodic processing logs or state machine transitions depending on the configured log level and application logic.

## Troubleshooting

- **No output on terminal:** Check if you are connecting to the correct COM port (ST-Link VCP).
- **"UART device not ready" error:** Ensure `usart1` is enabled in the device tree and not conflicting with other peripherals.
- **No communication:** Verify wiring (TX must go to RX). Check if GND is connected common.

## Nucleo-to-Nucleo Test (Standalone)

You can test the IO-Link physical layer using just two Nucleo boards, without a PC master. One board acts as the Device (standard firmware), and the other acts as a simple Master Generator (test firmware).

### 1. Build and Flash the Device (Board A)

This board runs the standard IO-Link Device stack.

```bash
# Build as Device (Default)
west build -p always -b nucleo_l476rg examples/zephyr_app

# Connect Board A and flash
west flash
```

### 2. Build and Flash the Master Generator (Board B)

This board runs a simple loop that sends WakeUp pulses and IDLE frames to test connectivity.

```bash
# Build as Master Generator
west build -p always -b nucleo_l476rg examples/zephyr_app -- -DCONFIG_IOLINK_DEMO_MASTER=y

# Connect Board B and flash
west flash
```

### 3. Verify

Open serial terminals for both boards.

- **Board B (Master)** will print: `Running as DEMO MASTER`, followed by `Master RX: ...` if it receives responses.
- **Board A (Device)** will print: `Starting IO-Link Zephyr Demo`. If wiring is correct, it should eventually synchronize or at least show activity if debug logging is enabled.

> **Note**: This simple Master generator sends dummy 0x55 bytes to simulate WakeUp/IDLE. It does not run a full Master stack, but is sufficient to verify that the physical UART link is working and the Device is receiving data.


## Running with Python Master

You can also test the IO-Link stack on a single Nucleo board by using your PC as the Master via a Python script.

### Prerequisites

- 1x Nucleo L476RG board (flashed with `zephyr_app`)
- Python 3 installed
- `pyserial` library:
    ```bash
    pip install pyserial
    ```

### Wiring

Connect the Nucleo board to your PC via USB. This creates a virtual serial port (e.g., `/dev/ttyACM0` on Linux or `COMx` on Windows).

> **Note:** The `zephyr_app` on the Nucleo is configured to use `usart1` (PA9/PA10) for IO-Link. However, the ST-Link Virtual COM port is usually connected to `usart2` (PA2/PA3).
>
> **To use the USB COM port:** You must either change the overlay to use `usart2` OR use an external USB-TTL adapter connected to PA9/PA10.

### Running the Master

1.  Identify your serial port (e.g., `/dev/ttyACM0`).
2.  Run the master script:

    ```bash
    python3 tools/virtual_master/nucleo_master.py /dev/ttyACM0 --baud 38400
    ```

3.  The script will attempt to wake up the Device and start the communication cycle.


## Process Data Exchange (Cyclic Communication)

The `zephyr_app` is configured to demonstrate cyclic Process Data (PD) exchange:

1.  **Device (Board A)**: Increments a simulated sensor value every 2 seconds and updates the IO-Link stack.
2.  **Master (Board B)**: Sends a valid IO-Link Type 0 frame (Read Direct Parameter, Index 0) every 2 seconds to trigger a response.

### Verification Steps

Run the monitor script:
```bash
python3 examples/zephyr_app/scripts/monitor_nucleos.py --duration 60
```

**Expected Output:**
You will see the Device reporting PD updates and the Master receiving data. Note that VCOM instability may cause some logs to appear as raw hex or fragmented text.

```text
[DEVICE] Device PD Update: 0x01
[DEVICE] RAW: ... 
...
[DEVICE] Device PD Update: 0x02
```

> **Important Note on Stability:** You may observe frequent "Connection lost" messages or fragmented logs from the ST-Link Virtual COM ports. This is often caused by ground loops or USB power fluctuations when two development boards are connected via both USB and the IO-Link UART wires. 
> - **Solution**: Power one board via an external battery or use an isolated USB hub if you need perfectly stable logging. The IO-Link communication itself (UART on pins D8/D2) is robust even if the USB console drops.


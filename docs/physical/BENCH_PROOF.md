# Bench proofs

Two benches close the third-party wire claim. This file is the procedure. `tools/bench_record.py` stores the result. A pass that skips a field is rejected.

## Device on a commercial master

Minimal hardware for the STM32G0B1RE + TIOL112 reference:

- `P-NUCLEO-IOM01M1`, the one-port L6360 master pack. Its firmware is a TEConcept IO-Link v1.1 master.
- `NUCLEO-G0B1RE`, the reference MCU board.
- `TIOX1X2XEVM`, fitted with TIOL1123, logic headers, and a Class A M12.
- A 24 V supply for L+. The master pack does not include one.

Wire L+, L−, and C/Q between the master terminals and the EVM terminals. Jumper the Nucleo UART and GPIO to the EVM logic headers using the EVM user's guide for those header pins. Record `P-NUCLEO-IOM01M1` in `counterpart` and the device commit in `commit`.

Pass: the master reaches OPERATE, reads the device VendorID and DeviceID that the firmware was built with, and completes one process-data cycle. Put the master log excerpt in `master_log`.

Fail the record if the first master frame is the legacy `0x0F` transition or a bare `0x00` probe. Those bytes are not the startup this repository tests.

## Commercial sensor on our master

Connect a commercial IO-Link sensor through an out-of-tree PHY adapter. Record the sensor part number in `counterpart` and the `iolinki-master` commit in `commit`.

Pass: the port reaches OPERATE and `iolink_master_write_gateway_line` prints a line whose VendorID and DeviceID match that sensor. Store that line in `gateway_line`.

A pass against the co-designed iolinki device does not count. `counterpart` must name the commercial part.

## One command per bench

`tools/bench_run.py` builds, flashes, captures and records. It writes one directory per run under `bench-results/` with `capture.log` (the raw serial output), `run.json` (commit, commands, port, SHA-256 of the capture and firmware) and, if the run is recordable, `record.json` from `tools/bench_record.py`.

It records nothing when the serial port produced no output, when the checkout is dirty, or when a pass is not backed by the capture.

Device on a commercial master. Needs `STM32_CMSIS_ROOT` and `CMSIS_CORE_ROOT` as in `examples/stm32g0_tiol112/README.md`, an ST-LINK on the Nucleo, and the master's log on a serial port:

```sh
python3 tools/bench_run.py device-on-commercial-master \
  --master-serial /dev/ttyACM1 --master-baud 115200 \
  --result pass --notes "OPERATE, VendorID/DeviceID read, PD cycle seen"
```

The script builds `examples/stm32g0_tiol112` into the run directory, flashes it with OpenOCD (override with `--flash-cmd`, `{elf}` and `{hex}` are substituted) and captures the master port for `--capture-seconds` (default 60, Ctrl-C ends early). The VendorID and DeviceID come from `examples/reference_device/reference_device.c` (0x04D2, 0x0000162E). A pass is refused unless the captured log shows both IDs in hex. Whether the log shows OPERATE and a process-data cycle is the operator's call, stated in `--notes`. Use `--result fail` for a failed run; the log is still stored.

Commercial sensor on our master. The master firmware and its PHY adapter live outside both repositories, so the build and flash commands are yours. `--master-dir` is the `iolinki-master` checkout that firmware builds against; its commit is recorded:

```sh
python3 tools/bench_run.py commercial-sensor-on-our-master \
  --master-dir ../iolinki-master \
  --build-cmd "make -C ~/bench/master-fw" \
  --flash-cmd "make -C ~/bench/master-fw flash" \
  --console-serial /dev/ttyUSB0 \
  --counterpart "<sensor part number>" --vendor-id <4 hex> --device-id <8 hex> \
  --notes "port 0 OPERATE, PD changes with the target"
```

Take `--vendor-id` and `--device-id` from the sensor's IODD or datasheet, not from the console. The script waits up to `--timeout` seconds (default 120) for an `iolinki-gw/1` line with that identity and records a pass with that line. A line for another identity, or no line, is not a pass; `--record-failure` records it as a fail with the captured log.

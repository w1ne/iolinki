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

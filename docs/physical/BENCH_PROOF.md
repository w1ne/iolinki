# Bench proofs

Two benches close the third-party wire claim. This file is the procedure. `tools/bench_record.py` stores the result. A pass that skips a field is rejected.

## Device on a commercial master

Connect an iolinki device build to a commercial IO-Link master. Record the master model in `counterpart` and the device commit in `commit`.

Pass: the master reaches OPERATE, reads the device VendorID and DeviceID that the firmware was built with, and completes one process-data cycle. Put the master log excerpt in `master_log`.

Fail the record if the first master frame is the legacy `0x0F` transition or a bare `0x00` probe. Those bytes are not the startup this repository tests.

## Commercial sensor on our master

Connect a commercial IO-Link sensor through an out-of-tree PHY adapter. Record the sensor part number in `counterpart` and the `iolinki-master` commit in `commit`.

Pass: the port reaches OPERATE and `iolink_master_write_gateway_line` prints a line whose VendorID and DeviceID match that sensor. Store that line in `gateway_line`.

A pass against the co-designed iolinki device does not count. `counterpart` must name the commercial part.

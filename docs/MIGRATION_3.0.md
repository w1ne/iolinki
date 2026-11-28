# Migrating from v2.2.0 to v3.0.0

v3.0.0 is a major release because it changes the streaming OPERATE request wire
format and the layout of public C structs. Update the Device and its Master
implementation together, then rebuild every consumer against the v3 headers.

## Wire format: update both ends

In v2.2.0, the OPERATE reply layout was corrected. v3.0.0 also corrects
streaming request framing:

- A typed read request is `MC + CKT + PD-out`; it has no OD field.
- A typed write request is `MC + CKT + PD-out + OD`.
- OD widths follow Table A.10: TYPE_1_2 and TYPE_2_V use 2 octets,
  TYPE_1_V uses 8, and the remaining types use 1.
- Type-0 page, diagnosis and ISDU writes keep their Type-0 framing.

A v2.2.0 Master may send an OD field on a typed read, or use the older reply
layout. Either mismatch can shift frame parsing and cause checksum or protocol
errors. Upgrade the Master parser and Device stack as one coordinated change.
The Python virtual master in this repository follows the v3 framing.

## Rebuild and update public structures

Public configuration, PHY, DLL, event, Data Storage and parameter contexts have
new or changed fields. The library does not promise binary compatibility for
these exposed structs. Rebuild the library and all application code together;
do not link objects compiled against different header versions.

Use designated initializers for `iolink_phy_api_t`. The optional
`get_response_timing` callback returns fresh request-end and response-start
timestamps. With no callback or no fresh sample, the stack reports response
timing as unavailable without raising a timing violation; it must not be
interpreted as a successful timing measurement. Invalid fresh samples and
measured out-of-range response delays still raise timing violations when
enforcement is enabled. PHYs can continue communicating without this callback.

## Configure the fixed COM rate

Set `iolink_config_t.stack.transmission_rate` to
`IOLINK_DEVICE_RATE_COM1`, `COM2`, or `COM3` when a specific Device rate is
required. Zero initialization selects COM2. The Device supports one configured
rate; the Master owns rate discovery. Wake-up and error recovery restore the
selected rate. If an application depended on recovery switching to COM1, set
COM1 explicitly. Calling `iolink_dll_set_baudrate()` also updates the selected
rate used for subsequent recovery.

## Review timing behavior

Response timing is now checked as `t_A`, from the end of the Master's request to
the first Device response start bit. A blocking `send()` duration measures the
whole transmitted response and is not a substitute for that boundary. The
`get_response_timing` callback must clear old samples on every send and error;
stale timestamps must never be reported as fresh.

`iolink_dll_set_t_ren_limit_us()` applies to wake-to-receive-enable processing.
It does not configure the response-start (`t_A`) limit. Captured PHY timestamps
are needed for wire timing evidence; software estimates and synthetic test
clocks verify software logic only.

## Recheck memory and application hooks

- `IOLINK_EVENT_QUEUE_SIZE` defaults to 8 instead of 4, and event condition
  tracking defaults to 8 slots. Review per-device RAM budgets.
- `IOLINK_OD_MAX_SIZE` defaults to 8 to support TYPE_1_V.
- Register a reset handler for SystemCommands 0x80, 0x81, or 0x83. Without one,
  those commands now return `FUNC_NOTAVAIL` instead of acknowledging an action
  the application cannot perform.
- `iolink_ds_process()` advances an open session but does not commit or close
  it. Use the matching End, Store, or Break command. Implement and validate NVM
  hooks before claiming persistent parameter storage.

## Validation limits

Host tests, virtual-master conformance, firmware builds and model execution
provide software evidence, not IO-Link certification. This release does not
claim third-party physical-Master interoperability, complete analog
transceiver fault behavior, or IO-Link Safety conformance. IODD schema/CLI
validation is not official IODD Checker approval. Embedded ports without a
validated NVM backend must not claim retained flash parameters.

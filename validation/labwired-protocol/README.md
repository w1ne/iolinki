# Hosted protocol demonstration

This GPL-3.0-or-later fixture compiles the repository's actual C stack into an
Arduino sketch. The live LabWired catalog used for this demonstration provides
the STM32G071, so the hosted run is a **G071 protocol demonstration**. It is not
evidence for the STM32G0B1RE, a particular application BSP, or IAR EWARM.

The fixture injects bytes through a virtual PHY, starts its vectors explicitly
in OPERATE, and checks replies with an independent parity-matrix checksum
implementation. Its clock advances synthetically between vectors. It does not
measure physical UART baud rate, C/Q tA, inter-byte gaps, or sustained throughput.

## Reproduce the host test

From the repository root:

```sh
bash validation/labwired-protocol/run_host.sh
```

Expected final counters are `checks=3078 failures=0 replies=400 selectors=400`,
followed by `PROTOCOL_REGRESSION_PASS`. The same code is a normal CTest target,
`test_labwired_protocol`, and runs in the protocol-rig CI job.

The vectors cover 100 READ and 100 WRITE cycles per COM3 and COM2 configuration,
current-cycle PDout-dependent selection of 32-byte prepared inputs, OD1/code4
advertisement, corrupt-request suppression, and a 1024-byte transfer reconstructed
from five ISDU records. ISDU checks include the 232-byte record limit, a rejected
233-byte response, 16-bit indices, pending-dispatch Busy, COUNT wrap, and repeated
response segments including the final segment. These counts describe executed
assertions and vectors, not a certification suite.

## Package the hosted source

```sh
python3 validation/labwired-protocol/package.py build/labwired-protocol/source_request.json
```

The generated JSON contains `entryPath`, `source`, and a `files` array suitable
for the hosted firmware source request. It embeds the current public headers,
the ten required core C files, and this fixture. Keep the request and resulting
firmware tied to the exact Git commit being demonstrated. The script performs
local packaging only; compiling, running, and creating a lab use the LabWired
tools and their selected board/toolchain configuration.

`main.ino` reports results over the console. PA6 marks request processing, PA7
marks a virtual reply callback, and PA5 goes high after all assertions pass.
Those pins are execution markers, not C/Q timing probes. A transceiver shown
in a lab netlist is not evidence of analog C/Q or physical-master behavior.

The fixture keeps a 256-by-32-byte prepared buffer table so changing selectors
cannot pass by repeating one payload. Products can choose a smaller table and
must define their own cross-record snapshot consistency for segmented ISDU.

Physical timing, reset-to-OPERATE negotiation, DMA behavior, analog transceiver
behavior, and external-master interoperability require their own evidence.

# Spec-conformant wire: checksum, ISDU transport, channels, indices, timing

Status: DESIGN approved 2026-09-18 (user: "yes, wire fix"). Repos: `iolinki` (device),
`labwired-core` (native master model, on-wire harness, pins), `iolinki-master`.
Spec: IO-Link Interface and System V1.1.5 (`docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/`).
Independent cross-reference: lwIOLink (`unref-ptr/lwIOLink`, tested against commercial masters).

## Problem

A review on 2026-09-18 found that the device stack, the master stack and the LabWired native master
model share the same non-spec wire format, so every test (unit, 49-test conformance suite, on-wire CI)
proves self-consistency only. No third-party master or device can talk to either stack:

1. Checksum: `src/crc.c` computes a CRC-6 (poly `0x1D<<2`, seed `0x15`). Spec A.1.6 is XOR of all
   octets with seed `0x52`, compressed 8 to 6 bits by equations (A.1). Example `(MC,CKT)=(0x00,0x00)`:
   spec `0x2D`, current `0x24`.
2. ISDU transport: both stacks interleave invented START/LAST/SEQ control bytes into the OD stream,
   never use FlowCTRL in the MC address (Table 52), never send CHKPDU (A.5.6), and the device treats
   communication channel 3 (ISDU, `MC & 0x60 == 0x60`) as "reserved" (`src/dll.c:550`).
3. Master ISDU lengths: read request Length nibble 0 (reserved, Table A.14); write Length counts data
   only (A.5.3 counts all octets incl. the 6 protocol octets).
4. Events: master reads EventCode via ISDU index 0x0002 (write-only SystemCommand); spec reads the
   event memory over the DIAGNOSIS channel and acks by writing StatusCode (7.3.8, Table 58/59).
5. Standard indices (`include/iolinki/protocol.h`): DetailedDeviceStatus at 0x001C (reserved; spec
   0x0025), 0x0024 mislabelled MinCycleTime (spec DeviceStatus), 0x0025 used for vendor error stats.
6. DeviceID ISDU 0x000B returns 4 octets (spec 3). M-seqCapability OPERATE codes for TYPE_1_1 and
   TYPE_1_V wrong on both sides (Table A.10). Device verifies CKT with its checksum bits unmasked and
   ignores the CKT type bits.
7. Timing: device `T_REN` per baud 5000/1200/230 µs (spec: one value ≤ 500 µs, Table 10);
   `IOLINK_T_DWU_US` = 80 µs is really T_WU (T_DWU is the 30..50 ms wake retry delay, Table 42);
   master wake retries back-to-back; master response timeout defaults to `min_cycle_time` (0 → instant
   timeout); master latches ERROR after 2 retries (7.2.2.1: restart from wake-up); master discards the
   MinCycleTime probe octet under NO_CHECK.

## Wire contract (normative for all three repos)

### C1. Message checksum (A.1.6)

```
ck8 = 0x52; for each octet o of the message: ck8 ^= o    // CKT/CKS included with bits 0-5 = 0
ck6 = (b7^b5^b3^b1)<<5 | (b6^b4^b2^b0)<<4 | (b7^b6)<<3 | (b5^b4)<<2 | (b3^b2)<<1 | (b1^b0)
```
Master message: `MC, CKT(type<<6 | ck6), [OD/PD...]` — the checksum covers MC, CKT with bits 0-5
zeroed, and every data octet. Device reply: `[data...], CKS(event<<7 | pdinvalid<<6 | ck6)` — covers
every data octet and CKS with bits 0-5 zeroed (event and PD-status bits are included).
Shared C helper in `iolinki/include/iolinki/crc.h`: `uint8_t iolink_checksum6(const uint8_t* octets,
size_t len)` (octets already carry a zeroed checksum field). `iolink_crc6` is deleted, not aliased.
Test vectors (all repos, same file content): `(0x00,0x00)→0x2D`, `(0xA2,0x00)→0x00`,
`(0x20,0x99,0x00)→` computed from the formula, plus one lwIOLink vector. Vectors are derived from the
formula in the test file itself, never from the implementation.

### C2. M-sequence control octet (A.1.2, Table A.1/A.2)

`MC = RW<<7 | channel<<5 | address`, channels: 0 Process, 1 Page, 2 Diagnosis, 3 ISDU. The device OD
handler dispatches on the channel (7.3.5.3, Table 51): Page → direct parameter page read/write incl.
MasterCommand at address 0; Diagnosis → event memory (Table 58); ISDU → ISDU handler with FlowCTRL in
the address bits. Reads of unimplemented addresses return 0; writes are ignored (A.1.2).

### C3. ISDU transport (7.3.6, A.5, Table 52)

ISDU octet stream: `I-Service<<4 | Length, [ExtLength], Index[, Index], [Subindex], Data..., CHKPDU`.
Length counts every octet of the ISDU including I-Service/Length and CHKPDU (A.5.3); 2..15 direct,
`Length=1 + ExtLength` for 17..238 total. CHKPDU = XOR of all ISDU octets with CHKPDU as 0 (A.5.6).
I-Service nibbles per Table A.12 (master: write 0x1/0x2/0x3, read 0x9/0xA/0xB by index format per
Table A.15; device: write resp 0x5(+)/0x4(-), read resp 0xD(+)/0xC(-); 0x0 No Service / Busy).
Negative responses carry ErrorType (ErrorCode, AdditionalCode) per Annex C.

Segmentation: the master sends the request through OD write messages on the ISDU channel with
FlowCTRL START (0x10) on the first message, then COUNT 1,2,...,15,0,... (Table 52). Each message
carries as many ISDU octets as the M-sequence OD width (1 for TYPE_0/TYPE_2_x, 2/8/32 per Table A.10).
It then polls with OD read, FlowCTRL START; the device answers `0x01` (Busy, Table A.14) until the
response is ready, then the response octets follow on reads with COUNT. The master ends with FlowCTRL
IDLE (0x11); ABORT (0x1F) aborts. A repeated FlowCTRL means "repeat the previous message"; any other
value is a structure violation (device: ISDUError → abort, Table 54). No control bytes inside the
ISDU stream. Master ISDU time-out per Table 102 (ISDUTime) is a config value, default 5 s.

### C4. Diagnosis channel and events (7.3.8, Table 58)

Event memory: address 0 StatusCode, then per slot n (1..6): `EventQualifier` at `3n-2`, `EventCode`
MSB/LSB at `3n-1`, `3n`. Device sets the Event flag in CKS after writing the memory and freezes it
until the master writes any value to address 0 (T8 in Table 59). Master: on Event flag rising edge,
read StatusCode, read the active slots, deliver, then write StatusCode to confirm.

### C5. Direct parameters and standard indices

- Table B.8 constants: 0x0010 VendorName, 0x0011 VendorText, 0x0012 ProductName, 0x0013 ProductID,
  0x0014 ProductText, 0x0015 SerialNumber, 0x0016 HardwareRevision, 0x0017 FirmwareRevision,
  0x0018 ApplicationSpecificTag, 0x0020 ErrorCount, 0x0024 DeviceStatus, 0x0025 DetailedDeviceStatus,
  0x000C DeviceAccessLocks, 0x0002 SystemCommand (write-only), 0x0003 DataStorageIndex. Vendor
  specific objects move to 0x0040+. 0x001C..0x001F are not used.
- DeviceID (index 0x000B via page 0x09..0x0B) is 3 octets.
- M-seqCapability OPERATE code (bits 1-3) derived from OD width and PD lengths per Table A.10;
  PREOPERATE code (bits 4-5) per Table A.8; ISDU bit 0. Validation on the master accepts the full
  valid set.
- CKT bits 6-7 select the M-sequence type of the incoming message; the device derives the expected
  message length from the CKT type plus its configured PD/OD widths and reports illegal types to the
  DL-mode handler (T11/T12, Table 47).

### C6. Timing constants

Device: `IOLINK_T_REN_US` single value 500 (Table 10); rename `IOLINK_T_DWU_US` to `IOLINK_T_WU_US`
(80, pulse) and add `IOLINK_T_DSIO_MS` (60..300, default 300) for fallback to SIO after a failed
wake; `T_FBD` per Table 43 (3 cycle times, max 500 ms) for MasterCommand FALLBACK.
Master: `T_DMT` 27..37 T_BIT before the first message after wake (default 32); `T_DWU` 30..50 ms
between wake retries (default 40), `n_WU` 2 retries then 3 establish-communication retries
(Figure 35/36); response deadline ≥ 10 T_BIT + frame time (A.3.5/6), never the cycle period; retry
exhaustion returns to STARTUP and re-issues wake-up (7.2.2.1); the MinCycleTime probe octet is stored
and used under every inspection level.

## Work split

### Slice D (iolinki, device)
crc.c/crc.h (C1), frame.c/dll.c (C1 CKT masking, C2 channel dispatch, C5 CKT type bits), isdu.c
rewrite of the transport layer (C3) keeping the existing index/subindex handlers and the application
API `iolink_isdu_*`, events.c Diagnosis channel memory (C4), protocol.h indices (C5), device_info /
DeviceID length, direct-param M-seq codes, config.h + dll.c timing (C6). Python virtual master
(`tools/virtual_master/virtual_master/{crc,protocol}.py` and the ISDU/event helpers) updated to the same
contract so the conformance suite and docker-validation CI exercise it. C unit tests: 23 targets stay
green with rewritten vectors; new `test_isdu_wire.c` drives Table A.13 examples and Figure A.20
byte-for-byte; new `test_checksum_vectors.c`. `check_quality.sh` clean.

### Slice L (labwired-core)
`crates/core/src/peripherals/components/iolink_master.rs` + `iolink_native.rs` (C1, C3, C4 for the
native master model), `third_party/iolinki` submodule bump to the merged device commit, vendored
`third_party/iolinki-master` refreshed from slice M's branch, `examples/iolink-dido` and
`examples/iolink-station` firmware rebuilt, `world_multichip` / `world_station_services` /
`iolink-station-l476` harness green. No engine-path change; `Ir/step` on non-cosim boards unchanged.

### Slice M (iolinki-master)
master_isdu.c rewrite (C3 incl. W3/W4 lengths, CHKPDU, FlowCTRL, ISDUTime), events via Diagnosis
channel (C4), master_parameters.c codes + reserved PD descriptor rejection, master_port.c timing and
recovery (C6), `docs/IMPLEMENTATION_STATUS.md` and the Zephyr sample fake PHY updated to the new wire.
14 ctest targets plus new `test_master_isdu_wire` (byte-exact Table A.13/Figure A.20) and
`test_master_real_iolinki_device` against the slice-D device.

## Merge order (CI couples the repos)

1. Slice D → `iolinki` develop (own CI: docker-validation incl. conformance suite, zephyr, sbom).
2. Slice M branch pushed (cmake-ctest lane green against iolinki@develop; on-wire lane red until 3).
3. Slice L → `labwired-core` main with the submodule at (1) and vendored master from (2).
4. Re-run slice M CI, merge → `master`. Then tag device v2.0.0 and master v1.0.0 (wire-incompatible
   with every prior release; release notes say so).

## Verification (reviewer runs these before any merge)

- Checksum: independent Python oracle (spec formula) vs C helper vs Rust vs the Python virtual master
  on 10 000 random messages: identical.
- ISDU: byte-exact captures of Figure A.20 read/write request and response; segmented 32-octet write
  and 64-octet read over TYPE_0 (1 OD octet) and TYPE_1_2 (2 OD octets); busy polling; ABORT.
- Events: master reads a 2-event memory via Diagnosis channel and the Event flag clears only after the
  StatusCode write.
- On-wire: LabWired station harness master-fw vs device-fw reaches OPERATE, ISDU read of VendorName,
  DS round trip; Ir/step diff on non-cosim boards within noise.
- Timing: device T_REN 500 µs enforced in the virtual master timing test; master T_DWU gating and
  ERROR → STARTUP restart covered by ctest.

## Out of scope (next phases, already agreed)

Real-PHY adapters (wake pulse on a GPIO, SIO thresholds, L+ window), analog testbenches in
labwired-core, KiCad reference boards under `iolinki/hardware/` (user decision 2026-09-18), multi-node
cosim in the World.

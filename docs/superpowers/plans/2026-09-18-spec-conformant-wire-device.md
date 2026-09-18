# Spec-conformant wire, device slice (iolinki) — implementation plan

> **For agentic workers:** execute task by task, TDD, one commit per task. Steps use `- [ ]`.

**Goal:** make the iolinki device stack speak the IO-Link V1.1.5 wire byte-for-byte: spec checksum,
channel dispatch, ISDU transport with FlowCTRL + CHKPDU, Diagnosis-channel event memory, correct
indices and direct-parameter codes, spec timing constants.

**Architecture:** the DLL keeps its state machine; the OD path is split by communication channel
(page / diagnosis / ISDU). The ISDU module keeps its application-facing index handlers and gets a new
transport layer (request assembly from the octet stream, response emission by FlowCTRL). The Python
virtual master mirrors every wire change so the conformance suite tests the new wire.

**Tech stack:** C99, cmocka tests via CMake/ctest, `check_quality.sh` (cppcheck + clang-format v21 +
doxygen zero-warn), Python 3 virtual master (`tools/virtual_master`, pytest).

**Spec:** `docs/superpowers/specs/2026-09-18-spec-conformant-wire-design.md` (read first; sections
C1..C6 are normative). IO-Link spec text extracts: `/tmp/claude-1000/-home-andrii/7d9ab1e8-21ab-4fb5-bb9c-6cc409879441/scratchpad/spec_extract.md`, `.../sec_736.md` (ISDU transport), `.../sec_738.md` (events);
the PDF is in `docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/` (`pdftotext -layout`).

## Global constraints

- No new heap use, static allocation only, `-Werror -Wpedantic -Wconversion -Wshadow` clean.
- Public API names in `include/iolinki/*.h` that applications use (`iolink_device_*`, `iolink_isdu_register*`,
  event/DS APIs) keep their signatures. Wire-level helpers may change.
- Test vectors are computed from the spec formula (Python oracle below), never from the C code.
- Commit messages: conventional, no AI/assistant mention, no trailers.
- Do not touch `tools/virtual_master/nucleo_master.py` (pre-existing ruff F541 is not ours).

## Checksum oracle (use for every vector)

```python
def ck6(octets):            # message checksum, A.1.6 + (A.1); CKT/CKS bits 0-5 already zero
    c = 0x52
    for o in octets: c ^= o
    b = [(c >> i) & 1 for i in range(8)]
    return ((b[7]^b[5]^b[3]^b[1])<<5)|((b[6]^b[4]^b[2]^b[0])<<4)|((b[7]^b[6])<<3)|((b[5]^b[4])<<2)|((b[3]^b[2])<<1)|(b[1]^b[0])
def chkpdu(octets):         # A.5.6, CHKPDU excluded
    c = 0
    for o in octets: c ^= o
    return c
```
Vectors: master `[0x00,0x00]`→CKT `0x2D`; `[0xA2,0x00]`→`0x00`; `[0x20,0x00,0x99]`→`0x06`;
TYPE_1 write `[0x00,0x40,0xA5,0x5A]`→CKT `0x75`; TYPE_2 read `[0x80,0x80]`→`0xAD`.
Device replies (data..., CKS with flag bits): `[0x10]`+flags 0→CKS `0x39`; `[0xA5]`+flags 0→`0x22`;
`[0xA5]`+Event flag→`0x8A`; empty data, flags 0→`0x2D`.
CHKPDU: read req `[0x93,0x10]`→`0x83`; `[0xB5,0x00,0x10,0x00]`→`0xA5`; read resp `[0xD4,0x12,0x34]`→`0xF2`;
write resp(+) `[0x52]`→`0x52`.

---

### Task 1: spec checksum helper

**Files:** modify `include/iolinki/crc.h`, `src/crc.c`; rewrite `tests/test_crc.c`; delete `tests/test_crc_manual.c`
(and its CMake entry); modify `src/frame.c`, `src/dll.c` call sites.
**Produces:** `uint8_t iolink_checksum6(const uint8_t* octets, size_t len);` (octets contain the CKT/CKS
octet with bits 0-5 = 0); `uint8_t iolink_ckt(uint8_t mseq_type, const uint8_t* mc_and_data, ...)` is NOT
needed — callers build the frame with the type bits in place and call `iolink_checksum6` over it, then OR the
result into the checksum octet. `iolink_crc6` and `iolink_checksum_ck` are removed.

- [ ] Write `tests/test_crc.c` with the vectors above (master frames and device replies) asserting
  `iolink_checksum6`; run, expect link failure.
- [ ] Implement `iolink_checksum6` per C1. Run test: pass.
- [ ] Update `src/frame.c` (`iolink_frame_encode_type0*`, type1/2 encoders, `iolink_frame_decode_response`)
  and `src/dll.c` (request verification at lines ~456-459 must zero CKT bits 0-5 before verifying, and reply
  emission at ~143/160/202) to use the helper. Fix every test in `tests/` that hard-codes old CK values by
  recomputing with the oracle (`test_frame.c`, `test_dll.c`, `test_pd*.c`, `test_m_sequence_types.c`,
  `test_integration_full.c`, `test_timing.c`, `test_helpers.c`, `test_app_pd.c`).
- [ ] `cmake -S . -B build && cmake --build build && ctest --test-dir build`: all green. Commit
  `fix(wire): use the IO-Link A.1.6 message checksum (seed 0x52, 8->6 compression)`.

### Task 2: Python virtual master checksum

**Files:** `tools/virtual_master/virtual_master/crc.py`, `tools/virtual_master/tests/test_crc.py`,
`tools/virtual_master/test_automated.py:98-101` (expects 0x1D wake CK — remove, wake is a 0x55 byte).
- [ ] Replace `calculate_crc6` by `checksum6(octets: bytes) -> int` (same formula as the oracle); keep
  `calculate_checksum_type0/type1/verify_checksum` names but reimplement on it. Test file uses the vectors above.
- [ ] `cd tools/virtual_master && python3 -m pytest tests -q`: green. Commit
  `fix(virtual-master): spec message checksum`.

### Task 3: CKT type bits and channel dispatch in the DLL

**Files:** `src/dll.c`, `src/dll_internal.h`, `include/iolinki/protocol.h`, `tests/test_dll.c`,
`tests/test_m_sequence_types.c`.
**Produces:** `IOLINK_MC_CHANNEL_PROCESS 0x00`, `_PAGE 0x20`, `_DIAGNOSIS 0x40`, `_ISDU 0x60`;
`IOLINK_FLOWCTRL_START 0x10`, `_IDLE 0x11`, `_ABORT 0x1F`, `IOLINK_FLOWCTRL_COUNT_MASK 0x0F`;
`static void dll_dispatch_od(iolink_dll_ctx_t*, uint8_t mc, const uint8_t* od_in, uint8_t od_len, uint8_t* od_out)`.
- [ ] Tests: (a) a TYPE_0 request whose CKT type bits say Type 1 while the device is configured for
  TYPE_0 is reported as illegal M-sequence → STARTUP (Table 47 T11/T12); (b) request length is derived
  from CKT type + configured PD/OD widths; (c) MC channel 0x60 with FlowCTRL IDLE in OPERATE answers
  "No Service" (`0x00`) and is not an error; (d) channel 0x40 address 0 read returns StatusCode.
- [ ] Implement: parse `ckt >> 6`, compute expected length, dispatch by `mc & 0x60`: page → existing
  page handler (keep `dll_handle_page_channel_read` and MasterCommand write); diagnosis → Task 5 hook
  (stub returning 0 until Task 5); ISDU → Task 4 hook. Remove the "0x60 reserved" branch (`dll.c:549-552`).
- [ ] ctest green; commit `fix(dll): honour CKT M-sequence type and dispatch OD by communication channel`.

### Task 4: ISDU transport rewrite (C3)

**Files:** `src/isdu.c` (transport part: `iolink_isdu_collect_byte`, `iolink_isdu_get_response_byte`,
control-byte code, `ISDU_STATE_*`), `include/iolinki/isdu.h` (new transport API), `src/dll.c` (call the
new API from the ISDU channel branch), delete `tests/test_isdu_flow_control.c` and
`tests/test_isdu_segmented.c` semantics that encode control bytes, add `tests/test_isdu_wire.c`.
**Produces:**
```c
/* Master -> device: one OD message on the ISDU channel. flowctrl = mc & 0x1F. */
void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, const uint8_t* od, uint8_t od_len);
/* Device -> master: fill od_out (od_len octets) for a read on the ISDU channel. */
void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* od_out, uint8_t od_len);
```
Behaviour (Table 52/54): START on write resets the request buffer; then COUNT 1,2,..,15,0,1,... on
each following write. A FlowCTRL equal to the previous one means "repeat the previous message": ignore
its payload. Any other value is an ISDUError: drop the request, go to Idle, answer the next read with
No Service `0x00` (Table 54 T13). The request is complete when the received octet count equals Length
(or ExtLength). Then verify XOR of all octets == 0 (A.5.6); on mismatch answer a negative response with
ErrorType APP_DEV `0x80 0x00` (Table C.1, C.2.2). Unknown index → `0x80 0x11` IDX_NOTAVAIL; unknown
subindex → `0x80 0x12`; write to read-only → `0x80 0x23` IDX_NOT_ACCESSIBLE; length mismatch →
`0x80 0x33`/`0x80 0x34`. Reads with START while the application has not answered yet → one octet
`0x01` (Busy, Table A.14); once ready the response octets follow across reads with COUNT; IDLE returns
to Idle; ABORT (read or write) returns to Idle and discards everything. StatusCode format for Task 5:
type 2 (Figure A.22: bit 7 Event Details = 1, bits 0-5 one bit per active slot 1..6).
Response format: `0xD<<4 | len`, data, CHKPDU; negative `0xC4, ErrorCode, AdditionalCode, CHKPDU`;
write `0x52, CHKPDU=0x52` / `0x44, ErrorCode, AdditionalCode, CHKPDU`. ExtLength for total > 15.
Index formats: accept I-Service 0x1/0x2/0x3 and 0x9/0xA/0xB (Table A.13); 16-bit index for 0x3/0xB.
- [ ] `tests/test_isdu_wire.c`: byte-exact Figure A.20 (read 8-bit index `93 10 83` → `D4 12 34 F2` for
  a registered 2-octet object at index 0x10; write `26 10 01 12 34 xx` → `52 52`), 16-bit read of
  VendorName 0x0010 over od_len 1 with COUNT wrap past 15, Busy polling, ABORT mid-request, CHKPDU
  corruption → negative response, repeated FlowCTRL → same octet repeated, ExtLength read of a
  64-octet object.
- [ ] Implement; ctest green (existing `test_isdu.c` application-level tests must still pass);
  `./check_quality.sh` clean. Commit `fix(isdu): spec ISDU transport with FlowCTRL and CHKPDU`.

### Task 5: Diagnosis channel event memory (C4)

**Files:** `src/events.c`, `include/iolinki/events.h`, `src/dll.c` (diagnosis branch), `tests/test_events.c`.
**Produces:** `uint8_t iolink_events_memory_read(iolink_events_ctx_t*, uint8_t addr);`
`void iolink_events_memory_write(iolink_events_ctx_t*, uint8_t addr, uint8_t value);` and
`bool iolink_events_flag(const iolink_events_ctx_t*)` used for CKS bit 7.
- [ ] Tests: trigger two events; addr 0 StatusCode has 2 active slots (bits per Table 58 StatusCode
  format in 7.3.8.2 — read the extract for the bit layout and cite it); addr 1..6 hold qualifier/code
  MSB/LSB; memory is frozen while the flag is set (a third event is queued, not visible); writing addr 0
  clears the flag and releases the memory; the CKS Event bit follows the flag.
- [ ] Implement; ctest green; commit `fix(events): expose the Table 58 event memory on the diagnosis channel`.

### Task 6: indices, DeviceID, M-seq codes

**Files:** `include/iolinki/protocol.h`, `src/isdu.c` (index handlers, `direct_param_mseq_capability`),
`src/device_info.c`, `tests/test_isdu.c`, `tests/test_config_verification.c`.
- [ ] Tests: `IOLINK_IDX_DETAILED_DEVICE_STATUS == 0x0025`, `IOLINK_IDX_DEVICE_STATUS == 0x0024`,
  `IOLINK_IDX_ERROR_COUNT == 0x0020`; reading 0x001C returns negative response (not implemented);
  vendor error stats at `0x0040`; DeviceID read returns 3 octets; direct-parameter page octet 3 for
  TYPE_2_x with PD in 1..8 bit = `0x01` (ISDU + code 0), for TYPE_1_2 no PD = `0x03`, for TYPE_1_V 8 OD
  octets = `0x0D` (code 6); PREOPERATE bits 4-5 = 0 for TYPE_0.
- [ ] Implement; ctest green; commit `fix(isdu): spec index map, 3-octet DeviceID, Table A.10 M-sequence codes`.

### Task 7: timing constants (C6)

**Files:** `include/iolinki/config.h`, `src/dll.c`, `tests/test_timing.c`, `tests/test_sio_fallback.c`.
- [ ] Tests: `IOLINK_T_REN_US == 500` and `dll_get_t_ren_limit_us` returns 500 for every baud unless
  overridden; `IOLINK_T_WU_US == 80`; `IOLINK_T_DSIO_MS` default 300 and a device that saw a wake-up
  but no valid message within T_DSIO returns to SIO (Table 47 T10); a FALLBACK MasterCommand switches
  to SIO after `T_FBD` = 3 × MasterCycleTime bounded by 500 ms (Table 43).
- [ ] Implement (keep `iolink_dll_set_t_ren_limit_us` override); ctest green; commit
  `fix(dll): spec T_REN, T_WU, T_DSIO and T_FBD timing`.

### Task 8: Python virtual master protocol + conformance suite

**Files:** `tools/virtual_master/virtual_master/protocol.py` (remove `ISDUControlByte`, add FlowCTRL ISDU
read/write generators and CHKPDU, event memory reads on channel 0x40), `master.py`,
`tools/virtual_master/test_conformance_*.py`, `tools/virtual_master/SERVICES.md`, `docs/CONFORMANCE.md`,
`docs/FRAMES.md`.
- [ ] Update generators to C1/C3/C4; the 49 conformance tests must pass against the rebuilt device
  (`tools/virtual_master/run_conformance_all_types.sh` or the docker path in `docs/TESTING.md`).
- [ ] Update FRAMES.md / CONFORMANCE.md to describe the spec wire and delete the old control-byte text.
  Commit `test(virtual-master): drive the spec wire in the conformance suite`.

### Task 9: ledger + CHANGELOG

- [ ] `CHANGELOG.md` "Unreleased: BREAKING wire change" listing C1..C6; `docs/IMPLEMENTATION_STATUS.md`
  or equivalent claims doc updated. Run `./check_quality.sh` and full ctest one last time. Commit
  `docs: record the spec-conformant wire`.

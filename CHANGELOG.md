# Changelog

All notable changes to the `iolinki` project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [3.0.0] - 2026-11-28

### Migration from 2.2.0
- **Update Device and Master together.** This release changes OPERATE request
  framing: typed reads omit OD, while writes carry OD after PD-out. A mixed
  2.2.0/3.0.0 pair can parse a different frame length and reject the request.
  See [Migration to 3.0.0](docs/MIGRATION_3.0.md) and the wire figures there.
- **Rebuild every consumer.** Public context, configuration, statistics and PHY
  structs have changed layout. Rebuild the library and application together;
  do not mix objects or binaries built against different headers. PHYs using
  timing enforcement should implement `get_response_timing` for evaluated
  response-start checks; designated initializers should be used for PHY tables.
- **Review COM configuration and timing.** The zero-valued transmission-rate
  setting selects COM2. The Device keeps its configured rate through recovery;
  the Master performs rate discovery. `iolink_dll_set_t_ren_limit_us()` now
  limits wake-to-receive-enable processing, while response start is checked as
  `t_A` from fresh PHY timing boundaries.
- **Recheck memory budgets.** The default Event queue grows from 4 to 8,
  condition tracking uses 8 slots, and the maximum OD buffer grows to 8 bytes.
- **Do not treat simulator evidence as certification.** Host, virtual-master,
  firmware-build and model checks do not establish physical-master
  interoperability, analog transceiver fault behavior, or IO-Link Safety
  conformance. Timing marked estimated is not wire qualification.

### Fixed
- Missing PHY response timing samples are counted as unavailable without
  emitting a timing violation event. Invalid fresh samples and measured `t_A`
  violations remain enforced; the G0 rig no longer applies a stale 20 ms
  override to the wake receive-enable budget.

### Added
- Configurable fixed Device transmission rate in `iolink_config_t` for COM1,
  COM2, or COM3. Zero-initialized configurations retain the COM2 default, and
  startup, wake-up and recovery restore the selected rate.
- OD1 configuration documentation for Type 2_V, including its three-octet
  Process Data requirement and code-4 selection.
- Explicit ISR-safety guidance for `iolink_device_process()` and
  `iolink_dll_process()`.
- Event Dispatcher lifecycle (V1.1.5 10.10.2): `iolink_event_appear()`,
  `iolink_event_disappear()` (explicit clear, deferred until 50 ms after
  "appears"), single-shot `iolink_event_notify()`, `iolink_events_process()`
  (called by `iolink_dll_process()`), a 1 s repeat interval per EventCode and
  EventQualifier, and a saturating `iolink_events_dropped()` counter. `_at`
  variants take an explicit wrap-safe millisecond time.
- `iolink_events_startup_reset()`: the DLL discards queued Events and the
  Event flag on entry to STARTUP; active conditions are reported again.
- DataStorageIndex 0x0003 Subindex 1-5 (Table B.10): DS_Command,
  State_Property, Data_Storage_Size, Parameter_Checksum and Index_List.
  Subindex 0 keeps the device-specific serialized image.
- Block Parameterization staging: in a download session (ParamDownloadStart
  or DS_DownloadStart) tag writes and 0x0003 image writes are checked and
  staged; End/Store commits them with one checked NVM write.
  New `iolink_ds_stage_param()`, `iolink_ds_handle_ds_command()`,
  `iolink_ds_download_open()`, `iolink_ds_upload_open()`,
  `iolink_ds_state_property()`, `iolink_ds_storage_size()`,
  `iolink_ds_index_list()`.
- MasterCommand 0x98 (PDOUTVALID) / 0x99 (PDOUTINVALID) handling in OPERATE
  and last valid Process Data snapshots: `iolink_dll_publish_pd_in()`,
  `iolink_dll_get_last_valid_pd_in()`, `iolink_dll_get_last_valid_pd_out()`.
- Back-to-box communication stop: `iolink_dll_disable_communication()`.
- `iolink_events_clear_status()` for Restore factory settings (Table 101).
- `iolink_params_ctx_reset_access_locks()`.
- Reset types `IOLINK_RESET_RESTORE_FACTORY` and `IOLINK_RESET_BACK_TO_BOX`;
  ISDU reset service hook `iolink_isdu_ctx_t.system_reset`.
- Device-level integration test `tests/test_integration_ds_reset.c`.

### Changed
- **Behavior change:** Communication fallback resets the protocol session and
  discards queued RX data. PHYs with wake-up detection still return to SIO;
  UART-only PHYs remain in SDCI and accept a fresh startup sequence.
- **Behavior change:** `iolink_dll_set_baudrate()` now selects the fixed Device
  rate as well as updating the live PHY. The selected rate therefore survives
  wake-up and communication recovery for existing callers of this API.
- **Behavior change:** Recovery now restores the configured Device rate. A
  zero-initialized configuration therefore recovers at COM2 instead of the
  previous COM1 fallback; select `IOLINK_DEVICE_RATE_COM1` to retain COM1.
- Response timing now checks the request-end to response-start interval (`t_A`)
  using fresh PHY timestamps. A blocking UART send duration is not a substitute
  for the first response start bit. Wake-to-receive-enable processing is tracked
  separately as `t_REN`; software estimates are not physical timing evidence.
- **Behavior change:** SystemCommands 0x80, 0x81 and 0x83 answer 0x8035
  FUNC_NOTAVAIL when no reset handler is registered, instead of a positive
  response without action. 0x81 runs the handler before the response; 0x80
  and 0x83 run after the response was read and 3 more M-sequences
  (10.7.2, 10.7.5).
- **Behavior change:** `iolink_ds_process()` no longer commits or closes a
  session; it only moves REQ to the active state. The DLL now calls it every
  cycle. A communication restart (STARTUP) breaks an open session.
- **Behavior change:** ProcessDataOutput 0x0029 returns the last PDout
  received after MasterCommand 0x98, not the current buffer in OPERATE.
  ProcessDataInput 0x0028 returns the last PDin published as valid.
- The Data Storage access lock refuses writes to 0x0003 (0x8023) and no
  longer blocks Block Parameterization (B.2.4). The Parameter (write) lock
  now refuses single parameter writes except inside a DS download.
- DeviceAccessLocks 0x000C over ISDU are device-local and non-volatile;
  reserved bits (0x8030), wrong length (0x8033/0x8034) and NVM failure
  (0x8000) are refused without change.
- Python virtual master: `last_isdu_error`, `read_vendor_id()`,
  `read_device_id()`. Conformance scripts use Table B.9 values and read
  VendorID/DeviceID from Direct Parameter page 1.

### Validation scope
- The release workflow records host test results, firmware builds, and model
  evidence for the exact source commit. Synthetic timing tests verify software
  boundary handling only. A PHY must provide fresh captured timestamps before
  reporting wire-qualified `t_A` behavior.
- No third-party physical Master report or IO-Link certification is included.
  Analog transceiver fault behavior, retained embedded flash parameters, and
  official IODD Checker approval remain separate validation work.

### Fixed
- `IOLINK_ISDU_ERROR_PARAM_INCONSISTENT` is 0x41 (PAR_SETINCONSIST, C.2.17);
  it was 0x40 (PAR_SETINVALID, now `IOLINK_ISDU_ERROR_PARAM_SET_INVALID`).
- A rejected write in a download session makes End/Store fail with 0x8041
  and keeps the previous set (Table 98 rules 4 and 7); previously End
  reported success.
- ParamDownloadStore passes 0x05 to the DS engine (backend store and
  DS_UPLOAD_FLAG) and raises DS_UPLOAD_REQ as a single-shot Notification only
  after a successful commit. A failed commit answers 0x8041 (invalid set) or
  0x8000 (NVM failure) instead of 0x8035.
- Restore factory settings uses the checked reset: an NVM failure answers
  0x8000 and changes nothing; success also clears Events, DeviceStatus,
  DetailedDeviceStatus, ErrorCount and the DS upload request.
- A tag write rejected by NVM answers 0x8000 (APP_DEV), not 0x8011.
- Event memory confirmation removes exactly the Events shown in the frozen
  memory; Events that arrive while frozen are kept. The memory lists Errors
  before Warnings. A full queue no longer evicts presented entries.
- DeviceStatus (0x0024) follows Table B.13: Error reports 4 (Failure), not 3.
  `IOLINK_DEVICE_STATUS_FAILURE` is now 4 and
  `IOLINK_DEVICE_STATUS_FUNCTIONAL_CHECK` (3) was added.
- DetailedDeviceStatus (0x0025) lists active "appears" Warnings/Errors only;
  cleared conditions and Notifications are not listed.
- Default `IOLINK_EVENT_QUEUE_SIZE` raised from 4 to 8 so the six event memory
  slots can be filled.
- **BREAKING wire change: streaming M-sequence request framing.** Typed reads
  carry `MC + CKT + PD-out` without OD; typed writes carry OD after PD-out.
  Type-0 page, diagnosis and ISDU writes retain their 3-octet request. OD
  widths follow Table A.10: TYPE_1_2 and TYPE_2_V use 2 octets, TYPE_1_V uses
  8, and the remaining types use 1. `IOLINK_OD_MAX_SIZE` is 8. Update the
  Master with the Device; the repository's `iolinki-master` follows this layout.

## [2.2.0] - 2026-10-17

### Changed
- **OPERATE M-sequence reply layout corrected (wire change).** The
  reply to a READ M-sequence is `[OD][PD-in] CKS` and the reply to a WRITE
  M-sequence is `[PD-in] CKS` with no OD octet, matching Figures
  A.9/A.10/A.13/A.14. `iolink_frame_decode_operate_response`, the Python
  virtual master and the reference example follow the same layout. Device and
  `iolinki-master` 1.1.0 must be upgraded together for the multi-octet OPERATE
  M-sequence types, because a mixed old/new pairing frames replies with the
  wrong layout and produces checksum errors.
- **API change:** the mislabelled `IOLINK_EVENT_PHY_VOLTAGE_FAULT` (0x1805) and
  `IOLINK_EVENT_PHY_SHORT_CIRCUIT` (0x1806) macros are removed. Those values are
  Table D.2 Master/Port codes ("Overtemperature", "Short circuit at L+"), not
  Device codes. `dll_poll_diagnostics()` now reports the standardized Table D.1
  Device codes `IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN` (0x5111) for a reading
  below 18 V, `IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN` (0x5110) for a reading
  above 30 V, and `IOLINK_EVENTCODE_SHORT_CIRCUIT` (0x7710). Callers that
  referenced the removed macros must switch to the `IOLINK_EVENTCODE_*` names.
- The public GPLv3 grant is no longer limited: the evaluation, non-commercial
  and 90-day conditions are dropped from `LICENSE`. The commercial license
  remains the alternative for proprietary products. The 12-month support
  period is removed from the public evidence bundle and security documents.
- Registering a vendor handler takes ownership of the Preferred Index range
  `0x0040`-`0x00FE`: requests to `0x0040` go to the handler, shadowing the
  built-in error-statistics object while it is registered.

### Added
- `tools/bench_run.py`, a one-command physical bench runner: it builds and
  flashes the firmware, captures the master's serial output, stores the raw
  capture with its SHA-256 and records the result; it records nothing for an
  empty capture or a dirty checkout.
- `tools/evidence_bundle.py`, a per-firmware-hash evidence bundle with the gap
  list the caller cannot drop.

### Fixed
- EventQualifier INSTANCE now encodes `0x04` (Application, Table A.17) instead of
  the reserved `0x02` in both the Table 58 event memory and the
  DetailedDeviceStatus (Index 0x0025) builder; an error-class device event is
  qualifier `0xF4`.
- `dll_poll_diagnostics()` no longer treats a negative `get_voltage_mv()` as an
  under/over-voltage fault: a negative reading means "measurement unavailable"
  (the generic PHY template returns `-1`), so generic-PHY devices no longer raise
  a voltage event on every poll.
- Vendor-specific ISDU indices in the Preferred Index range `0x0040`-`0x00FE`
  are now routed to the application vendor service (B.2.27), alongside the
  Extended Index range from `0x0100`; unsupported vendor indices keep answering
  `0x11` (Service not available). The built-in error-statistics object at
  `0x0040` remains the fallback when no vendor handler is registered.
- The bench record tooling accepts the gateway line the master prints and
  rejects a pass whose line names a different vendor or device.

### Validation scope
- Host suites pass: 36/36 CMocka tests, the Python virtual-master suites, and
  the corrected wire layout is exercised against `iolinki-master` in the
  cross-repo test. Physical-master re-validation of the corrected reply layout
  remains separate pending work.

## [2.1.1] - 2026-10-02

### Fixed
- Unsupported SystemCommand values now return FunctionNotAvailable (`0x8035`)
  as required by IO-Link Interface & System Specification V1.1.5, section C.2.14.
  Unknown ISDU indices continue to return IndexNotAvailable (`0x8011`).
- Released counter and switching-sensor IODDs now include required standard
  variable references and ISDU test configurations; the teach singleton uses
  SingleValue. Release ZIPs use canonical IODD filenames. Regenerated examples
  pass current official XSD validation and genuine IODD Checker 1.1.4 business rules.

## [2.1.0] - 2026-10-02

### Added
- Bare-metal STM32G0B1RE/TIOL112 GCC and native IAR project files, native
  ESP-IDF ESP32-C3/L6362A and Zephyr Nucleo-U575ZI-Q/TIOL112 references.
- Counter/button/LED and switching-sensor examples with matching schema/CLI-validated IODD
  maps. The sensor demonstrates threshold/hysteresis/inversion, wire ISDU teach
  and readback, PD publication and a vendor notification.
- Optional per-device vendor ISDU service for indices >= 0x0100, with bounded
  responses and independent application state. Mandatory indices stay reserved.
- Reproducible LabWired actual-firmware startup/interrupt recipes and exact pins.

### Changed
- New commercial quotes distinguish Indie person-held and Company company-held
  product-family rights; accepted earlier agreements retain their terms.
- Release gating requires passing, non-skipped JUnit tests. Source, exact-tag
  firmware, test evidence and SBOM assets are checksummed; RC tags are prereleases.

### Validation scope
- Native IAR project compilation, embedded flash persistence, complete analog
  transceiver Twins and physical-master validation are separate pending work.
- IODD maps pass schema/CLI checks; official IODD Checker approval is separate. The switching
  example does not claim standardized Smart Sensor Profile conformance.

## [2.0.0] - 2026-09-25

### Changed
- **BREAKING wire change: spec-conformant IO-Link V1.1.5 M-sequences.** The
  device stack now speaks the wire defined by the IO-Link Interface &
  System specification V1.1.5:
  - **C1 Checksum (A.1.6)**: the message checksum is the XOR of every octet
    seeded with `0x52`, compressed 8->6 bits by (A.1). The old CRC-6
    (poly `0x1D`, seed `0x15`) and the trailing checksum octet are gone.
  - **Reply layout (A.1.5)**: replies are `[PD-in][OD] CKS` with no leading
    status octet and no PD toggle bit; the Event flag is CKS bit 7 and the
    PD-invalid flag is CKS bit 6. A Type-0 read replies `OD CKS`; a Type-0
    write replies `CKS` only.
  - **C2 Channel dispatch**: OD is dispatched by the MC communication channel
    (process / page / diagnosis / ISDU); MasterCommands are page-channel writes
    to address 0.
  - **C3 ISDU transport**: requests and responses are carried by OD messages on
    the ISDU channel with FlowCTRL in the MC address bits and CHKPDU (A.5.6);
    the invented interleaved START/LAST/SEQ control bytes are removed.
  - **C4 Diagnosis channel**: the event memory is exposed per Table 58 and the
    Event flag is acknowledged by writing StatusCode at address 0.
  - **C5 Indices**: standard indices corrected (`0x0024` DeviceStatus,
    `0x0025` DetailedDeviceStatus, `0x0020` ErrorCount, vendor objects at
    `0x0040+`), 3-octet DeviceID and Table A.10 M-sequence capability codes.
  - **C6 Timing**: a single device `T_REN` of 500 us (Table 10), `T_WU` pulse
    (80 us), `T_DSIO` (300 ms) fallback to SIO, and `T_FBD` (3 MasterCycleTimes,
    max 500 ms) for the FALLBACK MasterCommand.
- The Python virtual master and the 49-test conformance suite were updated to
  the same wire contract.

### Fixed
- **Table A.10 OD width**: TYPE_2_1 and TYPE_2_2 carry one On-request Data octet.
  TYPE_2_V stays at two octets.
- **Type-0 OD write**: a write is three octets in PREOPERATE and in OPERATE,
  independent of the OPERATE OD width.
- **ISDU errors**: a read of write-only SystemCommand (index 0x0002) returns
  ErrorType `0x80 0x23` (IDX_NOT_ACCESSIBLE, Table C.1). Access denied uses the
  same ErrorType.

### Verified
- CI on `develop` is green, including the virtual-master conformance suite.
- This tag is not an official IO-Link conformance certificate. It has not been
  run against a third-party master.

## [1.2.0] - 2026-07-02
### Added
- **Per-release SBOMs**: every tagged release now ships CycloneDX 1.6 and SPDX 2.3
  SBOMs (`iolinki-<version>.cdx.json` / `.spdx.json`) generated by
  `tools/generate_sbom.py` and attached as release assets.
- **Security documentation package**: CRA-ready coordinated-disclosure policy
  (`SECURITY.md`), STRIDE threat model aligned to IO-Link Security Design and
  Development Guideline 10.512 (`docs/security/THREAT_MODEL.md`), and a Cyber
  Resilience Act overview for device makers (`docs/security/CRA.md`).

### Fixed
- **Zephyr module build**: `zephyr/CMakeLists.txt` still referenced
  `src/iolink_core.c`, deleted by the singleton-API removal; the module now
  builds against `src/device.c`. The breakage was invisible because
  `zephyr-validation` was not a required check.

### Changed
- **CI gates are now real**: branch protection on `develop` and `main` requires
  `docker-validation`, `zephyr-validation`, and `sbom-tools` directly; the
  echo-only proxy jobs (`quality-gate`, `build-and-test`, `build-bare-metal`,
  `build-example`) are removed.

## [1.1.2] - 2026-06-15
### Fixed
- **Device communication over the UART PHY**: the DLL booted in SIO mode and only left it upon electrical wake-up detection. A plain UART PHY cannot observe the C/Q wake-up pulse (`detect_wakeup == NULL`), so a device on real UART hardware stayed in SIO forever and never serviced M-sequences. The DLL now starts in SDCI when the PHY cannot detect wake-up (the already-established-COM contract of the UART PHY), so the device responds to the Master immediately.
- **Real-hardware build out of the box**: the Zephyr module defaulted to the virtual PHY, which is implemented with POSIX `termios` and therefore failed to compile for any real board (ARM/RISC-V). The PHY choice now tracks the target — virtual on `native_sim`/POSIX, UART on real hardware — and the virtual PHY is no longer offered (cannot be mis-selected) off POSIX.

### Tools
- `nucleo_master.py` gains `--no-wakeup` (drive an already-established-COM UART link without the `0x55` wake-up artifact, which would otherwise desynchronise framing) and `--hold-lines` (hold DTR/RTS low for boards whose USB-serial resets on control-line changes, e.g. the ESP32-C3 native USB-Serial-JTAG).

### Validated
- End-to-end IO-Link Master↔Device exchange on an ESP32-C3 (RISC-V) over the native USB-Serial-JTAG: PHY `send`/`recv_byte` round-trip, and a full startup → PREOPERATE → OPERATE sequence reading the device's live Process Data with valid checksums.

## [1.1.1] - 2026-06-15
### Fixed
- **Zephyr UART PHY build**: `uart_irq_update()` returns `void` in current Zephyr, but the PHY ISR used its result in a boolean (`if (!uart_irq_update(dev))`), so the UART PHY failed to compile when targeting real hardware. (`native_sim` builds use the virtual PHY, so this path was never exercised in CI.) Call it as a statement per the Zephyr API. Validated on a Nucleo-F103RB (Cortex-M3): builds (FLASH 25% / RAM 54% of 20 KB), flashes, boots, and runs on silicon.

### Added
- Board overlay for `nucleo_f103rb` (IO-Link UART on USART1).

## [1.1.0] - 2026-06-15
### Added
- **Data Storage parameter server** (ISDU index 0x0003): real serialization and restore of device parameters, with Access-Lock integration and checksum-mismatch recovery.
- **Direct Parameter pages**: page 1 read (index 0x0000) and page 2 read/write (index 0x0001).
- **Standard EventCodes**: V1.1.5 Annex D event-code constants and `iolink_event_classify()`.
- **Variable Process Data negotiation**: runtime PD length changes for Type 1_V/2_V via `iolink_set_pd_length()`.
- **Application callbacks API** (`iolink_app_register`): lifecycle (startup/preoperate/operate) and process-data callbacks.
- **Application reset handler** (`iolink_set_reset_handler`) for System Commands 0x80/0x81.
- **Zephyr module**: west-installable manifest, Kconfig-driven configuration, devicetree-bound UART PHY, and a `native_sim` sample.

### Fixed
- EventQualifier encoding (index 0x1C) corrected to the V1.1.5 layout.
- PD Input Descriptor (index 0x1D) reports the actual configured PD length.
- DLL resets the inactivity window on wake-up, making re-establishment after an idle period reliable.
- FALLBACK state is now observable; stray ISDU debug output removed; `timeout_errors` counter wired through.
- CI pipeline restored to green (strict compile, clang-format, Doxygen, and Zephyr build stage).

### Changed
- Documentation reconciled with the implementation (conformance test count, architecture, API reference).
- Clarified dual-license terms: GPLv3 is free for open-source use (including production) under copyleft; a commercial license is required only for closed-source/proprietary products. Replaces the prior, unenforceable "90-day evaluation" framing.

## [1.0.0] - 2026-02-06
### Added
- **Final V1.0.0 Stability**: Completed all mandatory ISDU index implementations and protocol state machine hardening.
- **Interleaved Mode Support**: Full compliance with IO-Link V1.1.5 interleaved Control/Data format for all acyclic transfers.

### Fixed
- **ISDU Protocol Alignment**: Corrected service ID mapping (8 for Read, 9 for Write) and standardized state transitions across all handlers.
- **Timer and Jitter Resilience**: Optimized cycle timing and timeout handling for robust performance in virtualized environments.
- **Unit Test Coverage**: Achieved 100% pass rate on 21 core unit tests and synchronized all test helpers with the protocol spec.

## [1.0.0-rc5] - 2026-02-06
### Fixed
- **ISDU Service Protocol**: Fixed critical bug in `HEADER_INITIAL` where Service Codes `0x9` (Read) and `0xA` (Write) were compared against internal Enum values `0/1` instead of protocol literals, causing all requests to fail or misbehave.
- **Unit Test Expectation**: Corrected `test_dll.c` expectation for Preoperate-to-Operate transition to 4 bytes, matching Type 1 environment defaults.

## [1.0.0-rc4] - 2026-02-06
### Fixed
- **Test Stability**: Fixed `test_timing.c` by explicitly configuring Type 0 mode to match test frames, avoiding default Type 1 conflict in CI.
- **ISDU Compliance**: Updated `test_isdu_segmented.c` to use standard sequence number logic (starting at 0), resolving potential protocol mismatches.

## [1.0.0-rc3] - 2026-02-06
### Fixed
- **Build System**: Fixed invalid CMake version string (removed `-rc` suffix from `project()` call) to resolve configuration error.

## [1.0.0-rc2] - 2026-02-06
### Fixed
- **ISDU Helpers**: Fixed `test_helpers.c` to correctly handle `Start/Last` bits in V1.1.5 Interleaved Mode (preventing `BUSY` errors).
- **Unit Tests**: Updated `test_dll.c` expectations to match Type 1 (4-byte) default configuration in CI.

## [1.0.0-rc1] - 2026-02-06

### Added
- **Quality Gate**: Integrated Docker-based validation strategy for consistent testing.
- **Pre-commit Hooks**: Enforced automated unit test execution before commits to prevent regressions.
- **ISDU Helpers**: Added test helper functions (`isdu_send_read_request`, etc.) for V1.1.5 interleaved format validation.

### Fixed
- **ISDU Protocol Compliance**: Updated `src/isdu.c` to enforcing V1.1.5 interleaved Control/Data format for **all** transfers (previously only segmented).
- **Type 0 Framing**: Corrected DLL implementation and tests to properly handle 2-byte idle frames (MC+CK).
- **Error Recovery**: Fixed inactivity timeout (200ms) in `src/dll.c` for robust communication recovery.
- **Stability**: Resolved PD toggle timing issues and ISDU stress test failures.

## [0.12.13] - 2026-02-05

### Fixed
- **CI/CD**: Final global formatting pass to ensure 100% compliance with `clang-format`.

## [0.12.12] - 2026-02-05

### Fixed
- **MISRA**: Eliminated all suppressions for Rule 14.3 by using non-constant stubs in `src/platform_stubs.c`.
- **CI/CD**: Final stabilization of static analysis for professional deployment baseline.

## [0.12.11] - 2026-02-05

### Fixed
- **MISRA**: Resolved `knownConditionTrueFalse` violation (MISRA 14.3) in `src/params.c` without using suppressions.
- **Architectural**: Refactored NVM weak stubs to a dedicated platform file (`src/platform_stubs.c`).

## [0.12.10] - 2026-02-05

### Fixed
- **CI/CD**: Fixed all static analysis warnings from `cppcheck` (style, const usage, and false positive in params).
- **CI/CD**: Resolved persistent `clang-format` issues by replacing macro with inline function.

## [0.12.9] - 2026-02-05

### Fixed
- **CI/CD**: Replaced `IOLINK_US_FROM_MS` macro with `iolink_us_from_ms` function to resolve persistent `clang-format` ambiguity.
- **CI/CD**: Verified formatting compliance across all platform ports.

## [0.12.8] - 2026-02-05

### Fixed
- **CI/CD**: Final fix for `clang-format` ambiguity in `time_utils.h`.
- **CI/CD**: Enforced formatting check on all project files.

## [0.12.7] - 2026-02-05

### Fixed
- **CI/CD**: Fixed unit test regressions in `test_dll` and `test_isdu_segmented`.
- **CI/CD**: Fixed `clang-format` violations in `time_utils.h`.

## [0.12.6] - 2026-02-05

### Fixed
- **PHY**: Modified `phy_virtual` to skip `tcgetattr` if the device is not a TTY (allowing `/dev/null` usage in CI).

## [0.12.5] - 2026-02-05

### Fixed
- **Bare Metal**: Fixed runtime crash in `examples/bare_metal_app` by initializing dummy port `/dev/null` for virtual PHY.
- **Unit Tests**: Fixed compilation error in `tests/test_dll.c` (undeclared variable `bad_frame` scope).

## [0.12.4] - 2026-02-05

### Fixed
- **Bare Metal**: Fixed API mismatch in `examples/bare_metal_app` (`iolink_init` arguments).
- **Unit Tests**:
  - Fixed `test_dll` failure caused by mock underflow in `recv_byte` during recovery.
  - Fixed `test_isdu` assertion logic to match flow control response timing.

## [0.12.3] - 2026-02-05

### Fixed
- **CI/CD**: Fixed `clang-format` violations across the codebase.
- **CI/CD**: Updated `cppcheck` configuration to suppress `unusedFunction` false positives for library APIs.

## [0.12.2] - 2026-02-05

### Fixed
- **Conformance**: Fixed critical State Machine and Error Injection conformance failures.
  - Resolved **False Wakeup Detection** in `OPERATE` state by using invalid MC byte `0x55`.
  - Fixed **ISDU State Machine** bug in Index `0x001C` (Detailed Device Status) read.
  - Fixed **Type 0 Protocol Collision** in test suite by enforcing Type 2 for large ISDU payloads.
  - Updated Virtual Master tool to support ISDU writes > 15 bytes.

## [0.12.1] - 2026-02-04

### Changed
- Updated LICENSE: Added dual-licensing terms (GPLv3 + Commercial).
- Updated project version to 0.12.1.

## [0.12.0] - 2026-02-04

### Changed
- **Major Codebase Refactoring and Quality Improvement**:
  - Centralized all IO-Link protocol constants in `include/iolinki/protocol.h`.
  - Refactored `src/dll.c` to improve readability and reduce complexity (modularized `handle_operate`).

## [0.11.0] — 2026-02-04

### Changed
- **Major Codebase Refactoring and Quality Improvement**:
  - Centralized all IO-Link protocol constants in `include/iolinki/protocol.h`.
  - Refactored `src/dll.c` to improve readability and reduce complexity (modularized `handle_operate`).
  - Refactored `src/isdu.c` to simplify the segmentation engine and standardize index handling.
  - Standardized code style across the project (MISRA-C style NULL checks and unsigned literals).
  - Resolved build warnings and improved build integrity.
- Updated `CMakeLists.txt`, `Doxyfile`, and documentation to version 0.11.0.

## [0.10.0] — 2026-02-03

### Added
- **SIO Mode Support**: Single-wire bidirectional communication mode
  - `iolink_dll_set_sio_mode()` and `iolink_dll_set_sdci_mode()` API functions
  - `iolink_dll_get_phy_mode()` to query current PHY mode
  - Virtual Master `set_sio_mode()` and `set_sdci_mode()` methods
  - Mode switching only allowed in OPERATE state
- **Virtual Master Enhancements**:
  - Added `verify_checksum()` for proper CRC validation
  - Added `checksum_ok` and `pd_valid` fields to `DeviceResponse`
  - Enhanced `run_cycle()` with `od_req2` and `ckt` parameters for better control
  - Improved `run_cycle_bad_crc()` with same parameters
- **Test Suites**:
  - `test_sio_mode.py` - SIO mode switching and restrictions tests

### Changed
- Fixed event detection in `DeviceResponse` (bit 0x80 instead of 0x01)
- Initialize PHY mode to SDCI in `iolink_dll_init()`
- Updated ROADMAP to mark SIO mode as complete

## [0.9.0] — 2026-02-03

### Added
- **Variable PD Types (1_V and 2_V)**: Support for dynamic Process Data length changes (2-32 bytes)
  - `iolink_dll_set_pd_length()` and `iolink_dll_get_pd_length()` API functions
  - Virtual Master `set_pd_length()` method for dynamic PD length changes
  - Frame length calculation for variable PD in `get_req_len()`
- **Integration Testing Infrastructure**:
  - `test_device_connection.py` - Automated device connectivity tests for Types 0, 1_2, 2_2
  - `test_integration.sh` - Bash integration test runner
  - `run_conformance_all_types.sh` - Multi-type conformance test runner
- **Test Suites**:
  - `test_pd_variable.py` - Variable PD type tests with dynamic length changes
  - Updated `test_conformance_isdu.py` with M-sequence type parametrization via environment variables

### Changed
- Updated Virtual Master README with Variable PD examples and comprehensive M-sequence type table
- Enhanced conformance tests to support multiple M-sequence types (0, 12, 22)
- Improved DLL initialization for variable PD field setup (pd_in_len_current, pd_out_len_current)

### Fixed
- OD length calculation for Type 2_V (now correctly uses 2-byte OD)

## [0.8.0] — 2026-02-03

### Added
- **M-Sequence Type Support**: Full implementation of Types 1_1, 1_2, 2_1, and 2_2 for comprehensive protocol coverage.
- **Variable OD Length**: Dynamic On-request Data sizing (1 or 2 bytes) based on M-sequence type.
- **Mandatory ISDU Indices**: Implemented all remaining mandatory indices per IO-Link V1.1.5:
  - 0x000A: Vendor ID (16-bit)
  - 0x000B: Device ID (32-bit)
  - 0x000D: Profile Characteristic (16-bit)
  - 0x001B: Device Status (8-bit)
- **PHY API Extensions**: Added optional hardware feature support:
  - Wake-up pulse detection (80μs pulse on C/Q line)
  - C/Q line state control
  - L+ voltage monitoring (18-30V range)
  - Short circuit detection
- **Error Handling**: Comprehensive error tracking and retry logic:
  - CRC error counter
  - Timeout error counter
  - Framing error counter
  - Automatic retry mechanism (max 3 attempts)
  - Timing statistics (response time tracking)
- **Unit Tests**: New `test_m_sequence_types.c` for validating M-sequence frame handling.

### Changed
- **DLL Context**: Added `od_len` field for dynamic OD sizing.
- **Device Info**: Moved `access_locks` from global variable to `device_info` structure for better encapsulation.
- **ISDU Write Support**: Enhanced Index 0x000C (Access Locks) to support write operations.

### Fixed
- **Global Variables**: Eliminated all global state variables (removed `g_access_locks`).
- **Multi-Instance Support**: All APIs now properly use context pointers for full reentrancy.

## [0.7.0] — 2026-02-03

### Added
- **Parametrization Manager**: New module (`src/params.c`) for persistent storage of ISDU parameters (e.g., Application Tag).
- **NVM Abstraction**: Added Non-Volatile Memory read/write hooks to the platform layer for cross-reboot persistence.
- **Dockerized Validation Suite**: Integrated optimized, two-stage Docker testing infrastructure for both Linux and Zephyr targets.
- **GitHub Actions Optimization**: Enhanced CI with Docker image caching for 5GB+ Zephyr environments.
- **Variable PD Verification**: Fully verified Type 1_V and 2_V frames using the updated Virtual Master automation.

### Changed
- **ISDU Response Logic**: Improved state machine robustness by resetting response buffers and flags on a per-request basis.

### Fixed
- **Platform Headers**: Corrected missing `size_t` definitions and header guards in `platform.h`.
- **Zephyr CI Paths**: Fixed `west` workspace initialization issues in automated testing scripts.

## [0.6.0] — 2026-02-02

### Added
- **Mandatory ISDU Indices**: Full support for IO-Link V1.1.5 mandatory indices (0x0010-0x0018, 0x001E, 0x0024).
- **V1.1.5 Compliance**: Implemented strict Control Byte interleaving/multiplexing for 1-byte OD systems.
- **Automated Integration Testing**: New Python Virtual Master test suite for verifying mandatory indices and protocol conformance.
- **CI Enforcement**: Integrated Virtual Master tests into the project validation suite (`test_all.sh`).

### Fixed
- **DLL State Machine**: Fixed missing `isdu_process` calls in Type 0 and Preoperate states that caused acyclic messaging hangs.
- **ISDU Response State**: Fixed a bug preventing the reset of response control flags between transactions.

## [0.5.0] — 2026-02-02

### Added
- **ISDU Completion**: Full support for IO-Link V1.1.5 segmented transfers (interleaved Control/Data bytes for 1-byte OD).
- **Flow Control**: Added ISDU Busy state handling for better acyclic messaging management.
- **16-bit Indexing**: Enhanced range for ISDU parameter access (16-bit Index, 8-bit Subindex).
- **New Tests**: Added `tests/test_isdu_segmented.c` for multi-frame ISDU validation.

## [0.4.0] — 2026-02-02

### Added
- **Variable Process Data**: Full support for IO-Link V1.1.5 frame types (Type 1_V, 2_V) supporting 2-32 bytes.
- **Data Validity**: Added mandatory `valid` flag to `iolink_pd_input_update` and `PDStatus` bit propagation in status byte.
- **Thread Safety**: Wrapped all Process Data input/output in platform critical sections.
- **Extended M-Sequences**: Supported Type 1_V and Type 2_V in DLL.
- **New Tests**: Added `tests/test_pd_variable.c` for multi-byte PD exchange validation.

### Changed
- **API (BREAKING)**: `iolink_pd_input_update` now requires a third `bool valid` argument.

## [0.3.0] — 2026-02-02

### Added
- **Core Portability**: Refactored entire stack to use Context-Based API (no global state).
- **Embedded Config**: Centralized `iolink_config.h` for tuning buffer sizes and memory usage.
- **RTOS Integration**: Added `platform.h` with critical section hooks (`iolink_critical_enter/exit`) for thread safety.
- **FreeRTOS Support**: Added `examples/freertos_app` demonstrating multi-task usage.
- **Zephyr Support**: Native simulation integration (`test_zephyr.sh`) and module manifest (`west.yml`).
- **Docker Environment**: Unified build/test container for Linux, Bare Metal, and Zephyr.
- **Strict Quality**: Enforced `-Werror`, `-Wconversion` across codebase.
- **Documentation**: Added `MEMORY_GUIDE.md` and `SECURITY.md`.

### Changed
- **Logging**: Removed `printf` from core logic; introduced logging abstraction.
- **CI/CD**: Added locking verification tests and static analysis enhancements.

## [0.2.0] — 2026-02-01

### Added
- **Application Layer API**: Introduced `iolink_pd_input_update` and `iolink_pd_output_read`.
- **Process Data (PD)**: Cyclic exchange support in `OPERATE` state.
- **ISDU Support**: Acyclic messaging engine with segmentation/reassembly.
- **Standard Services**: Implementation of Vendor Name (Index 0x10) reading.
- **Events**: Diagnostic signaling mechanism with FIFO queue and ISDU Index 2 integration.
- **Data Storage (DS)**: Parameter backup/restore state machine with storage abstraction.
- **Architecture Documentation**: Added `docs/ARCHITECTURE.md` detailing the layered design.
- **Unit Tests**: Added `test_pd.c` and `test_isdu.c`.

### Changed
- **MISRA Audit**: Improved code quality, fixed unused parameters, and ensured strict typing.
- **DLL State Machine**: Transition logic from Preoperate to Operate based on protocol events.

## [0.1.0] — 2026-02-01

### Added
- **Foundational Architecture**: Layered design with hardware abstraction.
- **PHY Abstraction**: `iolink_phy_api_t` interface for portability.
- **DLL Core**: Initial state machine (Startup, Preoperate) and CRC6 calculation.
- **Simulation Drivers**: `phy_virtual` for host-side execution and `phy_mock` for TDD.
- **Automated CI/CD**: GitHub Actions for linting, testing, and releases.
- **Host Demo**: `examples/host_demo` showing stack execution without hardware.
- **Documentation**: Initial README, ROADMAP, VISION, and RELEASE_STRATEGY.

[Unreleased]: https://github.com/w1ne/iolinki/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/w1ne/iolinki/compare/v2.2.0...v3.0.0
[2.2.0]: https://github.com/w1ne/iolinki/compare/v2.1.1...v2.2.0
[2.1.1]: https://github.com/w1ne/iolinki/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/w1ne/iolinki/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/w1ne/iolinki/compare/v1.2.0...v2.0.0
[1.2.0]: https://github.com/w1ne/iolinki/compare/v1.1.3...v1.2.0
[0.11.0]: https://github.com/w1ne/iolinki/compare/v0.10.0...v0.11.0
[0.10.0]: https://github.com/w1ne/iolinki/compare/v0.9.0...v0.10.0
[0.9.0]: https://github.com/w1ne/iolinki/compare/v0.8.0...v0.9.0
[0.5.0]: https://github.com/w1ne/iolinki/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/w1ne/iolinki/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/w1ne/iolinki/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/w1ne/iolinki/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/w1ne/iolinki/releases/tag/v0.1.0

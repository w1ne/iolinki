# Agent Task Queue

This file is the shared task board for agents. Claims are enforced by a lock directory.

Claiming rules (mutex enforced):
1. Claim a task with `tools/claim_task.sh <task-id> <name>`. This creates `docs/claims/task-<id>.lock` atomically.
2. If the claim command fails, the task is already taken. Pick another task.
3. Do not edit `Status:` manually when claiming. The script updates it.
4. The claim script creates a draft report file in `docs/agent_reports/` and regenerates `docs/AGENT_REPORTS.md`.
5. When finished or blocked, add a final report file in `docs/agent_reports/` (keep the draft).
6. Mark completion with `tools/complete_task.sh <task-id> <name>`. The lock stays to prevent re-claim.
7. Coordinators can run `tools/validate_tasks.sh` to sanity-check claims and report files.

Enforcement: work is considered invalid unless the lock directory exists for that task.

Adding tasks (for coordinators/agents):
1. Add new tasks at the end of this file to avoid merge conflicts.
2. Use the template below and increment the task ID (next unused number).
3. Set `Status:` to `unclaimed`.
4. Include scope, primary files, and acceptance criteria.
5. Do not create or edit lock files when adding tasks.

---

## Task 1: Frame Retry Logic + Error Recovery
Status: done by Antigravity (2026-02-04)
Scope:
- Add retry handling per frame type with a retry budget.
- Ensure retry interacts safely with `FALLBACK` and `OPERATE` transitions.
- Expose retry counts in DLL stats if useful.
Primary files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `tests/test_error_recovery.c`
- `tools/virtual_master/test_conformance_error_injection.py`
Acceptance:
- CRC errors trigger retries before fallback.
- Tests cover retry success and retry exhaustion.

## Task 2: State Transition Validation Guards
Status: done by Antigravity (2026-02-04)
Scope:
- Add explicit guard checks for invalid transitions.
- Ensure invalid frames don’t crash or incorrectly advance state.
- Emit counters or events for illegal transitions if desired.
Primary files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `tests/test_dll.c`
- `tools/virtual_master/test_conformance_state_machine.py`
Acceptance:
- Unit tests cover at least 3 invalid transition scenarios.
- Conformance state machine tests still pass.

## Task 3: Error Event Reporting
Status: done by Antigravity (2026-02-04)
Scope:
- Emit diagnostic events for CRC, timeout, framing, and timing violations.
- Map to standard event code ranges (0x1xxx–0x8xxx) where applicable.
- Ensure events are visible via OD/ISDU without destructive pop.
Primary files:
- `src/dll.c`
- `src/events.c`
- `include/iolinki/events.h`
- `src/isdu.c`
- `tests/test_events.c`
Acceptance:
- Events are triggered on CRC, timeout, framing, and timing violations.
- Tests validate event generation and retrieval.

## Task 4: ISDU Flow Control (Busy/Retry)
Status: done by Antigravity (2026-02-05)
Scope:
- Implement busy/retry handling for concurrent ISDU requests.
- Ensure segmented transfers remain consistent under load.
Primary files:
- `src/isdu.c`
- `include/iolinki/isdu.h`
- `tests/test_isdu_segmented.c`
- `tools/virtual_master/test_conformance_isdu.py`
Acceptance:
- Busy state is emitted correctly under overlap.
- Segmented transfers complete without corruption.

## Task 5: Mandatory ISDU Indices (Remaining)
Status: done by Antigravity (2026-02-04)
Scope:
- Implement remaining mandatory indices: `0x0019`, `0x001A`, `0x001C–0x001E`.
- Validate with ISDU read tests and Virtual Master reads.
Primary files:
- `src/isdu.c`
- `include/iolinki/protocol.h`
- `tests/test_isdu.c`
- `tools/virtual_master/test_conformance_state_machine.py`
Acceptance:
- Reads of these indices return valid, non-empty responses.
- Tests cover each index.

## Task 6: Frame Synchronization + t_byte/t_bit Timing
Status: done by Frame Sync and Timing (2026-02-05)
Scope:
- Implement frame synchronization and enforce `t_byte` / `t_bit` timing where feasible.
- Add measurement counters for inter-byte timing violations.
Primary files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `include/iolinki/config.h`
- `tests/test_timing.c`
Acceptance:
- Inter-byte timing violations are detected and counted.
- Unit tests cover at least one violation case.

## Task 7: Link Quality Metrics
Status: claimed by Antigravity (2026-02-04)
Scope:
- Track link quality metrics (error rate, retry rate, uptime/quality window).
- Expose via DLL stats struct and optional ISDU index if desired.
Primary files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `src/isdu.c`
- `tests/test_error_recovery.c`
Acceptance:
- Metrics increment correctly on errors/retries.
- Tests validate metric updates.

## Task 8: PHY Diagnostics (L+ Voltage + Short Circuit)
Status: done by Antigravity (2026-02-05)
Scope:
- Implement default handling paths for `get_voltage_mv` and `is_short_circuit`.
- Add device events on fault conditions if supported.
Primary files:
- `src/dll.c`
- `src/events.c`
- `include/iolinki/phy.h`
- `tests/test_events.c`
Acceptance:
- Voltage/short-circuit faults trigger diagnostic events.
- Tests validate event emission.

## Task 9: Error Reporting Index
Status: done by codex (2026-02-04)
Scope:
- Add ISDU index for error statistics (CRC/timeouts/framing/timing).
- Ensure index returns a consistent binary structure.
Primary files:
- `src/isdu.c`
- `include/iolinki/protocol.h`
- `tests/test_isdu.c`
Acceptance:
- Reads return correct stats and lengths.
- Tests cover at least CRC and timeout counters.

## Task 10: SIO Fallback Behavior
Status: done by Antigravity (2026-02-05)
Scope:
- Implement SIO fallback when validation fails or repeated errors occur.
- Ensure safe transition back to SDCI.
Primary files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `tests/test_dll.c`
- `tools/virtual_master/test_sio_mode.py`
Acceptance:
- Device enters SIO on repeated validation failures.
- Tests cover SIO enter/exit paths.

## Task 11: System Command Handlers (0x0002 Remaining)
Status: done by antigravity (2026-02-04)
Scope:
- Implement remaining System Command subcommands: `0x80`–`0x84`, `0x95`–`0x97`.
- Ensure commands route through the ISDU framework and update device state safely.
- Add/extend tests for each subcommand and verify Virtual Master behavior.
Primary files:
- `src/isdu.c`
- `src/dll.c`
- `include/iolinki/protocol.h`
- `tests/test_isdu.c`
- `tools/virtual_master/test_conformance_isdu.py`
Acceptance:
- Reads/writes to index `0x0002` execute each subcommand with defined side effects.
- Unit tests cover all seven subcommands with expected responses.

## Task 12: Process Data Consistency Toggle Bit
Status: done by Antigravity (2026-02-05)
Scope:
- Implement PD consistency toggle bit mechanism for cyclic process data.
- Ensure toggle behavior follows IO-Link V1.1.5 expectations across PDIn/PDOut.
- Add unit tests and update integration tests if needed.
Primary files:
- `src/app.c`
- `include/iolinki/app.h`
- `include/iolinki/protocol.h`
- `tests/test_app_pd.c`
- `tools/virtual_master/test_conformance_pd.py`
Acceptance:
- Toggle bit flips correctly when PD content updates.
- Tests validate consistency behavior for both input and output PD.

## Task 13: Standard Event Code Mapping
Status: done by Antigravity (2026-02-05)
Scope:
- Map diagnostic events to standard event code ranges (0x1xxx–0x8xxx).
- Define event mode (single/multiple) handling and qualifiers as needed.
- Ensure event visibility through OD/ISDU without destructive pop.
Primary files:
- `src/events.c`
- `include/iolinki/events.h`
- `src/isdu.c`
- `tests/test_events.c`
Acceptance:
- Standard codes are emitted for at least one event per range.
- Tests validate event codes and retrieval behavior.

## Task 14: Data Storage Integration with Device Access Locks
Status: done by Antigravity (2026-02-05)
Scope:
- Integrate Data Storage (DS) upload/download flow with Device Access Locks (0x000C).
- Implement DS commands: Upload Start/End, Download Start/End.
- Add checksum mismatch recovery behavior.
Primary files:
- `src/ds.c`
- `src/isdu.c`
- `include/iolinki/protocol.h`
- `tests/test_ds.c`
Acceptance:
- DS commands follow lock rules and return correct status.
- Tests cover upload, download, and checksum mismatch recovery.

## Task 15: SIO Mode Switching Logic
Status: claimed by Antigravity (2026-02-05)
Scope:
- Implement dynamic SIO ↔ SDCI transitions and AutoComm behavior.
- Ensure safe mode switching and error recovery.
- Add unit and integration tests for mode transitions.
Primary files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `tests/test_dll.c`
- `tools/virtual_master/test_sio_mode.py`
Acceptance:
- Device transitions to SIO and back to SDCI without state corruption.
- Tests cover AutoComm and error-triggered mode switches.

## Task 16: Context-Based API (Remove Globals)
Status: unclaimed
Scope:
- Remove global state (`g_dll_ctx`, `g_isdu`, etc.) and pass context pointers.
- Update APIs and callers to use explicit context.
- Ensure tests and examples compile with the new API signatures.
Primary files:
- `src/dll.c`
- `src/isdu.c`
- `include/iolinki/dll.h`
- `include/iolinki/isdu.h`
- `tests/test_dll.c`
- `tests/test_isdu.c`
Acceptance:
- No global context remains in core modules.
- All tests compile and pass with context-based API usage.

## Task 17: Power-On Delay (t_pd) Enforcement
Status: done by codex (2026-02-05)
Scope:
- Implement `t_pd` power-on delay handling before responding to the Master.
- Add a timer/counter and configuration hook for the delay value.
- Emit a timing violation counter/event if communication occurs before `t_pd` elapses.
Primary files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `include/iolinki/config.h`
- `tests/test_timing.c`
Acceptance:
- Device does not respond to frames until `t_pd` has elapsed.
- Unit test covers at least one pre-`t_pd` violation case.

# Agent Reports

This file is the shared reporting log for agents working on `iolinki`.
Append a new section at the end when you finish or get blocked.

Template (append below):

```
## Task: <short name>
Status: in progress | done | partial | blocked
Summary:
- ...
Files:
- ...
Tests:
- ...
Follow-ups:
- ...
```

## Task: Frame Retry Logic + Error Recovery
Status: done
Summary:
- Implemented retry budget (3 attempts) for all frame types in PREOPERATE and OPERATE states.
- Added cumulative retry tracking to DLL statistics for diagnostics.
- Fixed a gap where PREOPERATE state did not verify CRCs or handle retries.
- Verified that existing integration tests pass and identified pre-existing conformance test failures.
Files:
- `src/dll.c`
- `include/iolinki/dll.h`
Tests:
- `./test_all.sh` (Integration tests passed, Conformance tests have pre-existing failures)
Follow-ups:
- Implement retry logic in DLL.
- Implement conformance tests for error injection.

## Task: Task 1: Frame Retry Logic + Error Recovery
Status: done
Summary:
- Implemented unified retry logic in `PREOPERATE`, `ESTAB_COM`, and `OPERATE` states.
- Added `total_retries` cumulative statistics to DLL.
- Verified CRC fallback (3 retries) using conformance test `test_07`.
Files:
- `src/dll.c`
- `include/iolinki/dll.h`
- `tests/test_error_recovery.c`
Tests:
- `test_conformance_error_injection.py` (Passed test_06, test_07)
Follow-ups:
- Investigate baseline failures in `test_01`, `test_04` (possibly related to ISDU/OD timing).

## Task: State Transition Validation Guards
Status: done
Summary:
- Implemented `is_valid_mc_for_state` helper in `src/dll.c`.
- Added guards to `PREOPERATE`, `ESTAB_COM`, and `OPERATE` to reject invalid Master Commands.
- Added unit tests for invalid transition command (0x0F) in `OPERATE` and illegal channel bits.
- Verified that all state machine conformance tests pass and guards function correctly.
Files:
- `src/dll.c`
- `tests/test_dll.c`
Tests:
- `tests/test_dll` (Passed)
- `test_conformance_state_machine.py` (Passed)
Follow-ups:
- Investigate baseline failures in `test_01`, `test_04` (possibly related to ISDU/OD timing).

## Task: Error Event Reporting (Task 3)
Status: done
Summary:
- Implemented standard IO-Link event codes (0x1801-0x1804) in events.h.
- Added event emission for CRC, timeout, framing, and timing errors in dll.c.
- Implemented mandatory ISDU indices 0x001B (Device Status) and 0x001C (Detailed Device Status).
- Added event engine helpers to get highest severity and list all active events.
- Fixed recovery robustness by resetting the ISDU engine on communication fallback.
Files:
- include/iolinki/events.h
- src/events.c
- src/dll.c
- src/isdu.c
Tests:
- tools/virtual_master/verify_events.py (Passed)
- tools/virtual_master/test_conformance_error_injection.py (Improved stability/recovery)
Follow-ups:
- Address timing/dropout issues in remaining failing conformance tests (test_01, test_02, test_04, test_05).

## Task: 4 - ISDU Flow Control (Busy/Retry)
Status: in progress
Summary:
- Claimed by Antigravity (2026-02-04)
Files:
- ...
Tests:
- not run
Follow-ups:
- ...

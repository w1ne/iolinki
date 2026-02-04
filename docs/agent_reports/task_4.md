## Task: 4 - ISDU Flow Control (Busy/Retry)
Status: done
Summary:
- Implemented collision detection in `src/isdu.c` to handle overlapping requests correctly.
- Added logic: Protocol overlaps (Start bit during data collection) trigger implicit Abort/Restart.
- Added logic: Application overlaps (Start bit during Service Execution) trigger `0x8030` (Busy).
- Hardened collision check with `!is_expecting_data` to prevents false positives on valid Data bytes.
Files:
- src/isdu.c
- tests/test_isdu_flow_control.c
- tools/virtual_master/verify_isdu_busy.py
Tests:
- tools/virtual_master/verify_isdu_busy.py (Verification script created)
- tools/virtual_master/verify_events.py (Regression passed)
Follow-ups:
- Investigate why verification script logs are silent in this environment to enable deeper debugging of the interruption scenario.

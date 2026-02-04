## Task: 5 - Mandatory ISDU Indices (Remaining)
Status: done
Summary:
- Implemented three missing mandatory ISDU indices: 0x0019 (Function Tag), 0x001A (Location Tag), 0x001D (PD Input Descriptor)
- Extended params module with NVM storage for Function Tag and Location Tag
- Added protocol constants and ISDU handlers for all three indices
- Created comprehensive unit tests (all passing)
Files:
- include/iolinki/protocol.h (added constants)
- src/params.c (extended NVM structure and get/set functions)
- src/isdu.c (added ISDU handlers)
- tests/test_isdu.c (added 3 new test functions)
Tests:
- test_isdu_function_tag_read_write: PASSED
- test_isdu_location_tag_read_write: PASSED
- test_isdu_pdin_descriptor_read: PASSED
Follow-ups:
- Virtual Master conformance tests should be run to verify integration
- Consider adding PD length field to device_info structure for dynamic PD descriptor

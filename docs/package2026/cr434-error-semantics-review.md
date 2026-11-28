# CR434 error-semantics review — iolinki device stack

Status: **draft-based review, no compliance claim.** Based on review drafts, not released documents:

- Interface **D1.1.6-02**, August 2026 (review draft; all CRs in state "Review", pp. 318-322)
- Baseline comparison: Interface **V1.1.5** (Oct 2025); Table C.1 values are identical in both

Scope: CR434 — the ErrorTypes `SERV_NOTAVAIL` (0x20), `SERV_NOTAVAIL_LOCCTRL` (0x21),
`SERV_NOTAVAIL_DEVCTRL` (0x22) and `FUNC_UNAVAILTEMP` (0x36) are transient and shall not be
used for permanently unavailable functions. Review of the device stack error-code usage only;
no wire change was made.

## 1. The reviewed spec text

D1.1.6-02, **C.2.5, p. 273**:

> This ErrorType shall be used if a parameter is not accessible for a read or write service due to
> the current state of the Device application.
> NOTE: The temporary restriction is expected to disappear after an acceptable time without user interaction [CR434].

D1.1.6-02, **C.2.6, p. 273**:

> This ErrorType shall be used if a parameter is not accessible for a read or write service due to
> an ongoing local operation at the Device (for example operation or parameterization via an on-board Device control panel).
> NOTE: The temporary restriction is expected to disappear after an acceptable time without user interaction [CR434].

D1.1.6-02, **C.2.7, p. 273**:

> This ErrorType shall be used if a read or write service is not accessible due to a remote triggered
> state of the device application (for example parameterization during a remote triggered teach-in operation or calibration).
> NOTE: The temporary restriction is expected to disappear after an acceptable time without user interaction [CR434].

D1.1.6-02, **C.2.15, p. 274**:

> This ErrorType shall be used for a write service with a command value calling a Device function
> not available due to the current state of the Device application (for example a SystemCommand).
> NOTE: The temporary restriction is expected to disappear after an acceptable time without user interaction [CR434].

V1.1.5 C.2.5/C.2.6/C.2.7/C.2.15 contain the same rules without the NOTE. The NOTE text was also
read in the CR434 write-up in the draft's change-request annex (p. 318 ff., CR list state "Review").

Relevant Table C.1 rows (D1.1.6-02 pp. 272-273; identical in V1.1.5 p. 275 ff.):

| ErrorCode | AdditionalCode | Name | Nature |
|---|---|---|---|
| 0x80 | 0x11 | `IDX_NOTAVAIL` | permanent — index does not exist (C.2.3) |
| 0x80 | 0x20 | `SERV_NOTAVAIL` | **transient** (CR434 / C.2.5) |
| 0x80 | 0x21 | `SERV_NOTAVAIL_LOCCTRL` | **transient** (CR434 / C.2.6) |
| 0x80 | 0x22 | `SERV_NOTAVAIL_DEVCTRL` | **transient** (CR434 / C.2.7) |
| 0x80 | 0x35 | `FUNC_NOTAVAIL` | permanent — command not supported (C.2.14) |
| 0x80 | 0x36 | `FUNC_UNAVAILTEMP` | **transient** (CR434 / C.2.15) |

Note: the spec text defines when these ErrorTypes "shall be used". CR434 is a clarification that
the four transient codes must not be used for permanent conditions; it adds no new wire encoding.

## 2. Method

`grep -rn` over `src/`, `include/`, `examples/`, `samples/` for the macros
`IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL`, `IOLINK_ISDU_ERROR_BUSY`,
`IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL`, `IOLINK_ISDU_ERROR_NOT_ACCESSIBLE`,
`IOLINK_ISDU_ERROR_WRITE_PROTECTED`, `IOLINK_ISDU_ERROR_PARAM_INCONSISTENT` and for the raw
values `0x13`, `0x20`, `0x21`, `0x22`, `0x23`, `0x30`, `0x35`, `0x36`, plus a manual
walk of every `response_buf[1]` / `error_code` assignment in `src/isdu.c`.

## 3. Result: none of the four CR434 transient codes is used anywhere

`SERV_NOTAVAIL` (0x20), `SERV_NOTAVAIL_LOCCTRL` (0x21), `SERV_NOTAVAIL_DEVCTRL` (0x22) and
`FUNC_UNAVAILTEMP` (0x36) have **zero occurrences** in the device stack (source, headers,
examples, samples). The stack can therefore not violate CR434's prohibition: no permanently
unavailable function is reported with a transient code.

The catch is the constant naming, which is the actual finding of this review (section 4).

## 4. What `IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL` really emits

`include/iolinki/protocol.h:167` defines:

```c
#define IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL 0x11U  /**< Requested service not available. */
```

On the wire, 0x11 is **`IDX_NOTAVAIL` (index not available, C.2.3)** — a *permanent* error.
`SERV_NOTAVAIL` would be 0x20 and is transient. The constant's name and doc comment do not
match Table C.1; the emit sites below therefore answer with a *permanent* code, not with any of
the four CR434 codes. All wire-emitting sites, with their condition:

| Site | Condition | Wire code | CR434 verdict |
|---|---|---|---|
| `src/isdu.c:174` | I-Service nibble outside {0x1-0x3, 0x9-0xB} (reserved/illegal service) | 0x80 0x11 | permanent request error; not a transient code — defensible under CR434 |
| `src/isdu.c:192` | 16-bit-index request truncated (index octets missing) | 0x80 0x11 | malformed request; permanent — defensible under CR434 |
| `src/isdu.c:204` | 8-bit-index request truncated (index octet missing) | 0x80 0x11 | malformed request; permanent — defensible under CR434 |
| `src/isdu.c:450` | `iolink_device_info_get()` returns NULL (stack not initialized) | 0x80 0x11 | permanent internal fault — defensible under CR434 |
| `src/isdu.c:521` | ApplicationTag write rejected by persistent storage (NVM write failure) | 0x80 0x11 | device-side failure, may be permanent — must **not** be reported with a transient code; current permanent code is the safe choice |
| `src/isdu.c:551` | FunctionTag write rejected by persistent storage | 0x80 0x11 | as above |
| `src/isdu.c:581` | LocationTag write rejected by persistent storage | 0x80 0x11 | as above |
| `src/isdu.c:637` | default branch: unknown/non-existing index (e.g. 0x001C-0x001F, B.8) | 0x80 0x11 | correct per C.2.3 (`IDX_NOTAVAIL`) — defensible |
| `src/isdu.c:703` | `iolink_ds_handle_command()` returns unknown result (currently only -3, unknown DS command) | 0x80 0x11 | unsupported command is permanent; C.2.14 assigns 0x35 — wrong code, but not a CR434 violation (permanent for permanent) |
| `src/isdu.c:870` | Error-stats vendor index 0x0040 but no DLL context bound | 0x80 0x11 | index effectively not available in this build — defensible |
| `src/isdu.c:1055` | Data Storage index 0x0003 but no DS context bound | 0x80 0x11 | index effectively not available in this build — defensible |
| `src/isdu.c:1112` | vendor service produced more than the response capacity | 0x80 0x11 | C.2.12 assigns 0x33 (`VAL_LENOVRRUN`) for "data object too large … ISDU buffer restriction" — wrong code, but not a CR434 violation |
| `src/isdu.c:1134` | SystemCommand write with empty payload | 0x80 0x11 | malformed request; permanent — defensible under CR434 |

Two more sites assign the constant but never put it on the wire:

- `src/isdu.c:319` — COUNT FlowCTRL without START sets `ctx->error_code`, then
  `isdu_transport_reset()` on the next line clears it; the next read answers No Service (0x00).
  Not observable, no CR434 exposure (dead assignment).
- `src/isdu.c:363` — CHKPDU mismatch sets `ctx->error_code`, but the wire response is
  `0x80 0x00` APP_DEV (line 366). `error_code` is an internal field.

Related codes checked and found correctly placed under CR434:

- `IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL` = 0x35 (`FUNC_NOTAVAIL`, permanent, C.2.14):
  `src/isdu.c:751` for an unsupported SystemCommand value. Correct; this is the permanent
  counterpart of `FUNC_UNAVAILTEMP`.
- `IOLINK_ISDU_ERROR_NOT_ACCESSIBLE` / `IOLINK_ISDU_ERROR_WRITE_PROTECTED` = 0x23
  (`IDX_NOT_ACCESSIBLE`, permanent, C.2.8): `src/isdu.c:603, 696-700, 790, 856, 1001, 1145`.
  Access/direction denials, not transient restrictions; correct.

Conclusion for CR434: **the stack is already compliant with the CR434 clarification as written
in D1.1.6-02.** No wire change is needed or made. The review draft also leaves "acceptable time"
undefined, so no timing claim is made.

## 5. Adjacent findings (not CR434, no change in this PR)

Found while walking the error paths; each is a candidate for a separate, individually reviewed
change. They are **not** changed here because altering wire error codes affects master
interoperability and existing tests pin the current values.

1. **Macro name vs wire value** — `IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL` (0x11) is
   `IDX_NOTAVAIL`, not `SERV_NOTAVAIL` (0x20); doc comment and the comment in
   `tests/test_vendor_isdu.c:80` repeat the wrong name. The wire value 0x11 is right for the
   default unknown-index branch (`src/isdu.c:637`) but not for the tag-storage,
   vendor-length, and malformed-request branches. Worst case: a future "fix" that changes the
   value to 0x20 based on the name would put a transient code on permanent paths — exactly what
   CR434 prohibits. Suggested follow-up: rename the constant (e.g.
   `IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL`) and, separately, re-map the non-index branches
   (APP_DEV 0x00, `VAL_LENOVRRUN` 0x33 per C.2.12, `FUNC_NOTAVAIL` 0x35 per C.2.14).
2. **`IOLINK_ISDU_ERROR_BUSY` = 0x30** (`include/iolinki/protocol.h:172`) — Table C.1 defines
   0x30 as `PAR_VALOUTOFRNG` (C.2.9), not "device busy"; Table C.1 has no busy ErrorType (ISDU
   busy is signalled at FlowCTRL level, e.g. a 0x01 read start during execution). Sites:
   `src/isdu.c:696, 719, 732` (Data Storage command while a DS operation is active or a legacy
   upload/download cannot start). A transient condition is reported with an unrelated permanent
   code.
3. **DS apply-image failure** — `src/isdu.c:1066` uses
   `IOLINK_ISDU_ERROR_PARAM_INCONSISTENT` = 0x40 (`PAR_SETINVALID`, C.2.16); C.2.17
   (`PAR_SETINCONSIST` = 0x41) explicitly names "after a DS_DownloadEnd Command, if the
   plausibility check shows inconsistencies". Candidate for re-mapping to 0x41.
4. **No 0x13 usage anywhere** — 0x13 is not defined in Table C.1 (reserved); confirmed absent
   from the stack.

## 6. Caveats

- D1.1.6-02 is a review draft; the CRs are in state "Review". Re-check the CR434 wording and
  Table C.1 when the released V1.1.6 package is available. Table C.1 is unchanged from V1.1.5,
  so no re-mapping is expected.
- "Acceptable time" is not quantified in the draft, so this review cannot define a conformant
  retry/clear timeout; it only verifies that the codes are not misused.
- No official Package 2026 test tool or certification result was available; no compliance claim
  is made.

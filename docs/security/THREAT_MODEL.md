# iolinki Threat Model

**Aligned to:** IO-Link Security Design and Development Guideline, Order No. 10.512
(D1.0.0-01, October 2025) and IO-Link Secure Deployment Guideline, Order No. 10.502
(V1.0.0, June 2025), both published by the IO-Link Community. This document
paraphrases and cites those guidelines; it does not reproduce their text. Obtain
them from [io-link.com/downloads](https://io-link.com/downloads).

**Scope:** the `iolinki` device stack as a software component integrated into an
IO-Link Device. Per 10.512 (clause 1), IO-Link Devices are *embedded devices* in
the sense of IEC 62443-4-2, targeted at capability security level SL-C 1, and the
protocol in scope is the wired point-to-point interface only — no networking, no
TCP/IP, no wireless. Anything outside the stack (device hardware, firmware-update
mechanism, product application logic) is the device maker's domain; this document
is explicit about where that boundary lies.

Every stack claim below carries a code or test anchor. A claim without an anchor
belongs in [§5 Gaps](#5-gaps-and-integrator-duties), not here.

## 1. System model and trust boundaries

```
 IO-Link Master ══ 3-wire cable ══╗ (untrusted input boundary)
                                  ▼
  PHY drivers          src/phy_generic.c, src/phy_virtual.c
    │ bytes
  Framing + checksum   src/frame.c, src/crc.c
    │ validated frames
  DLL state machine    src/dll.c            (mode/state legality)
    │ on-request data / process data
  ISDU parser          src/isdu.c           (service PDU parsing)
    ├─ Direct Parameters   src/params.c
    ├─ Data Storage        src/data_storage.c
    ├─ Events              src/events.c
    └─ Device Info         src/device_info.c
    │ callbacks (trusted)
  Device application   (device maker's code)
```

Trust boundaries:

- **The wire is untrusted.** Everything arriving at the PHY is attacker-controlled
  in the threat scenarios of 10.512 clause 6. The stack's job is to ensure no byte
  sequence received from the wire can corrupt stack state or the application's
  memory.
- **The application callbacks are trusted.** The stack executes in the device
  firmware's trust domain; it does not defend against its own host.
- **Build/supply chain** is outside the runtime model and covered by the SBOM
  (see `SECURITY.md`): the stack has zero third-party runtime dependencies, so the
  supply-chain surface is this repository itself plus the device maker's toolchain.

## 2. Assets

| Asset | Where it lives |
|---|---|
| Process data integrity (sensor/actuator values) | DLL + application callbacks |
| Device parameterization | `src/params.c`, `src/data_storage.c` |
| Identification and diagnosis data | `src/device_info.c`, `src/events.c` |
| Availability of the device function | whole stack, fixed-resource design |

## 3. STRIDE analysis

The threat catalogue follows 10.512 clause 6 (Table 1), which identifies spoofing
of either peer, tampering/replay on the wire, information disclosure on the wire,
and denial of service as the relevant threats, with **physical protection of cable
and device as the guideline's countermeasure at SL-C 1**. The protocol itself
carries no cryptographic authentication, integrity, or confidentiality mechanisms
(10.512 clauses 7.4.2, 7.5.2). The stack therefore cannot — and does not claim to —
defend against a physically present attacker; what it guarantees is that malformed
or hostile traffic is *rejected safely* rather than corrupting the device.

### S — Spoofing (either peer impersonated on the wire)

- Protocol reality (10.512 §6): no peer authentication exists; countermeasure is
  physical protection. Integrator duty.
- Stack guarantee: a spoofing peer gets no more capability than the protocol
  grants any Master. All state transitions are driven through the DLL state
  machine (`src/dll.c`); out-of-state requests are rejected rather than acted on.

### T — Tampering (modified, replayed, or forged frames)

- Protocol reality: the per-frame checksum/CRC detects *accidental* corruption
  only; intentional modification with a correct checksum is undetectable at the
  protocol level (10.512 §7.4.2). Integrator duty: physical protection; a security
  assessment note to that effect belongs in the device's user documentation
  (paraphrasing the contextual mapping of CR 3.1 in 10.512 §7.4.2).
- Stack guarantees (CR 3.1, CR 3.5 — 10.512 §7.4.2, §7.4.5):
  - Every received OPERATE frame is verified against CRC6 before use:
    `src/frame.c:71` (`checksum_ok` computed via `iolink_crc6`, `src/crc.c`).
  - Frame decode enforces exact expected length and PD/OD bounds; NULL and
    size violations are rejected before any parsing: `src/frame.c:61-62`
    (decode), `src/frame.c:30-34` (encode).
  - ISDU payloads land in fixed-size buffers (`IOLINK_ISDU_BUFFER_SIZE`,
    `include/iolinki/isdu.h:71-74`); reads are clamped to buffer size
    (`src/isdu.c:446-447`, `src/isdu.c:327,351,374`); segmented transfers
    enforce start/last/sequence-number legality and answer violations with the
    protocol SEGMENTATION error (`src/isdu.c:66-68,132`).
  - Stored parameterization is integrity-checked: the Data Storage image
    carries a Fletcher-16 checksum (`iolink_ds_calc_checksum`,
    `src/data_storage.c:74`), recomputed on serialization
    (`src/data_storage.c:121`) and used for consistency comparison and
    recovery on mismatch (CR 3.4 — 10.512 §7.4.4).
  - Data Storage writes respect the Device Access Locks parameter (index
    0x000C, `include/iolinki/protocol.h:45`): a set DS lock refuses the
    operation (`src/data_storage.c:301`).

### R — Repudiation

- Assessed not relevant for Devices in 10.512 (§7.3.9): no human users, no
  accounts; the Master timestamps and stores events (§7.3.8). No stack claims.

### I — Information disclosure (wire eavesdropping)

- Protocol reality: no encryption exists (10.512 §7.5.2); confidentiality of data
  in transit is achieved by restricting physical access. Integrator duty, to be
  stated in the device's user documentation per the guideline.
- Stack guarantee (CR 3.7 — 10.512 §7.4.7): error paths answer with
  protocol-defined error codes only (e.g. `IOLINK_ISDU_ERROR_*`, `src/isdu.c`);
  no internal state, addresses, or diagnostics beyond the spec-defined responses
  leave the device through the stack.

### D — Denial of service (flooding, selective drops, disconnection)

- Protocol reality: a point-to-point peer can always simply stop communicating;
  DoS by the peer is not defendable at the Device (10.512 §7.8.2 justification).
- Stack guarantees (aligned with the §7.8.2/§7.8.3 expectation that a Device
  processes traffic at the maximum protocol rate):
  - No dynamic memory allocation anywhere in the stack (no `malloc`/`free` in
    `src/` or `include/`); all contexts and buffers are fixed-size and
    zero-initialized (`iolink_ctx_zero`, `src/isdu.c:39`). Sustained flooding
    cannot exhaust heap because there is none.
  - Loss of communication is an explicit, observable DLL state: consecutive
    errors drive the FALLBACK state and SIO fallback
    (`include/iolinki/dll.h:32,114-115`), so an actuator application can apply
    its safe-state policy (CR 3.6 — 10.512 §7.4.6). Exercised by
    `tests/test_sio_fallback.c` and `tests/test_error_recovery.c`.
  - Event-queue overflow has defined behavior instead of failure, matching the
    overwrite-oldest expectation of 10.512 §7.3.7 (`src/events.c`,
    `tests/test_events.c`).

## 4. IEC 62443-4-2 requirement mapping (stack view)

10.512 clause 7 (Table 2) assesses which IEC 62443-4-2 requirements are relevant
for an IO-Link Device at SL-C 1. The rows below restate the *relevant* ones as
stack-level claims; requirement interpretation belongs to the guideline, the
anchors are ours. Rows assessed "not relevant" in Table 2 (FR 1 identification,
most of FR 2, host/network device requirements) are omitted here for the same
reasons the guideline gives: machine-to-machine point-to-point protocol, no human
users, not a network device.

| Requirement (10.512 ref) | Stack claim | Anchor |
|---|---|---|
| CR 2.8 auditable events (§7.3.5) | Security-relevant conditions are reported as protocol events with standard EventCodes (V1.1.5 Annex D constants) | `include/iolinki/events.h:34+`, `iolink_event_classify` `src/events.c:149` |
| CR 2.9 audit storage (§7.3.6) | Events queue until transmitted/acknowledged; bounded storage | `src/events.c`, `tests/test_events.c` |
| CR 2.10 audit failure response (§7.3.7) | Queue overflow behavior is defined (oldest overwritten), never a crash | `src/events.c` |
| CR 3.1 communication integrity (§7.4.2) | CRC6/checksum verified on every OPERATE frame; unintentional corruption detected | `src/frame.c:71`, `src/crc.c`, `tests/test_crc.c`, `tests/test_frame.c` |
| CR 3.4 software/information integrity (§7.4.4) | Data Storage image checksummed (Fletcher-16), consistency-compared, recovered on mismatch | `src/data_storage.c:74,121,157`, `tests/test_ds.c` |
| CR 3.5 input validation (§7.4.5) | Length, bounds, and sequence legality of every received frame and ISDU enforced before use | `src/frame.c:30-34,61-62`, `src/isdu.c:66-68,132,446-447`, `tests/test_isdu*.c` |
| CR 3.6 deterministic output (§7.4.6) | Communication loss is an observable DLL state for actuator safe-state policies | `include/iolinki/dll.h:32,114-115`, `tests/test_sio_fallback.c` |
| CR 3.7 error handling (§7.4.7) | Errors answered with protocol-defined codes only; no internal-state leakage | `src/isdu.c` error paths |
| CR 4.1/4.3 confidentiality, cryptography (§7.5) | Stack processes no confidential data and uses no cryptography; nothing to claim, nothing misused | — (see Gaps for device-level duties) |
| CR 5.1 network segmentation (§7.6.2) | Point-to-point by construction | protocol property |
| CR 7.3/7.4 backup and recovery (§7.8.4-5) | Data Storage parameter server (index 0x0003) implements upload/restore of device parameterization with Access-Lock integration | `src/data_storage.c`, `include/iolinki/protocol.h:45`, `tests/test_ds.c` |
| CR 7.6/7.7 config settings, least functionality (§7.8.6-7) | All optional stack features are compile-time configurable (`include/iolinki/config.h`); unused services can be excluded from the build | `include/iolinki/config.h` |

## 5. Gaps and integrator duties

Stated plainly, because a threat model that hides gaps is marketing:

1. **No BLOB Transfer & Firmware Update profile.** 10.512 expects Devices to be
   updatable and to verify update authenticity (EDR 3.2/3.10/3.14, §7.10.2-5).
   The stack implements the firmware-*revision* identification string
   (`include/iolinki/device_info.h:35`) but not the update transport or its
   verification. Firmware update capability, signature verification, and boot
   integrity are the device maker's responsibility. If you need the BLOB profile
   in the stack, that is commercial feature work.
2. **Physical protection is your countermeasure.** Per 10.512 §6 and the Secure
   Deployment Guideline 10.502, spoofing/tampering/disclosure on the wire are
   mitigated physically at SL-C 1. Your user documentation should carry a
   security-assessment recommendation to that effect (see the CR 3.1 contextual
   mapping in 10.512 §7.4.2 for the guideline's suggested wording).
3. **Device-level requirements the stack cannot see:** disabling local user
   interfaces (10.512 clause 5), confidential data at rest beyond IO-Link
   parameters (§7.5.2), boot-time integrity of your firmware image (§7.10.5),
   and your product's risk assessment under the CRA.
4. **10.512 is a draft under review until 2026-02-03.** Claims here cite
   D1.0.0-01; we track the final release and will re-verify this mapping.

## 6. Verification

The claims above are regression-checked by the existing test suite (cmocka via
CMake/CTest, run in Docker in CI — see `run_all_tests_docker.sh`):

- Frame/CRC validation: `tests/test_frame.c`, `tests/test_crc.c`,
  `tests/test_crc_manual.c`
- ISDU parsing, segmentation, flow control: `tests/test_isdu.c`,
  `tests/test_isdu_segmented.c`, `tests/test_isdu_flow_control.c`
- Data Storage checksum/recovery/locks: `tests/test_ds.c`
- Events and classification: `tests/test_events.c`
- Communication loss and fallback: `tests/test_sio_fallback.c`,
  `tests/test_error_recovery.c`, `tests/test_dll.c`
- End-to-end device behavior: `tests/test_integration_full.c`

Static analysis: cppcheck with MISRA-oriented rules (`tools/run-cppcheck.sh`,
`MISRA_DEVIATIONS.md`) runs in the CI quality gate.

*Maintenance rule:* any PR that changes a file cited as an anchor here must
re-verify the corresponding claim or update this document.

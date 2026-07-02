# CRA Security Package — Design

**Date:** 2026-07-02
**Status:** Approved in principle (user: "plan and do it"); detail decisions taken as
documented defaults, to be confirmed at PR review.
**Scope:** `iolinki` device stack first. The IO-Link master stack (`iolinki-master`)
and `udslib` reuse this template once it is proven here.

## 1. Problem

Stack suppliers are starting to sell CRA-readiness as a differentiator. TEConcept
announced (for H2 2026) a customer package consisting of: a CRA compliance statement
mapping their stack to Regulation (EU) 2024/2847, an SBOM per release (SPDX or
CycloneDX), a STRIDE threat model aligned with the IO-Link Security Design and
Development Guideline (Order No. 10.512), and contractually agreed security updates.

`iolinki` today has none of these: a boilerplate `SECURITY.md`, no SBOM, no threat
model, no update commitment. Independently of competition, selling commercial licenses
of the stack into the EU makes us a manufacturer of a "product with digital elements"
under the CRA: vulnerability-handling and ENISA reporting readiness applies from
September 2026, full obligations from December 2027. A protocol library is in the
CRA *default* class, so conformity is self-assessment — no notified body.

## 2. Goals

1. Ship the four deliverables for the `iolinki` device stack, beating the H2 2026
   timeline of competing stack vendors.
2. Make the recurring part (SBOM) fully automatic in the existing release pipeline.
3. Keep the package honest: every claim in the threat model maps to code that exists;
   gaps (e.g. no BLOB Transfer & Firmware Update profile) are stated as integrator
   responsibilities, not glossed over.
4. Respect the paid-only stance: credibility artifacts public, contract-grade
   artifacts commercial.

## 3. Non-goals

- IO-Link master stack and udslib coverage (follow-up, same template).
- Implementing new stack features (e.g. BLOB FW-update profile) — the package
  documents what exists.
- Legal sign-off. The CRA statement and contract clause ship marked DRAFT — PENDING
  LEGAL REVIEW; they are not offered to customers before that review.
- CE marking / EU Declaration of Conformity for iolinki itself (needs the legal
  review first; the statement template prepares for it).

## 4. Placement decision (public vs commercial)

| Artifact | Home | Rationale |
|---|---|---|
| SBOM per release (CycloneDX + SPDX) | public — release assets on w1ne/iolinki | Zero-dependency C stack; SBOM is cheap credibility and required due diligence input for every evaluator |
| `SECURITY.md` (disclosure policy, CRA vulnerability-handling readiness) | public repo root | Standard OSS location; CRA Annex I Part II expects a public disclosure channel |
| STRIDE threat model (10.512-aligned) | public — `docs/security/THREAT_MODEL.md` | The credibility play: evaluators can check rigor before contact; forces claims to stay tied to code |
| CRA overview for integrators | public — `docs/security/CRA.md` | Explains manufacturer-stays-responsible split and what the commercial package adds; the lead-generation surface |
| CRA compliance statement (Annex I mapping) | commercial — `iolinki-private/docs/cra/` | Contract-grade document, per-customer/per-release; TEConcept-equivalent deliverable |
| Security-update terms (support period, response targets) | commercial — `iolinki-private/docs/cra/` | Contract clause template attached to LICENSE.COMMERCIAL deals |

Default support period in the terms template: **5 years** from release, matching the
CRA's "at least five years" expectation (10.512 Annex A.2 summarizes the same),
negotiable per contract.

## 5. Deliverables in detail

### 5.1 SBOM generator — `tools/generate_sbom.py` (public)

- Plain-Python (stdlib only, ruff-clean per repo `pyproject.toml`), following the
  pattern of `tools/generate_release_notes.py`.
- Inputs: `--version` (from the release tag), `--format cyclonedx|spdx`, `--output`.
- Emits CycloneDX 1.6 JSON and SPDX 2.3 JSON describing one root component:
  the iolinki library (supplier, dual license `MIT OR LicenseRef-iolinki-Commercial`
  — match actual LICENSE terms at implementation time, purl
  `pkg:github/w1ne/iolinki@v<version>`), with explicit zero third-party
  runtime dependencies. Optional components section lists build/test-only tooling
  (cmocka, CMake) marked as excluded-from-runtime so the "no dependencies" claim is
  auditable rather than implied.
- Deterministic apart from the required timestamp fields (CI provides the clock).
- Tests: a Python unit test (`tests/test_generate_sbom.py` or `tools/`-adjacent,
  matching repo layout) asserting required fields, valid JSON, correct version
  propagation, and stable component identity. Runs in CI alongside existing checks.

### 5.2 Release pipeline — `.github/workflows/release.yml` (public)

- New step after tests: generate `iolinki-<version>.cdx.json` and
  `iolinki-<version>.spdx.json`, attach both to the GitHub release assets.
- CHANGELOG gains an Added entry; from the next tag onward every release carries
  its SBOMs.

### 5.3 `SECURITY.md` upgrade (public)

Replace the boilerplate with:
- Coordinated disclosure: GitHub private vulnerability reporting as the primary
  channel (verify it is enabled on the repo; enable if not), with response targets
  (acknowledge ≤ 72 h, triage verdict ≤ 14 days).
- Supported versions: latest release on `develop` free of charge; older versions
  under commercial support agreements only (links the paid-only stance).
- CRA readiness note: how vulnerabilities flow into advisories and releases; the
  actively-exploited-vulnerability reporting duty (ENISA early warning within 24 h,
  notification within 72 h) is acknowledged as an internal process commitment.
- Pointer to `docs/security/THREAT_MODEL.md` and the commercial CRA package.
- No invented contact addresses: use GitHub private reporting + the maintainer
  contact that actually exists.

### 5.4 STRIDE threat model — `docs/security/THREAT_MODEL.md` (public)

Structure mirrors 10.512 (paraphrased with clause citations only — the guideline's
license forbids reproducing its text/tables):

1. **Scope and system model** — the stack as an IEC 62443-4-2 *embedded device*
   component at SL-C 1; wired point-to-point protocol, no networking, no crypto in
   the protocol. Module map with trust boundaries:
   PHY byte input (`src/phy_*.c`) → framing + checksum (`src/frame.c`, `src/crc.c`)
   → DLL state machine (`src/dll.c`) → ISDU parser (`src/isdu.c`) → Data Storage
   (`src/data_storage.c`), Events (`src/events.c`), Direct Parameters
   (`src/params.c`), Device Info (`src/device_info.c`) → application callbacks.
2. **Assets** — process data integrity, device parameterization, identification
   data, availability of the sensor/actuator function.
3. **STRIDE analysis** — per threat category following 10.512 Section 6's threat
   catalogue (spoofing of either peer, tampering/replay on the wire, information
   disclosure on the wire, DoS by flooding/disconnection), stating for each: what
   the *protocol* leaves open (per 10.512: physical protection is the countermeasure
   at SL-C 1), what the *stack* guarantees (checksum verification, length/bounds
   validation of every received frame and ISDU, state-machine legality checks,
   fixed-rate processing without allocation), and what remains with the
   *integrator/device maker*.
4. **IEC 62443-4-2 requirement mapping (stack view)** — the 10.512 Table 2 "yes"
   rows restated as stack-relevant claims with code anchors, e.g.:
   - CR 2.8/2.9/2.10 (auditable events, storage, overflow behavior) → `src/events.c`
     event queue + V1.1.5 Annex D event codes.
   - CR 3.1 (communication integrity) → per-protocol checksum/CRC enforcement on
     receive and transmit paths.
   - CR 3.4 (software/information integrity) → Data Storage image Fletcher-16
     checksum + mismatch recovery.
   - CR 3.5 (input validation) → frame length/type validation, ISDU bounds checks.
   - CR 3.6 (deterministic output) → DLL exposes communication-loss state to the
     application for actuator fallback.
   - CR 3.7 (error handling) → error paths return protocol-defined codes, no
     internal state leakage.
   - CR 7.3/7.4 (backup/recovery) → Data Storage parameter server (index 0x0003)
     with Access-Lock integration.
   - EDR 3.2/3.10/3.14 (updates, boot integrity) → **explicit gap**: the stack does
     not implement the BLOB Transfer & Firmware Update profile; firmware update
     authenticity and boot integrity are device-maker responsibilities. Named
     integration hooks where applicable.
5. **Residual risks and integrator duties** — physical protection of cable/device
   (10.512/10.502 stance), user-doc statements the device maker should carry
   (verbatim-quote duty from CR 3.1 contextual mapping is *paraphrased*, with a
   pointer to the guideline), secure-deployment reference to Order No. 10.502.
6. **Verification** — how each stack claim is exercised by the existing test suite
   (cmocka + docker E2E), so claims stay regression-checked.

Every claim must carry a code or test anchor. If a claim cannot be anchored, it is
moved to the gaps section. Claims are verified against the source at writing time,
not asserted from memory.

### 5.5 CRA overview — `docs/security/CRA.md` (public)

Short document for device makers evaluating the stack:
- You remain the manufacturer; a stack cannot discharge CRA duties (mirrors the
  supplier-side framing now common in the market).
- What iolinki provides free (SBOM per release, public threat model, coordinated
  disclosure) and what the commercial package adds (compliance statement mapped to
  your product context, contractual update terms, support period).
- Not legal advice disclaimer.

### 5.6 CRA compliance statement template — `iolinki-private/docs/cra/CRA_COMPLIANCE_STATEMENT.md`

- Header block: stack release, customer, date, DRAFT — PENDING LEGAL REVIEW banner.
- Part 1: mapping of CRA Annex I Part I (product security requirements) to stack
  properties — secure-by-default configuration, attack-surface minimization, input
  validation, integrity protections, no known exploitable vulnerabilities at
  release (CI + cppcheck/MISRA evidence), data minimization (stack processes no
  personal data).
- Part 2: mapping of CRA Annex I Part II (vulnerability handling) to our process —
  SBOM, disclosure policy, advisory channel, update delivery via tagged releases,
  support period.
- Part 3: division of responsibility — what the device manufacturer must still do
  (their risk assessment, their DoC, their CE marking, their Article 14 reporting).
- Kept in the private repo; a rendered copy is delivered per commercial contract.

### 5.7 Security-update terms template — `iolinki-private/docs/cra/SECURITY_UPDATE_TERMS.md`

- Support period: 5 years from release date (default; per-contract override).
- Severity-tiered response targets (aligned with SECURITY.md public targets).
- Delivery: patched tagged releases + advisory; customer notification route.
- Explicitly scoped to security fixes (keeps the paid-only feature stance intact).
- DRAFT — PENDING LEGAL REVIEW banner.

## 6. Approach considered and rejected

- **Syft/cyclonedx-cli generated SBOM**: scanners find nothing useful in a
  zero-dependency C source tree; a first-party generator is smaller, deterministic,
  and testable. Rejected external tooling.
- **Everything commercial (including threat model)**: maximizes short-term gating
  but kills the credibility/marketing value; evaluators can't verify rigor before
  contact. Rejected.
- **Everything public (including compliance statement)**: gives away the
  contract-grade deliverable competitors charge for; conflicts with paid-only
  stance. Rejected.

## 7. Testing / verification

- SBOM generator: unit tests in CI (structure, version propagation, both formats).
- Release workflow: dry-run the generator step locally; full verification on the
  next `v*` tag.
- Threat model: every claim anchored to a file/test that exists at merge time;
  anchors checked during implementation, and the Verification section names the
  covering tests.
- Docs: repo quality gates (ruff for Python; markdown consistent with existing
  docs style).

## 8. Open items for the user (defaults taken, override at review)

1. Scope pass 1 = device stack only (master + udslib follow).
2. Threat model public vs commercial → public.
3. Support period default → 5 years.
4. Both SBOM formats (CycloneDX 1.6 + SPDX 2.3) vs one → both.
5. Legal review of 5.6/5.7 before first customer delivery → required, not scheduled
   here.

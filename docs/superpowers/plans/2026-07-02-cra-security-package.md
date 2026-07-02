# CRA Security Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the four-part CRA conformance package for the iolinki device stack: per-release SBOMs, a 10.512-aligned STRIDE threat model, an upgraded security policy, a public CRA overview, plus commercial-repo templates (compliance statement, update terms).

**Architecture:** One stdlib-only Python SBOM generator wired into the existing tag-triggered release workflow; four Markdown documents in the public repo; two DRAFT templates in `iolinki-private`. No stack (C) code changes.

**Tech Stack:** Python 3 stdlib (generator + unittest), GitHub Actions, Markdown.

**Spec:** `docs/superpowers/specs/2026-07-02-cra-security-package-design.md` — content requirements for every document live there (section 5). The 10.512 guideline PDF is at `/tmp/claude-1000/-home-andrii/06eaa99b-dd07-4d2b-b5a4-cbfca1bb54b5/scratchpad/iol_sec_desdev_10512.pdf`; its license forbids reproducing its text/tables — paraphrase + cite clause numbers only.

## Global Constraints

- Work in `~/projects/iolinki-cra-security` (worktree, branch `feat/cra-security-package`); iolinki-private work in `~/projects/iolinki-private` (plain checkout, default branch).
- License expression everywhere: `GPL-3.0-only OR LicenseRef-iolinki-Commercial`.
- Python must pass `ruff check` with repo `pyproject.toml`; stdlib only, no pip installs.
- Threat-model claims MUST be verified against source before written (grep the anchor; if absent, the claim moves to the gaps section).
- Commercial templates carry a `> **DRAFT — PENDING LEGAL REVIEW.**` banner and are not customer-deliverable until reviewed.
- Support period default: 5 years from release.

---

### Task 1: SBOM generator with tests

**Files:**
- Create: `tools/generate_sbom.py`
- Create: `tools/test_generate_sbom.py`

**Interfaces:**
- Produces CLI: `python3 tools/generate_sbom.py --version 1.1.3 --format cyclonedx|spdx --output <path>`; exit 0 on success.
- Produces functions used by tests: `build_cyclonedx(version: str) -> dict`, `build_spdx(version: str) -> dict`.

- [x] **Step 1: Write failing tests** — `tools/test_generate_sbom.py` (unittest): CycloneDX doc has `bomFormat == "CycloneDX"`, `specVersion == "1.6"`, root component name `iolinki`, `version` propagated, purl `pkg:github/w1ne/iolinki@v<version>`, license expression exact, `dependencies` for root component present with empty `dependsOn` (auditable zero-dep claim); SPDX doc has `spdxVersion == "SPDX-2.3"`, `SPDXID == "SPDXRef-DOCUMENT"`, one package with `licenseDeclared` exact, `externalRefs` purl, relationship `DESCRIBES`; CLI writes valid JSON to `--output` and exits 0; unknown `--format` exits non-zero.
- [x] **Step 2: Run tests, verify FAIL** — `python3 tools/test_generate_sbom.py` → import error / failures.
- [x] **Step 3: Implement `tools/generate_sbom.py`** — stdlib (argparse/json/datetime/uuid); metadata constants (supplier, license, purl, repo URL, description); timestamp from `SOURCE_DATE_EPOCH` if set else now (deterministic CI option); build/test-only tools (cmake, cmocka) listed as CycloneDX components with scope `excluded` / SPDX packages with `BUILD_TOOL_OF` relationship.
- [x] **Step 4: Run tests, verify PASS**; run `ruff check tools/generate_sbom.py tools/test_generate_sbom.py` → clean.
- [x] **Step 5: Commit** — `feat: add per-release SBOM generator (CycloneDX 1.6 + SPDX 2.3)`.

### Task 2: Wire SBOMs into release + CI

**Files:**
- Modify: `.github/workflows/release.yml` (after tests, before/with release creation)
- Modify: `.github/workflows/ci.yml` (new lightweight job)
- Modify: `CHANGELOG.md` (Unreleased → Added)

**Interfaces:**
- Consumes Task 1 CLI. Release assets named `iolinki-<version>.cdx.json`, `iolinki-<version>.spdx.json`.

- [x] **Step 1: release.yml** — add step generating both SBOMs with `${{ steps.version.outputs.version }}`, attach via existing release action's files list (inspect current upload mechanism and extend it).
- [x] **Step 2: ci.yml** — add job `sbom-tools` (ubuntu-latest, no Docker): checkout + `python3 tools/test_generate_sbom.py`.
- [x] **Step 3: Validate YAML** — `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml')); yaml.safe_load(open('.github/workflows/ci.yml'))"`.
- [x] **Step 4: CHANGELOG entry** — Added: per-release SBOMs + security docs package.
- [x] **Step 5: Commit** — `ci: attach CycloneDX/SPDX SBOMs to releases, test SBOM tool in CI`.

### Task 3: SECURITY.md upgrade

**Files:**
- Modify: `SECURITY.md` (full rewrite per spec 5.3)

- [x] **Step 1: Rewrite** — sections: Supported Versions (latest free; older = commercial support), Reporting (GitHub private vulnerability reporting primary; ack ≤72h, triage verdict ≤14d), Coordinated Disclosure (advisory + patched release; reporter credit), CRA Readiness (internal ENISA 24h/72h process commitment; SBOM pointer; threat model pointer; commercial package pointer). No invented email addresses.
- [x] **Step 2: Verify links resolve** (relative paths exist in repo).
- [x] **Step 3: Commit** — `docs: upgrade SECURITY.md to CRA-ready disclosure policy`.

### Task 4: STRIDE threat model

**Files:**
- Create: `docs/security/THREAT_MODEL.md` (structure per spec 5.4 — six sections)

- [x] **Step 1: Verify every planned code anchor** — grep each of: checksum verify path in `src/frame.c`/`src/crc.c`; ISDU bounds checks in `src/isdu.c`; DS Fletcher-16 + recovery in `src/data_storage.c`; event queue + Annex D codes in `src/events.c`; comm-loss surfacing in `src/dll.c`/`include/iolinki/*.h`; Access-Lock in `src/data_storage.c`/`src/params.c`. Note exact symbol/line for each; drop/move unverifiable claims to gaps.
- [x] **Step 2: Write the document** — paraphrase 10.512 §6 threat catalogue + §7 Table 2 stack-relevant rows with clause citations; explicit gaps: no BLOB Transfer & FW Update profile (EDR 3.2/3.10/3.14 → device maker), no crypto in protocol (CR 3.1/4.1 → physical protection per guideline), DoS unmitigable by protocol (CR 7.1 justification). Verification section maps claims → existing cmocka/E2E tests (grep `tests/` for covering tests, name them).
- [x] **Step 3: Commit** — `docs: add STRIDE threat model aligned to IO-Link guideline 10.512`.

### Task 5: Public CRA overview

**Files:**
- Create: `docs/security/CRA.md` (per spec 5.5)
- Modify: `README.md` (Security section: 3 links — SECURITY.md, THREAT_MODEL.md, CRA.md)

- [x] **Step 1: Write CRA.md** — manufacturer-stays-responsible framing; free vs commercial table; timeline (Sep 2026 reporting, Dec 2027 full); not-legal-advice disclaimer.
- [x] **Step 2: README security section** + link check.
- [x] **Step 3: Commit** — `docs: add CRA overview for device makers; link security docs from README`.

### Task 6: Commercial templates (iolinki-private)

**Files:**
- Create: `~/projects/iolinki-private/docs/cra/CRA_COMPLIANCE_STATEMENT.md` (per spec 5.6)
- Create: `~/projects/iolinki-private/docs/cra/SECURITY_UPDATE_TERMS.md` (per spec 5.7)

- [x] **Step 1: Check repo state** — `git -C ~/projects/iolinki-private status`, pull default branch.
- [x] **Step 2: Write both templates** — DRAFT banners; statement: Annex I Part I mapping (evidence: CI, cppcheck/MISRA, tests), Part II mapping (SBOM/disclosure/advisories/support period), responsibility split; terms: 5y default, severity tiers matching SECURITY.md targets, delivery via tagged release + advisory.
- [x] **Step 3: Commit + push** to iolinki-private default branch.

### Task 7: Final verification + PR

- [x] **Step 1: Full check** — `python3 tools/test_generate_sbom.py` PASS; `ruff check tools/` clean; dry-run `python3 tools/generate_sbom.py --version 0.0.0-test --format cyclonedx --output /tmp/t.cdx.json` + spdx variant, `python3 -m json.tool` both.
- [x] **Step 2: Anchor audit** — re-grep every file:symbol cited in THREAT_MODEL.md.
- [x] **Step 3: Push branch + open PR** to `develop` on w1ne/iolinki — body: deliverables list, decisions taken as defaults (spec §8), DRAFT/legal caveat, follow-ups (master stack, udslib, tag to exercise release SBOMs).

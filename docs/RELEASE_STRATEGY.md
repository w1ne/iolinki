# iolinki Release Strategy

This document outlines the development workflow, versioning, and release procedures for the `iolinki` project.

## 1. Workflow

We use a simplified **Gitflow** model.

### Branches
- **`main`**: Production code and stable releases. Protected; stable `vX.Y.Z`
  tags are accepted only from commits on this branch.
- **`develop`**: Integration branch. Source for `feature/*` and destination for verified features.
- **`feature/*`**: Feature branches. Created from and merged back to `develop` via PR.
- **`release/*`**: Release preparation branch. Open a PR to `develop`; after
  approval and checks, promote the reviewed commit to `main` using the protected
  branch process. An approved RC tag publishes a prerelease.
- **`bugfix/*`**: Fixes for production bugs. Merged to `develop` and `main` via PRs.

## 2. Versioning

We use **Semantic Versioning 2.0.0** (SemVer).

Format: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible source/binary API changes or significant wire protocol updates.
- **MINOR**: Backward-compatible new functionality (e.g., new standard definitions).
- **PATCH**: Backward-compatible bug fixes.

## 3. CI/CD

Automated pipelines run on every push to `main` or `develop`.

### Quality Gate (runs on all pushes/PRs)
1. **Code Formatting**: `clang-format` check (enforces `.clang-format` style).
2. **Static Analysis**: `cppcheck` with all checks enabled.
3. **MISRA Check**: Enforced in Docker via `cppcheck --addon=misra` (MISRA C:2012 subset). `IOLINKI_MISRA_ENFORCE=1` is set in release Docker runs.
4. **Unit Tests**: All CMocka tests must pass.
5. **Integration Tests**: Enforced - Virtual IO-Link Master conformance (V1.1.5).

### Build Stages
1. **Build**: CMake build for host (Linux).
2. **Test**: Run all unit tests via `ctest`.
3. **Example Build**: Verify example projects compile.

### Zephyr Build (Planned)
- Build verification on `native_sim` target.
- Cross-compile verification for ARM targets.

## 4. Release Process

### Release preparation PR

Prepare a release on a dedicated `release/x.y.z` branch based on `develop` and
submit it to `develop`. This PR is the review and CI gate; it does not merge,
tag, or publish the release.

1. Fetch and inspect both histories before branching:
   ```bash
   git fetch origin --prune
   git log --oneline --left-right origin/main...origin/develop
   git switch -c release/x.y.z origin/develop
   ```
2. If `main` has commits missing from `develop`, merge them into the release
   branch with a merge commit:
   ```bash
   git merge --no-ff origin/main
   ```
   Preserve both histories. Do not reset, rebase away commits, force-push, or
   bypass branch protections. Resolve code conflicts by retaining the behavior
   and tests from both lines. If a conflict cannot be resolved confidently or
   the licensing/provenance status is unclear, stop for maintainer/legal review.
3. Apply SemVer. Incompatible public API/ABI or wire changes require a new
   major version. Update `CMakeLists.txt`, the dated release entry and compare
   links in `CHANGELOG.md`, `docs/MIGRATION_<major>.md`, and the matching
   `ROADMAP.md` preparation entry. The changelog is the canonical source for
   generated release notes.
4. Run affected source tools and host checks where local resources permit. The
   required pull-request CI remains authoritative; do not skip required jobs or
   replace them with build artifacts from another revision.
5. Push the release branch and open a PR to `develop`. Review the generated
   notes, exact-source evidence, migration instructions and model/physical
   validation limits. Wait for required checks and review. Leave the PR open;
   merging is a separate maintainer action.

### Candidate workflow and publication

The `release.yml` pull-request workflow builds and packages a short-retention
candidate artifact and a separate notes artifact for the tested PR merge
commit. Candidate notes use the checked-in changelog and do not link to
unpublished release downloads. This is the dry-run path.

Pushing a `vX.Y.Z-rcN` tag is different: it runs the release workflow and
publishes a GitHub prerelease with downloadable artifacts. Do that only after
the release owner explicitly approves publication. Stable `vX.Y.Z` tags also
publish a GitHub Release and are accepted only from `main`.

After the release preparation PR has merged, promote the reviewed commit to
protected `main` through the repository's allowed protected-branch process.
The exact promotion commit must pass the required checks. If branch protection
rejects the update, stop and use its normal PR/check workflow; do not force-push
or use an admin bypass. Only then create and push the stable tag from `main`.
The stable-tag workflow builds coverage, generates notes from the release
entry, packages source/examples/firmware, host demos, IODDs, the IAR project,
LabWired recipes, test evidence, and CycloneDX/SPDX SBOMs, then creates the
GitHub Release. If `main` still has commits absent from `develop`, the workflow
opens a back-merge PR; it does not push to protected `develop`.

The GitHub source archive is the GPL/public distribution and does not expand
proprietary rights to GPL-only contributions or third-party material.
Commercial source delivery remains a separate operation requiring the final
rights review and agreed path list in
[`COMMERCIAL_SOURCE_SCOPE.md`](COMMERCIAL_SOURCE_SCOPE.md).

## 5. Release Artifacts

The release workflow attaches the following files to a stable GitHub Release:

- Versioned source and example archives.
- Firmware archive for the ESP32-C3, STM32G0 and STM32U5 example jobs.
- Linux host demos, the native STM32G0 IAR project, and counter/switching-sensor
  IODD packages.
- LabWired recipes and pinned engine commits.
- CycloneDX and SPDX SBOMs.
- JUnit results, test log/statistics, coverage summary, IODD validation log,
  host compiler version, source commit, and `SHA256SUMS` for the package files.
- GitHub's automatic source archives for the tagged commit.

On pull requests these are candidate workflow artifacts with three-day
retention; release-download URLs exist only after a tag publishes a release.
The notes describe only files produced by the workflow and state their
validation limits.

## 6. Distribution

### Open-Source Evaluation (GPLv3)
- **Source**: GitHub releases provide the full source archive (automatic).
- **Artifacts**: Example/test binaries are provided for evaluation and verification.
- **Checksums**: Each release includes SHA256 checksums for all published artifacts.

### Commercial Distribution
- **Access**: Commercial customers receive a licensed SDK bundle via a private download link after purchase.
- **Contents**:
  - Headers + libraries (platform-specific)
  - Example binaries
  - Test bundle (optional)
  - Documentation snapshot
- **License Delivery**: License file and terms are delivered with the SDK bundle.
- **Lead Time**: Typical fulfillment within 2 business days.
- **Pricing** (see `LICENSE.COMMERCIAL`):
  - **Indie**: €1,399 (one-time, royalty-free; named individual, own independent product family)
  - **Company**: €4,699 (one-time, royalty-free; named legal company, one agreed product family, with scoped engineering assistance)
  - **Enterprise**: Custom pricing

New quotes allow unlimited manufactured units and customers, including variants,
successors and hardware revisions within the agreed named commercial range.
Unrelated lines need a separate license. MCU/PHY changes within that range retain
the license; porting and validation are separately quoted. Enterprise scope for
multiple families, subsidiaries or broader SDK redistribution is quoted. Already
granted or separately agreed rights retain their accepted scope.
Indie is issued to a named individual developing and distributing their own
independent product family, including as a sole trader. It does not grant an
employer or separate company product rights. Company is held by the named legal
company and allows unlimited authorized employees and contractors working on the
family. Separate legal entities and independent contractor reuse require agreed
additional scope.

### Support Policy
- **Included Support**: 12 months of updates and support are included in the commercial price.
- **Assistance**: Indie includes two onboarding hours for build/setup, MCU/PHY wiring and callback review, and a written bring-up checklist, with a 48-hour email response target. Company includes eight total scoped engineering hours, a 24-hour priority response target and quarterly technical reviews during the first year within that allowance. Deliverables within the hours include build/configuration review, MCU/PHY callback and wiring review, a written checklist and findings from the agreed build/test review. Agree board/compiler/PHY/tasks and the booking window before scheduling. Larger ports are quoted separately; reproducible stack bug corrections do not consume custom engineering hours. See LICENSE.COMMERCIAL for holder and support terms.

### Provenance
- **Signing**: Release artifacts are signed or accompanied by checksums.

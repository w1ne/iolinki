# iolinki Release Strategy

This document outlines the development workflow, versioning, and release procedures for the `iolinki` project.

## 1. Workflow

We use a simplified **Gitflow** model.

### Branches
- **`main`**: Production code. Stable releases. **Protected: All official version tags (`vX.Y.Z`) MUST be pushed from this branch**.
- **`develop`**: Integration branch. Source for `feature/*` and destination for verified features.
- **`feature/*`**: Feature branches. Created from and merged back to `develop` via PR.
- **`release/*`**: Release preparation. Source for `vX.Y.Z-rcN` tags. Merged to `main` via PR for official release.
- **`bugfix/*`**: Fixes for production bugs. Merged to `develop` and `main` via PRs.

## 2. Versioning

We use **Semantic Versioning 2.0.0** (SemVer).

Format: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible API changes or significant protocol version updates.
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

### Automated Release (Main-only)

Official releases are triggered ONLY from the `main` branch. 

1. **Tagging**: Push a semantic version tag to `main`:
   ```bash
   git checkout main && git pull
   git tag -a v1.0.0 -m "Release version 1.0.0"
   git push origin v1.0.0
   ```

2. **Workflow**: The GitHub Action automatically:
   - Builds with **Code Coverage**.
   - Generates **Automated Release Notes**.
   - Packages binaries and creates a GitHub Release.
   - **Back-merges `main` to `develop`**.

### Release Candidates (RC)

RCs are used to verify the release workflow and quality before finalizing on `main`.

1. **RC Tagging**: Push an RC tag from a `release/*` branch:
   ```bash
   git checkout release/1.0.0
   git tag -a v1.0.0-rc1 -m "Release Candidate 1"
   git push origin v1.0.0-rc1
   ```
2. **Verification**: RC tags trigger the same build/test pipeline but do NOT update `main` or perform back-merges.

> [!TIP]
> Use RC tags to "dry-run" the release notes and binary packaging without affecting the production baseline.

### Manual Release (PR-Based Flow)

1. **Prepare Release Branch**:
   ```bash
   git checkout develop
   git checkout -b release/x.y.z
   ```

2. **Update Documentation**:
   - Update version in `CMakeLists.txt`
   - Update `CHANGELOG.md`
   - Update `ROADMAP.md` milestones

3. **Verify Quality Locally**:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   cd build && ctest --output-on-failure
   ```

4. **Open Pull Requests**:
   - Push the `release/x.y.z` branch to remote.
   - Open a PR from `release/x.y.z` to **`main`**.
   - **Wait for CI to pass** and get approval.
   - Merge the PR (this will update `main`).

5. **Tag the Release**:
   - On the updated `main` branch locally:
     ```bash
     git pull origin main
     git tag -a vx.y.z -m "Release vx.y.z"
     git push origin vx.y.z
     ```

6. **Troubleshooting & Retries**:
   - **Failed Release**: If the CI fails on a tag, delete the tag on remote and local, fix the issue on `main`, and re-tag:
     ```bash
     git tag -d v1.0.0
     git push origin :v1.0.0
     # Fix issue on main...
     git tag v1.0.0
     git push origin v1.0.0
     ```
   - **Pre-commit Blocks**: If hooks (e.g., branch name checks) block release housekeeping commits, use `--no-verify`:
     ```bash
     git commit -m "chore(release): bump version" --no-verify
     ```

## 5. Release Artifacts

Each GitHub Release includes:
- **Test Results**: Test count and pass/fail summary
- **Build Artifacts**:
  - `simple_device` - Example executable
  - `test_init` - Unit test executable
- **Documentation**: Links to all project docs
- **Source Code**: Automatic archive by GitHub

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
  - **Single Developer**: €1,399 (one-time, royalty-free)
  - **Team (5 seats)**: €4,699 (one-time, royalty-free)
  - **Enterprise**: Custom pricing

### Support Policy
- **Included Support**: 12 months of updates and support are included in the commercial price.
- **Integration Assistance**: Up to 8 hours of integration effort are included.

### Provenance
- **Signing**: Release artifacts are signed or accompanied by checksums.

# iolinki Release Strategy

This document outlines the development workflow, versioning, and release procedures for the `iolinki` project.

## 1. Workflow

We use a simplified **Gitflow** model.

### Branches
- **`main`**: Production code. Stable releases.
- **`develop`**: Integration branch. All features merge here first.
- **`feature/*`**: Feature branches. Created from and merged back to `develop`.
- **`release/*`**: Release preparation steps (version bumps, changelogs).
- **`bugfix/*`**: Fixes for bugs found in `develop`.

## 2. Versioning

We use **Semantic Versioning 2.0.0** (SemVer).

Format: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible API changes or significant protocol version updates.
- **MINOR**: Backward-compatible new functionality (e.g., new standard definitions).
- **PATCH**: Backward-compatible bug fixes.

## 3. CI/CD (Planned)

### Stages
1. **Quality Gate**:
   - `clang-format` check.
   - Static analysis.
2. **Build**:
   - CMake build for host (Linux).
   - Zephyr `native_sim` build.
   - ARM cross-compile verification (STM32/nRF).
3. **Tests**:
   - Unit tests (e.g., Unity/CMock).
   - Integration tests.

## 4. Release Process

1. **Freeze**: Create a `release/x.y.z` branch from `develop`.
2. **Audit & Documentation**:
   - Update `CHANGELOG.md`.
   - Update version numbers in `CMakeLists.txt` and headers.
   - Verify IODD file generation links/templates.
3. **Validate**: Ensure CI passes on the release branch.
4. **Publish**:
   - Merge `release/x.y.z` into `main`.
   - Tag the release: `git tag -a vx.y.z -m "Release vx.y.z"`.
   - Merge `main` back into `develop`.

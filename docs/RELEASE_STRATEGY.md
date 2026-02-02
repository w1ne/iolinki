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

## 3. CI/CD

Automated pipelines run on every push to `main` or `develop`.

### Quality Gate (runs on all pushes/PRs)
1. **Code Formatting**: `clang-format` check (enforces `.clang-format` style).
2. **Static Analysis**: `cppcheck` with all checks enabled.
3. **MISRA Check**: Planned for future compliance.
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

### Automated Release (Recommended)

Releases are automated via GitHub Actions. Simply push a version tag:

```bash
git tag -a v0.1.0 -m "Release version 0.1.0"
git push origin v0.1.0
```

The workflow automatically:
1. Builds the project in Release mode
2. Runs all tests
3. Generates formatted release notes including:
   - Test results summary
   - Build artifacts list
   - Documentation links
4. Packages binaries (examples + tests)
5. Creates GitHub Release (using content from `CHANGELOG.md`)

> [!IMPORTANT]
> Ensure `CHANGELOG.md` is updated and the `[Unreleased]` section is renamed to the version number **before** tagging.

### Manual Release (if needed)

1. **Prepare Release Branch**:
   ```bash
   git checkout develop
   git checkout -b release/x.y.z
   ```

2. **Update Documentation**:
   - Update version in `CMakeLists.txt`
   - Update `CHANGELOG.md` (if exists)
   - Update `ROADMAP.md` milestones

3. **Verify Quality**:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   cd build && ctest --output-on-failure
   ```

4. **Merge to Main**:
   ```bash
   git checkout main
   git merge release/x.y.z
   git tag -a vx.y.z -m "Release vx.y.z"
   git push origin main --tags
   ```

5. **Back-merge to Develop**:
   ```bash
   git checkout develop
   git merge main
   git push origin develop
   ```

## 5. Release Artifacts

Each GitHub Release includes:
- **Test Results**: Test count and pass/fail summary
- **Build Artifacts**: 
  - `simple_device` - Example executable
  - `test_init` - Unit test executable
- **Documentation**: Links to all project docs
- **Source Code**: Automatic archive by GitHub

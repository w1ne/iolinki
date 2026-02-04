# Testing & Quality Assurance

This document describes the testing strategy, quality standards, and CI/CD workflow for the `iolinki` project.

## 🏆 Philosophy
We adhere to **Strict Coding Standards** to ensure safety, portability, and reliability in embedded environments.
- **Zero Warnings**: Code must compile with `-Wall -Wextra -Werror`.
- **Static Analysis**: All code must pass `cppcheck` with no errors.
- **Portable C**: Code must be strictly C99/C11 compliant, with no hidden global state (Context-based API).
- **Automated Verification**: Every commit is verified against Linux, Bare Metal, and Quality checks.

## 🛠️ Local Testing

### 1. Run All Tests
Use the helper script to run the full validation suite (Linux Build + Type 1 Integration Test + Bare Metal compilation check).
```bash
./test_all.sh
```

### 2. Run Quality Checks
Verify code against warnings and static analysis tools.
```bash
./check_quality.sh
```

### 3. Docker Environment
To ensure a reproducible environment matching CI, run tests inside Docker.

**Build Image:**
```bash
docker build -t iolinki-test .
```

**Run Tests:**
```bash
docker run --rm iolinki-test
```

### 4. Zephyr Integration Test
Included automatically in the Docker environment.
To run locally, you need the Zephyr SDK and `west` installed.
```bash
./tests/test_zephyr.sh
```

## ✅ Continuous Integration (CI)

> [!IMPORTANT]
> **Release Requirement**: Passing the Virtual Master integration tests is a HARD requirement for any production release. Automated release tags will fail if these tests do not pass.

The CI pipeline (via Docker) enforces:

1.  **Strict Compilation**: Builds with `-Werror` on Linux and Bare Metal (ARM cross-compiler simulation).
2.  **Static Analysis**: Runs `cppcheck` to detect memory leaks, undefined behavior, and style issues.
3.  **Integration Tests**: 
    - `test_type1.py`: Basic protocol logic (Process Data, CRC).
    - `test_automated_mandatory.py`: Verification of all mandatory ISDU indices (0x0010-0x0018, etc.).
4.  **Zephyr Simulation**: Verifies the stack runs on Zephyr `native_sim` (if SDK available).

## 📊 Code Standards

### Syntax & Style
- **Indentation**: 4 spaces (no tabs).
- **Naming**: `snake_case` for variables/functions, `PASCAL_CASE` for macros/enums.
- **Prefixes**: All public API must use `iolink_` prefix.

### Safety Rules
- **No Global State**: All module state must be encapsulated in a context structure (e.g., `iolink_dll_ctx_t`).
- **Input Validation**: All public API functions must check pointer validity (`if (!ctx) return;`).
- **Memory**: No dynamic memory allocation (`malloc`/`free`) allowed in the core stack.
- **Types**: Use `<stdint.h>` types (`uint8_t`, `int32_t`) explicitly.

### MISRA C Compatibility
The code enforces a subset of MISRA C:2012 rules via `cppcheck --addon=misra` in Docker.
Set `IOLINKI_MISRA_ENFORCE=1` to require the MISRA addon to be available.
- **Rule 8.4**: Objects/functions shall be defined.
- **Rule 11.4**: No conversion between pointer and integer.
- **Rule 17.2**: No recursion.

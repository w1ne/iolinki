# Coding Standards

This document defines the coding style and engineering practices for the `iolinki` project.

## 1. C Language Standard
- **Standard**: C99 (ISO/IEC 9899:1999) or C11.
- **Safety**: Aim for **MISRA C:2012** compliance for all core stack components.

## 2. Naming Conventions
- **Prefixes**: All public symbols (functions, structs, enums) must start with `iolink_`.
- **Functions**: `snake_case` (e.g., `iolink_dll_process`).
- **Variables**: `snake_case`. Private/local variables do not require the prefix.
- **Types**: `snake_case` with `_t` suffix (e.g., `iolink_dll_ctx_t`).
- **Macros**: `UPPER_SNAKE_CASE` (e.g., `IOLINK_MAX_PAYLOAD`).

## 3. Documentation
- **Tool**: Doxygen.
- **Requirement**: Every public API function and structured type in `include/` must have a Doxygen comment block.
- **Style**:
  ```c
  /**
   * @brief Brief description of the function.
   * @param param_name Description of the parameter.
   * @return Description of the return value.
   */
  ```

## 4. Static Analysis
- **Cppcheck**: Must pass with `all` checks enabled.
- **MISRA**: Core files must be checked against the MISRA C:2012 ruleset.

## 5. Testing
- **Unit Tests**: Minimum 80% line coverage for the core library.
- **Framework**: CMocka.
- **Integration**: Every major feature must have a full-stack integration test.

## 6. Git & Formatting
- **Formatter**: `clang-format` using the project's `.clang-format` file.
- **Commit Messages**: Follow Conventional Commits (e.g., `feat: Add ISDU engine`).

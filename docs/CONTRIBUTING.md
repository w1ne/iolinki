# Contributing to iolinki

## Getting Started

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/w1ne/iolinki.git
    cd iolinki
    ```

2.  **Verify the environment**:
    Ensure you have CMake, GCC, Python 3, and cppcheck installed.
    ```bash
    ./check_quality.sh
    ```

## Development Workflow

1.  **Create a branch** for your feature or fix.
2.  **Write Code** following the [Code Standards](TESTING.md#code-standards).
3.  **Run Tests Locally**:
    ```bash
    ./test_all.sh
    ```
4.  **Check Quality**:
    ```bash
    ./check_quality.sh
    ```
5.  **Update Changelog**: Add a matching entry in `CHANGELOG.md` under `[Unreleased]`.
6.  **Submit Pull Request**: CI will automatically run the above checks.

## Coding Guidelines

We enforce strict quality rules. Please read [TESTING.md](TESTING.md) for detailed requirements.

-   **Portable**: Use Standard C (C99/C11). No OS-specific calls in `src/`.
-   **Safe**: No globals, no malloc, checked inputs.
-   **Clean**: No compiler warnings.

## Reporting Issues

Please file issues on GitHub with:
-   Description of the bug
-   Steps to reproduce
-   Expected vs Actual behavior
-   Logs/Output

Thank you for contributing!

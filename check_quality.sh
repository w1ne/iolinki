#!/bin/bash
set -e

echo "============================================"
echo "🔍 iolinki Code Quality & Safety Check"
echo "============================================"

# 1. Compiler Warnings (Strict)
# We rely on CMake to set -Wall -Wextra -Werror via the build system
echo -e "\n[1/3] 🛡️  Verifying Compilation Warnings..."
BUILD_DIR="${IOLINKI_BUILD_DIR:-build_quality}"
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cached_src=$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "${BUILD_DIR}/CMakeCache.txt" | cut -d= -f2-)
    if [ -n "${cached_src}" ] && [ "${cached_src}" != "$(pwd)" ]; then
        rm -rf "${BUILD_DIR}"
    fi
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
# Enable Strict Mode (we need to add this flag support to CMakeLists or force it here)
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow"
if make -j$(nproc); then
    echo "   ✅ Strict Compilation Passed"
else
    echo "   ❌ Strict Compilation FAILED"
    exit 1
fi
cd ..

# 2. Static Analysis (Cppcheck)
echo -e "\n[2/4] 🧹 Running Static Analysis (Cppcheck)..."
if command -v cppcheck &> /dev/null; then
    # Run cppcheck with rigorous settings
    # --enable=all: Enable all checks (style, performance, portability, etc.)
    # --suppress=missingIncludeSystem: Don't fail on missing standard headers
    # --error-exitcode=1: Fail script if errors found
    # --addon=misra: Ideally we would use this, but it requires a rules text file. 
    #                We'll use --enable=warning,style,performance,portability for now.
    
    cppcheck --enable=warning,style,performance,portability \
             --error-exitcode=1 \
             --suppress=missingIncludeSystem \
             --inline-suppr \
             --quiet \
             -I include \
             src/ examples/
             
    if [ $? -eq 0 ]; then
        echo "   ✅ Static Analysis Passed"
    else
        echo "   ❌ Static Analysis FAILED"
        exit 1
    fi
else
    echo "   ⚠️ Cppcheck not installed. Skipping static analysis."
    echo "      Install with: sudo apt-get install cppcheck"
fi

# 3. MISRA C:2012 Check (Cppcheck addon)
echo -e "\n[3/4] 📏 Running MISRA C:2012 Check..."
if command -v cppcheck &> /dev/null; then
    # Use cppcheck MISRA addon if available in the environment.
    # If the addon is not present, fail in enforce mode.
    MISRA_ADDON=""
    for path in /usr/share/cppcheck/addons/misra.py /usr/lib/cppcheck/addons/misra.py; do
        if [ -f "$path" ]; then
            MISRA_ADDON="$path"
            break
        fi
    done

    if [ -n "$MISRA_ADDON" ]; then
        cppcheck --addon=misra \
                 --enable=warning,style,performance,portability \
                 --error-exitcode=1 \
                 --suppress=missingIncludeSystem \
                 --inline-suppr \
                 --quiet \
                 -I include \
                 src/ examples/
        echo "   ✅ MISRA Check Passed"
    else
        if [ "${IOLINKI_MISRA_ENFORCE}" = "1" ]; then
            echo "   ❌ MISRA addon not available in cppcheck. Enforced mode fails."
            exit 1
        else
            echo "   ⚠️ Cppcheck MISRA addon not available. Skipping MISRA checks."
        fi
    fi
else
    echo "   ⚠️ Cppcheck not installed. Skipping MISRA checks."
fi

# 4. Code Formatting (Check only)
echo -e "\n[4/4] 🎨 Checking Code Formatting..."
# Placeholder: warning if clang-format not run (could force it)
echo "   ℹ️  Ensure you have run clang-format style files."

echo -e "\n============================================"
echo "✅ Code Quality Checks Completed"
echo "============================================"
exit 0

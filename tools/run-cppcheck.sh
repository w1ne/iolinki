#!/bin/bash
# Wrapper for cppcheck to be used with pre-commit
# It ensures correct include paths and suppression settings

set -e

# Extract directories from files passed by pre-commit to help cppcheck if needed,
# but mostly we just want to pass the flags.
# Pre-commit passes the list of files as arguments.

cppcheck --enable=warning,style,performance,portability \
         --error-exitcode=1 \
         --suppress=missingIncludeSystem \
         --suppress=unusedFunction \
         --inline-suppr \
         --quiet \
         -I include \
         "$@"

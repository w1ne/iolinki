#!/bin/bash
# Script to add copyright headers to all source files
# Copyright (C) 2026 Andrii Shylenko

C_HEADER='/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

'

PYTHON_HEADER='"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

'

# Function to add C/H header
add_c_header() {
    local file="$1"
    # Check if file already has copyright
    if ! grep -q "Copyright (C) 2026 Andrii Shylenko" "$file"; then
        echo "Adding header to $file"
        echo "$C_HEADER$(cat "$file")" > "$file"
    fi
}

# Function to add Python header
add_python_header() {
    local file="$1"
    # Skip if already has copyright
    if ! grep -q "Copyright (C) 2026 Andrii Shylenko" "$file"; then
        # Check if file has shebang
        if head -n1 "$file" | grep -q '^#!'; then
            shebang=$(head -n1 "$file")
            rest=$(tail -n +2 "$file")
            echo "Adding header to $file (with shebang)"
            echo -e "$shebang\n$PYTHON_HEADER$rest" > "$file"
        else
            echo "Adding header to $file"
            echo "$PYTHON_HEADER$(cat "$file")" > "$file"
        fi
    fi
}

# Process C and H files
find src include tests examples -type f \( -name "*.c" -o -name "*.h" \) | while read -r file; do
    add_c_header "$file"
done

# Process Python files
find tools/virtual_master -type f -name "*.py" | while read -r file; do
    add_python_header "$file"
done

echo "Copyright headers added successfully!"

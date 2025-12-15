#!/bin/bash
set -e # Exit immediately if a command exits with a non-zero status.

# Configuration
FIXPOW_BIN="build/clang20-release/fixpow"
SEARCH_DIR="tests/data"

# 1. Validate environment
if [ ! -f "$FIXPOW_BIN" ]; then
    echo "Error: fixpow binary not found at '$FIXPOW_BIN'"
    echo "Please build it first."
    exit 1
fi

# 2. Find and process files
found_count=0

# Use a while loop with find to correctly handle paths with spaces
while IFS= read -r input_path; do
    found_count=$((found_count + 1))
    
    # Strip the .nopow suffix to get the target path
    output_path="${input_path%.nopow}"

    echo "[minetests] Processing: $input_path"
    echo "            Target:     $output_path"

    # Run fixpow
    if "$FIXPOW_BIN" --in="$input_path" --out="$output_path"; then
        echo "[minetests] Success. Deleting .nopow source."
        rm "$input_path"
    else
        echo "[minetests] FAILURE: fixpow returned error for $input_path"
        exit 1
    fi
    echo "---------------------------------------------------"

done < <(find "$SEARCH_DIR" -type f -name "*.bin.nopow")

if [ "$found_count" -eq 0 ]; then
    echo "[minetests] No *.bin.nopow files found in $SEARCH_DIR."
fi
#!/bin/bash
set -e

# 1. Check for perf
PERF_CMD="perf"
# The system 'perf' wrapper exists but might fail if kernel tools are missing.
# We check if it actually runs.
if ! perf --version &> /dev/null; then
    # Fallback: Look for an executable perf binary in /usr/lib/linux-tools
    # We pick the first one found.
    FALLBACK=$(find /usr/lib/linux-tools -name perf -executable 2>/dev/null | head -n 1)
    
    if [ -n "$FALLBACK" ]; then
        PERF_CMD="$FALLBACK"
        echo "Warning: System 'perf' wrapper failed. Using fallback: $PERF_CMD"
    else
        echo "Error: 'perf' is not installed or not working."
        echo "Try: sudo apt install linux-tools-common linux-tools-generic linux-tools-\$(uname -r)"
        exit 1
    fi
fi

# 2. Get FlameGraph scripts
TOOLS_DIR=$(dirname "$(readlink -f "$0")")
FG_DIR="$TOOLS_DIR/FlameGraph"
if [ ! -d "$FG_DIR" ]; then
    echo "Cloning FlameGraph repository..."
    git clone https://github.com/brendangregg/FlameGraph "$FG_DIR"
fi

# 3. Run perf
# -F 99: Sample at 99Hz (avoids lockstep with clock)
# -g: Record call graphs (stack traces)
PERF_DATA="perf.data"
echo "Running perf record... (sudo required)"
# Note: "$@" passes all arguments from this script to the command being profiled
sudo "$PERF_CMD" record -F 99 -g -o "$PERF_DATA" -- "$@"

# 4. Generate SVG
OUT_SVG="flamegraph.svg"
echo "Generating $OUT_SVG..."
# We need to own the perf.data file to read it without sudo, or just run perf script with sudo
sudo "$PERF_CMD" script -i "$PERF_DATA" | "$FG_DIR/stackcollapse-perf.pl" | "$FG_DIR/flamegraph.pl" > "$OUT_SVG"

echo "Done! Open $OUT_SVG in your web browser."

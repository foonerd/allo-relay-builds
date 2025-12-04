#!/bin/bash
# allo-relay-builds clean-all.sh
# Clean all build artifacts

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "[+] Cleaning build artifacts..."

# Clean output directories
rm -f out/armv6/fn-rattenu*
rm -f out/armhf/fn-rattenu*
rm -f out/arm64/fn-rattenu*
rm -f out/amd64/fn-rattenu*

# Clean any temporary build directories
rm -rf build/

echo "[OK] Clean complete"

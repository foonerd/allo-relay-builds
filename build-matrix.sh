#!/bin/bash
# allo-relay-builds build-matrix.sh
# Build relay attenuator binaries for all architectures

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VERBOSE=""
LGPIO_PATH=""
NO_LIRC=""

# Parse arguments
for arg in "$@"; do
  if [[ "$arg" == "--verbose" ]]; then
    VERBOSE="--verbose"
  elif [[ "$arg" == --lgpio=* ]]; then
    LGPIO_PATH="$arg"
  elif [[ "$arg" == "--no-lirc" ]]; then
    NO_LIRC="--no-lirc"
  fi
done

# Require --lgpio flag
if [ -z "$LGPIO_PATH" ]; then
  echo "Usage: $0 --lgpio=<path> [--no-lirc] [--verbose]"
  echo ""
  echo "Arguments:"
  echo "  --lgpio: Path to lgpio-builds repository (REQUIRED)"
  echo "  --no-lirc: Build without LIRC support"
  echo "  --verbose: Show detailed build output"
  echo ""
  echo "Example:"
  echo "  $0 --lgpio=../lgpio-builds"
  echo "  $0 --lgpio=../lgpio-builds --no-lirc --verbose"
  exit 1
fi

echo "========================================"
echo "allo-relay Build Matrix"
echo "========================================"
echo "lgpio source: ${LGPIO_PATH#*=}"
echo "LIRC support: $([ -z "$NO_LIRC" ] && echo "enabled" || echo "disabled")"
echo ""

# Check source directory
if [ ! -d "source" ]; then
  echo "Error: source directory not found"
  exit 1
fi

if [ ! -f "source/r_attenu.c" ]; then
  echo "Error: source/r_attenu.c not found"
  exit 1
fi

# Build for all architectures
ARCHITECTURES=("armv6" "armhf" "arm64" "amd64")

for ARCH in "${ARCHITECTURES[@]}"; do
  echo ""
  echo "----------------------------------------"
  echo "Building for: $ARCH"
  echo "----------------------------------------"
  ./docker/run-docker-rattenu.sh "$ARCH" $LGPIO_PATH $NO_LIRC $VERBOSE
done

echo ""
echo "========================================"
echo "Build Matrix Complete"
echo "========================================"
echo ""
echo "Output structure:"
for ARCH in "${ARCHITECTURES[@]}"; do
  if [ -d "out/$ARCH" ]; then
    echo "  out/$ARCH/"
    ls -lh "out/$ARCH/fn-rattenu"* 2>/dev/null | awk '{printf "    %-20s %s\n", $9, $5}' || echo "    (no binaries)"
  fi
done

echo ""
echo "Binaries: fn-rattenu, fn-rattenuc"

#!/bin/bash
set -e

ARCHS=("armv6" "armhf" "arm64" "amd64")
VOL_MODE=""
VERBOSE=""
LGPIO_PATH=""
NO_LIRC=""

for arg in "$@"; do
  case "$arg" in
    --volumio) VOL_MODE="volumio" ;;
    --verbose) VERBOSE="--verbose" ;;
    --lgpio=*) LGPIO_PATH="${arg#--lgpio=}" ;;
    --no-lirc) NO_LIRC="--no-lirc" ;;
  esac
done

if [[ -z "$LGPIO_PATH" ]]; then
  echo "Error: --lgpio=<path> is required"
  echo "Usage: $0 --lgpio=../lgpio-builds [--volumio] [--verbose] [--no-lirc]"
  exit 1
fi

for ARCH in "${ARCHS[@]}"; do
  echo ""
  echo "====================================="
  echo ">> Preparing clean source directory"
  echo "====================================="
  ./scripts/extract.sh $NO_LIRC
  echo ""
  echo "====================================="
  echo ">> Building for architecture: $ARCH"
  echo "====================================="
  ./docker/run-docker-rattenu.sh rattenu "$ARCH" "$VOL_MODE" "$VERBOSE" "--lgpio=$LGPIO_PATH" "$NO_LIRC"
done

echo ""
echo "[OK] All builds completed. Check the 'out/' directory for results."

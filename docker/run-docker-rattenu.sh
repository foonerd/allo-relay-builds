#!/bin/bash
# allo-relay-builds docker/run-docker-rattenu.sh
# Core Docker build logic for relay attenuator binaries

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"

cd "$REPO_DIR"

VERBOSE=0
LGPIO_PATH=""
NO_LIRC=0

# Parse arguments
ARCH="$1"
shift || true

for arg in "$@"; do
  if [[ "$arg" == "--verbose" ]]; then
    VERBOSE=1
  elif [[ "$arg" == --lgpio=* ]]; then
    LGPIO_PATH="${arg#*=}"
  elif [[ "$arg" == "--no-lirc" ]]; then
    NO_LIRC=1
  fi
done

# Show usage if missing required parameters
if [ -z "$ARCH" ] || [ -z "$LGPIO_PATH" ]; then
  echo "Usage: $0 <arch> --lgpio=<path> [--no-lirc] [--verbose]"
  echo ""
  echo "Arguments:"
  echo "  arch: armv6, armhf, arm64, amd64"
  echo "  --lgpio: Path to lgpio-builds repository (REQUIRED)"
  echo "  --no-lirc: Build without LIRC support"
  echo "  --verbose: Show detailed build output"
  echo ""
  echo "Example:"
  echo "  $0 arm64 --lgpio=../lgpio-builds"
  echo "  $0 armv6 --lgpio=../lgpio-builds --no-lirc --verbose"
  exit 1
fi

# Locate lgpio DEBs
LGPIO_DEBS="$LGPIO_PATH/out/$ARCH"
if [ ! -d "$LGPIO_DEBS" ]; then
  echo "Error: lgpio DEBs not found for $ARCH: $LGPIO_DEBS"
  echo ""
  echo "Build them first:"
  echo "  cd $LGPIO_PATH"
  echo "  ./build-matrix.sh"
  exit 1
fi

# Count DEBs
DEB_COUNT=$(find "$LGPIO_DEBS" -name "*.deb" 2>/dev/null | wc -l)
if [ "$DEB_COUNT" -eq 0 ]; then
  echo "Error: No DEB files found in $LGPIO_DEBS"
  exit 1
fi

# Platform mappings for Docker
declare -A PLATFORM_MAP
PLATFORM_MAP=(
  ["armv6"]="linux/arm/v7"
  ["armhf"]="linux/arm/v7"
  ["arm64"]="linux/arm64"
  ["amd64"]="linux/amd64"
)

# Library path mappings
declare -A LIB_PATH_MAP
LIB_PATH_MAP=(
  ["armv6"]="/usr/lib/arm-linux-gnueabihf"
  ["armhf"]="/usr/lib/arm-linux-gnueabihf"
  ["arm64"]="/usr/lib/aarch64-linux-gnu"
  ["amd64"]="/usr/lib/x86_64-linux-gnu"
)

# Validate architecture
if [[ -z "${PLATFORM_MAP[$ARCH]}" ]]; then
  echo "Error: Unknown architecture: $ARCH"
  echo "Supported: armv6, armhf, arm64, amd64"
  exit 1
fi

PLATFORM="${PLATFORM_MAP[$ARCH]}"
LIB_PATH="${LIB_PATH_MAP[$ARCH]}"
DOCKERFILE="docker/Dockerfile.rattenu.$ARCH"
IMAGE_NAME="rattenu-builder:$ARCH"
OUTPUT_DIR="out/$ARCH"

if [ ! -f "$DOCKERFILE" ]; then
  echo "Error: Dockerfile not found: $DOCKERFILE"
  exit 1
fi

echo "========================================"
echo "Building relay attenuator for $ARCH"
echo "========================================"
echo "  Platform: $PLATFORM"
echo "  Lib Path: $LIB_PATH"
echo "  lgpio DEBs: $LGPIO_DEBS ($DEB_COUNT files)"
echo "  LIRC support: $([ "$NO_LIRC" -eq 0 ] && echo "enabled" || echo "disabled")"
echo "  Dockerfile: $DOCKERFILE"
echo "  Image: $IMAGE_NAME"
echo "  Output: $OUTPUT_DIR"
echo ""

# Build Docker image with platform flag
echo "[+] Building Docker image..."
if [[ "$VERBOSE" -eq 1 ]]; then
  DOCKER_BUILDKIT=1 docker build --platform=$PLATFORM --progress=plain -t "$IMAGE_NAME" -f "$DOCKERFILE" .
else
  docker build --platform=$PLATFORM --progress=auto -t "$IMAGE_NAME" -f "$DOCKERFILE" . > /dev/null 2>&1
fi
echo "[+] Docker image built: $IMAGE_NAME"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Special CFLAGS for ARM architectures
EXTRA_CFLAGS=""
if [[ "$ARCH" == "armv6" ]]; then
  EXTRA_CFLAGS="-march=armv6 -mfpu=vfp -mfloat-abi=hard -marm"
elif [[ "$ARCH" == "armhf" ]]; then
  EXTRA_CFLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
elif [[ "$ARCH" == "arm64" ]]; then
  EXTRA_CFLAGS="-march=armv8-a"
fi

# Absolute path to DEBs for mounting
ABS_LGPIO_DEBS="$(cd "$LGPIO_DEBS" && pwd)"

# Run build inside container with DEBs mounted
echo "[+] Running build inside container..."
if [[ "$VERBOSE" -eq 1 ]]; then
  docker run --rm --platform=$PLATFORM \
    -v "$(pwd)/source:/build/source:ro" \
    -v "$(pwd)/scripts:/build/scripts:ro" \
    -v "$(pwd)/$OUTPUT_DIR:/build/output" \
    -v "$ABS_LGPIO_DEBS:/debs:ro" \
    -e "ARCH=$ARCH" \
    -e "LIB_PATH=$LIB_PATH" \
    -e "EXTRA_CFLAGS=$EXTRA_CFLAGS" \
    -e "NO_LIRC=$NO_LIRC" \
    "$IMAGE_NAME" \
    bash /build/scripts/build-rattenu.sh
else
  docker run --rm --platform=$PLATFORM \
    -v "$(pwd)/source:/build/source:ro" \
    -v "$(pwd)/scripts:/build/scripts:ro" \
    -v "$(pwd)/$OUTPUT_DIR:/build/output" \
    -v "$ABS_LGPIO_DEBS:/debs:ro" \
    -e "ARCH=$ARCH" \
    -e "LIB_PATH=$LIB_PATH" \
    -e "EXTRA_CFLAGS=$EXTRA_CFLAGS" \
    -e "NO_LIRC=$NO_LIRC" \
    "$IMAGE_NAME" \
    bash /build/scripts/build-rattenu.sh 2>&1 | grep -E "^\[|^Error|^Building|warning:"
fi

echo ""
echo "[+] Build complete for $ARCH"
echo "[+] Binaries in: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"/fn-rattenu* 2>/dev/null || echo "(no binaries found)"

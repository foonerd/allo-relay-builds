#!/bin/bash
set -e

VERBOSE=0
LGPIO_PATH=""
NO_LIRC=""

# Parse arguments
COMPONENT=$1
ARCH=$2
MODE=$3
shift 3 || true

for arg in "$@"; do
  case "$arg" in
    --verbose) VERBOSE=1 ;;
    --lgpio=*) LGPIO_PATH="${arg#--lgpio=}" ;;
    --no-lirc) NO_LIRC="1" ;;
  esac
done

if [[ -z "$COMPONENT" || -z "$ARCH" ]]; then
  echo "Usage: $0 <component> <arch> [volumio] [--verbose] --lgpio=<path> [--no-lirc]"
  echo "Example: $0 rattenu armhf volumio --lgpio=../lgpio-builds"
  exit 1
fi

if [[ -z "$LGPIO_PATH" ]]; then
  echo "Error: --lgpio=<path> is required"
  exit 1
fi

LGPIO_PATH=$(cd "$LGPIO_PATH" && pwd)

DOCKERFILE="docker/Dockerfile.rattenu.$ARCH"
IMAGE_TAG="foonerd-rattenu-$ARCH"

declare -A ARCH_FLAGS
ARCH_FLAGS=(
  ["armv6"]="linux/arm/v7"
  ["armhf"]="linux/arm/v7"
  ["arm64"]="linux/arm64"
  ["amd64"]="linux/amd64"
)

if [[ -z "${ARCH_FLAGS[$ARCH]}" ]]; then
  echo "Error: Unknown architecture: $ARCH"
  echo "Available architectures: ${!ARCH_FLAGS[@]}"
  exit 1
fi

PLATFORM="${ARCH_FLAGS[$ARCH]}"

if [[ ! -f "$DOCKERFILE" ]]; then
  echo "Missing Dockerfile for architecture: $ARCH"
  exit 1
fi

# Check for lgpio DEBs
LGPIO_DEB_DIR="$LGPIO_PATH/out/$ARCH"
if [[ ! -d "$LGPIO_DEB_DIR" ]] || [[ -z "$(ls -A $LGPIO_DEB_DIR/*.deb 2>/dev/null)" ]]; then
  echo "Error: No lgpio DEBs found in $LGPIO_DEB_DIR"
  echo "Build lgpio first: cd $LGPIO_PATH && ./build-matrix.sh --volumio"
  exit 1
fi

echo "[+] Building Docker image for $ARCH ($PLATFORM)..."
if [[ "$VERBOSE" -eq 1 ]]; then
  DOCKER_BUILDKIT=1 docker build --platform=$PLATFORM --progress=plain -t $IMAGE_TAG -f $DOCKERFILE .
else
  docker build --platform=$PLATFORM --progress=auto -t $IMAGE_TAG -f $DOCKERFILE .
fi

# Prepare NO_LIRC env var for build
LIRC_ENV=""
if [[ -n "$NO_LIRC" ]]; then
  LIRC_ENV="export NO_LIRC=1 &&"
fi

echo "[+] Running build for $COMPONENT in Docker ($ARCH)..."
if [[ "$ARCH" == "armv6" ]]; then
  docker run --rm --platform=$PLATFORM \
    -v "$PWD":/build \
    -v "$LGPIO_DEB_DIR":/debs:ro \
    -w /build $IMAGE_TAG bash -c "\
    dpkg -i /debs/*.deb && \
    cd build/$COMPONENT/source && \
    $LIRC_ENV \
    export CFLAGS='-O2 -march=armv6 -mfpu=vfp -mfloat-abi=hard -marm' && \
    export CXXFLAGS='-O1 -Wno-psabi -march=armv6 -mfpu=vfp -mfloat-abi=hard -marm' && \
    export DEB_BUILD_MAINT_OPTIONS='hardening=+all optimize=-lto' && \
    dpkg-buildpackage -us -uc -b"
else
  docker run --rm --platform=$PLATFORM \
    -v "$PWD":/build \
    -v "$LGPIO_DEB_DIR":/debs:ro \
    -w /build $IMAGE_TAG bash -c "\
    dpkg -i /debs/*.deb && \
    cd build/$COMPONENT/source && \
    $LIRC_ENV \
    export CXXFLAGS='-O1 -Wno-psabi' && \
    export DEB_BUILD_MAINT_OPTIONS='hardening=+all optimize=-lto' && \
    dpkg-buildpackage -us -uc -b"
fi

mkdir -p out/$ARCH
find build/$COMPONENT -maxdepth 1 -type f -name '*.deb' -exec mv {} out/$ARCH/ \;

if [[ "$MODE" == "volumio" ]]; then
  echo "[+] Volumio mode: Renaming .deb packages for custom suffixes..."
  for f in out/$ARCH/*.deb; do
    if [[ "$f" == *_all.deb ]]; then
      echo "[VERBOSE] Skipping _all.deb file: $f"
      continue
    fi

    base_name=$(basename "$f")
    newf="$f"

    case "$ARCH" in
      armv6)
        newf="${f/_armhf.deb/_arm.deb}"
        if [[ "$f" != "$newf" ]]; then
          echo "[VERBOSE] Renaming $f to $newf (ARMv6/7 target VFP2 - hard-float)"
        fi
        ;;
      armhf)
        newf="${f/_armhf.deb/_armv7.deb}"
        if [[ "$f" != "$newf" ]]; then
          echo "[VERBOSE] Renaming $f to $newf (ARMv7 target)"
        fi
        ;;
      arm64)
        newf="${f/_arm64.deb/_armv8.deb}"
        if [[ "$f" != "$newf" ]]; then
          echo "[VERBOSE] Renaming $f to $newf (ARMv8 target - 64-bit)"
        fi
        ;;
      amd64)
        newf="${f/_amd64.deb/_x64.deb}"
        if [[ "$f" != "$newf" ]]; then
          echo "[VERBOSE] Renaming $f to $newf (x86_64 target)"
        fi
        ;;
      *)
        newf="$f"
        ;;
    esac

    if [[ "$f" != "$newf" ]]; then
      mv "$f" "$newf"
    fi
  done
fi

echo "[OK] Build complete. Packages are in out/$ARCH/"

#!/bin/bash
# allo-relay-builds scripts/build-rattenu.sh
# Build script for relay attenuator binaries (runs inside Docker container)

set -e

echo "[+] Starting relay attenuator build"
echo "[+] Architecture: $ARCH"
echo "[+] Library path: $LIB_PATH"
echo "[+] Extra CFLAGS: $EXTRA_CFLAGS"
echo "[+] LIRC support: $([ "$NO_LIRC" -eq 0 ] && echo "enabled" || echo "disabled")"
echo ""

# Install lgpio DEBs from mounted directory
echo "[+] Installing lgpio DEBs from /debs..."
DEB_COUNT=$(ls /debs/*.deb 2>/dev/null | wc -l)
if [ "$DEB_COUNT" -eq 0 ]; then
  echo "Error: No DEB files found in /debs"
  exit 1
fi

dpkg -i /debs/*.deb || true
apt-get install -f -y

# Verify installation
if ! pkg-config --exists libfn-lgpio; then
  echo "Error: libfn-lgpio not found after DEB installation"
  echo "Available packages:"
  dpkg -l | grep lgpio
  exit 1
fi

echo "[+] lgpio library installed:"
pkg-config --modversion libfn-lgpio
pkg-config --cflags libfn-lgpio
pkg-config --libs libfn-lgpio
echo ""

# Directories
BUILD_DIR="/build/work"
SOURCE_DIR="/build/source"
OUTPUT_DIR="/build/output"

mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

# Copy source to build directory
cp -r "$SOURCE_DIR"/* "$BUILD_DIR/"
cd "$BUILD_DIR"

#
# Determine build flags
#
LGPIO_CFLAGS=$(pkg-config --cflags libfn-lgpio)
LGPIO_LIBS=$(pkg-config --libs libfn-lgpio)

COMMON_CFLAGS="-Wall -O2 $EXTRA_CFLAGS $LGPIO_CFLAGS"
COMMON_LDFLAGS="-static-libgcc"

if [ "$NO_LIRC" -eq 0 ]; then
  # Build with LIRC support
  LIRC_CFLAGS=""
  LIRC_LIBS="-llirc_client"
  DAEMON_DEFINES=""
else
  # Build without LIRC support
  LIRC_CFLAGS=""
  LIRC_LIBS=""
  DAEMON_DEFINES="-DNO_LIRC"
fi

echo "[+] Build configuration:"
echo "    COMMON_CFLAGS: $COMMON_CFLAGS"
echo "    LGPIO_LIBS: $LGPIO_LIBS"
echo "    LIRC_LIBS: $LIRC_LIBS"
echo "    DAEMON_DEFINES: $DAEMON_DEFINES"
echo ""

#
# Build fn-rattenu (daemon)
#
echo "[+] Building fn-rattenu..."

gcc $COMMON_CFLAGS $DAEMON_DEFINES \
    -o fn-rattenu r_attenu.c \
    $COMMON_LDFLAGS $LGPIO_LIBS $LIRC_LIBS -lpthread

echo "[+] fn-rattenu built"

#
# Build fn-rattenuc (client)
#
echo "[+] Building fn-rattenuc..."

gcc $COMMON_CFLAGS \
    -o fn-rattenuc r_attenuc.c \
    $COMMON_LDFLAGS

echo "[+] fn-rattenuc built"

#
# Strip binaries
#
echo ""
echo "[+] Stripping binaries..."
strip fn-rattenu
strip fn-rattenuc

#
# Verify binaries
#
echo ""
echo "[+] Verifying binaries..."

echo "fn-rattenu:"
file fn-rattenu
ldd fn-rattenu 2>/dev/null || echo "  (statically linked or minimal dependencies)"

echo ""
echo "fn-rattenuc:"
file fn-rattenuc
ldd fn-rattenuc 2>/dev/null || echo "  (statically linked or minimal dependencies)"

#
# Copy to output
#
echo ""
echo "[+] Copying to output..."
cp fn-rattenu "$OUTPUT_DIR/"
cp fn-rattenuc "$OUTPUT_DIR/"

echo ""
echo "[+] Build complete"
ls -lh "$OUTPUT_DIR"/fn-rattenu*

#!/bin/bash
set -e

echo "[+] Cleaning build directory..."
rm -rf build/

echo "[+] Cleaning output directories..."
rm -f out/armv6/*.deb
rm -f out/armhf/*.deb
rm -f out/arm64/*.deb
rm -f out/amd64/*.deb

echo "[OK] Clean complete."

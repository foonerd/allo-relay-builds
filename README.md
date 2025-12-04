# allo-relay-builds

Docker-based build system for Allo Relay Attenuator binaries targeting Volumio 4.x (Bookworm).

## Overview

Builds fn-rattenu (daemon) and fn-rattenuc (client) binaries for Allo Relay Volume Attenuator hardware.
Ported from WiringPi to lgpio for Raspberry Pi OS Bookworm compatibility.

## Output Package

- foonerd-rattenu - Contains fn-rattenu daemon and fn-rattenuc client

## Prerequisites

1. Docker with buildx and QEMU for cross-compilation
2. Built lgpio DEBs from lgpio-builds repository

## Build Commands

Build all architectures for Volumio:
```bash
./build-matrix.sh --lgpio=../lgpio-builds --volumio
```

Build without LIRC support:
```bash
./build-matrix.sh --lgpio=../lgpio-builds --volumio --no-lirc
```

Build single architecture:
```bash
./scripts/extract.sh
./docker/run-docker-rattenu.sh rattenu arm64 volumio --verbose --lgpio=../lgpio-builds
```

## Output Files

With --volumio flag:
```
out/armv6/foonerd-rattenu_2.0.0-1_arm.deb
out/armhf/foonerd-rattenu_2.0.0-1_armv7.deb
out/arm64/foonerd-rattenu_2.0.0-1_armv8.deb
out/amd64/foonerd-rattenu_2.0.0-1_x64.deb
```

## Architecture Mapping

| Build Arch | Docker Platform | Volumio Suffix |
|------------|-----------------|----------------|
| armv6      | linux/arm/v7    | _arm.deb       |
| armhf      | linux/arm/v7    | _armv7.deb     |
| arm64      | linux/arm64     | _armv8.deb     |
| amd64      | linux/amd64     | _x64.deb       |

## Binary Usage

Daemon:
```bash
fn-rattenu -d              # Run as daemon with LIRC
fn-rattenu -d -l           # Run without LIRC
fn-rattenu -d -n myname    # Custom LIRC program name
fn-rattenu -d -c /path     # Custom LIRC config path
```

Client:
```bash
fn-rattenuc -c GET_VOLUME        # Returns 0-63
fn-rattenuc -c SET_VOLUME=32     # Set volume
fn-rattenuc -c GET_MUTE          # Returns 0 or 1
fn-rattenuc -c SET_MUTE=1        # Mute
```

## Hardware Requirements

- Allo Relay Attenuator board
- I2C enabled (dtparam=i2c_arm=on in /boot/config.txt)
- I2C addresses: 0x20 (switch), 0x21 (relay)
- GPIO BCM 5 for button interrupt

## License

MIT License. Original sources by Allo.com. lgpio is public domain.

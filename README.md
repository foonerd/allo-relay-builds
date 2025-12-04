# allo-relay-builds

Docker-based cross-compilation build system for Allo Relay Attenuator binaries.

Produces statically linked binaries for Volumio plugin integration.

## Overview

The Allo Relay Attenuator is a high-quality relay-based volume control for
audiophile applications. This build system produces binaries compatible with
Raspberry Pi OS Bookworm and Volumio 4.x.

- **Hardware**: Allo Relay Attenuator board
- **Interface**: I2C (addresses 0x20 switch, 0x21 relay)
- **Volume range**: 0-63 (6-bit)
- **Optional**: LIRC IR remote support

## Output

Builds produce two binaries for each architecture:

| Binary | Description |
|--------|-------------|
| fn-rattenu | Relay attenuator daemon (I2C control, optional LIRC) |
| fn-rattenuc | Client utility (get/set volume and mute) |

Supported architectures:

| Architecture | Platform | Notes |
|--------------|----------|-------|
| armv6 | Pi Zero/1 | ARMv6 optimized |
| armhf | Pi 2/3 | ARMv7 with NEON |
| arm64 | Pi 4/5 | ARMv8-A |
| amd64 | x86_64 | For testing |

## Requirements

- Docker with multi-architecture support (buildx)
- QEMU for cross-architecture emulation
- lgpio-builds DEBs (sibling repository)

Setup on Debian/Ubuntu:
```bash
sudo apt-get install docker.io qemu-user-static binfmt-support
sudo systemctl enable --now docker
```

## Dependencies

Before building, you need lgpio DEBs from the lgpio-builds repository:

```
../lgpio-builds/
  out/
    armv6/libfn-lgpio*.deb
    armhf/libfn-lgpio*.deb
    arm64/libfn-lgpio*.deb
    amd64/libfn-lgpio*.deb
```

Build lgpio first:
```bash
cd ../lgpio-builds
./build-matrix.sh
```

## Building

Build all architectures:
```bash
./build-matrix.sh --lgpio=../lgpio-builds
```

Build single architecture:
```bash
./docker/run-docker-rattenu.sh arm64 --lgpio=../lgpio-builds
./docker/run-docker-rattenu.sh armhf --lgpio=../lgpio-builds --verbose
```

Build without LIRC support:
```bash
./build-matrix.sh --lgpio=../lgpio-builds --no-lirc
```

## Output Structure

```
out/
  armv6/
    fn-rattenu
    fn-rattenuc
  armhf/
    fn-rattenu
    fn-rattenuc
  arm64/
    fn-rattenu
    fn-rattenuc
  amd64/
    fn-rattenu
    fn-rattenuc
```

## Binary Usage

### fn-rattenu (daemon)

```bash
# Run with LIRC support
fn-rattenu -d

# Run without LIRC (hardware buttons only)
fn-rattenu -d -l

# Options:
#   -d          Run as daemon
#   -l          Disable LIRC (run without IR remote)
#   -n name     Program name for lircrc matching
#   -c config   LIRC config file path
#   -h          Help
#   -v          Version
```

### fn-rattenuc (client)

```bash
# Get current volume (0-63)
fn-rattenuc -c GET_VOLUME

# Set volume
fn-rattenuc -c SET_VOLUME=32

# Get mute status (0=unmuted, 1=muted)
fn-rattenuc -c GET_MUTE

# Set mute
fn-rattenuc -c SET_MUTE=1    # mute
fn-rattenuc -c SET_MUTE=0    # unmute
```

## Hardware Configuration

The relay attenuator uses I2C bus 1:
- Switch input: address 0x20
- Relay output: address 0x21
- GPIO interrupt: pin 5 (BCM)

Ensure I2C is enabled in `/boot/config.txt`:
```
dtparam=i2c_arm=on
```

## Volumio Plugin Integration

The binaries are designed for use with the Volumio relay attenuator plugin:

```
/data/plugins/miscellanea/allo_relay_volume_attenuator/
  fn-rattenu
  fn-rattenuc
  setvolume.sh
  getvolume.sh
  setmute.sh
  getmute.sh
```

## Cleaning

Remove all build artifacts:
```bash
./clean-all.sh
```

## License

Build scripts: MIT License
Original relay code: Based on Allo RelayAttenuator
lgpio library: Public Domain (joan2937)

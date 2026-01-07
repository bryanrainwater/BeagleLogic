# PulseView Installation Scripts

This directory contains automated installation scripts for building PulseView with BeagleLogic support from source.

## Quick Start

### Linux / WSL

```bash
./install-linux.sh
```

**Tested on:**
- Ubuntu 22.04+
- Debian 11+
- WSL2 (Ubuntu)

**Time:** 20-30 minutes

### macOS

```bash
./install-macos.sh
```

**Not properly tested yet on Mac**
- macOS Monterey (12.x)+
- macOS Ventura (13.x)+
- Both Intel and Apple Silicon

**Time:** 25-35 minutes

## What Gets Installed

All scripts build and install the following components from source:

1. **libserialport** - Serial port library
2. **libsigrok** - Hardware abstraction library with:
   - C++ bindings enabled
   - BeagleLogic driver enabled
3. **libsigrokdecode** - Protocol decoder library
4. **PulseView** - GUI application

## Why Build from Source?

Pre-built PulseView packages don't include the BeagleLogic driver. Building from source ensures:

- ✅ BeagleLogic TCP network driver support
- ✅ Latest features and bug fixes
- ✅ C++ bindings for custom development
- ✅ All protocol decoders included

## Installation Location

- **Linux/WSL**: `/usr/local`
- **macOS**: `/usr/local` (or `/opt/homebrew` for dependencies)

## After Installation

### Verify Installation

```bash
# Check PulseView is installed
which pulseview

# Verify BeagleLogic driver
sigrok-cli --list-supported | grep beaglelogic
```

Expected output:
```
beaglelogic - BeagleLogic
```

### Connect to BeagleLogic

1. Ensure BeagleLogic TCP server is running on your BeagleBone (port 5555)
2. Launch PulseView: `pulseview`
3. Click device dropdown → "Connect to remote device"
4. Enter connection: `tcp/192.168.7.2/5555` (or alternate IP if over eth0)
5. Click "Scan for devices"
6. Select BeagleLogic and start capturing!

## Requirements

### Linux/WSL

- Ubuntu 22.04+ or Debian 11+
- `sudo` access
- Internet connection
- ~2 GB free disk space

### macOS

- macOS 12.0+ (Monterey or later)
- Homebrew (will be installed if missing)
- Xcode Command Line Tools
- Internet connection
- ~2 GB free disk space

## Clean Rebuild

If installation fails or you want to start fresh:

```bash
# Remove build artifacts
rm -rf ~/sigrok-build

# Remove installed libraries (optional)
sudo rm -rf /usr/local/lib/libsigrok*
sudo rm -rf /usr/local/include/libsigrok*

# Re-run installation script
./install-linux.sh  # or ./install-macos.sh
```

## Common Issues

### Linux: "C++ bindings not enabled"

**Solution:**
```bash
sudo apt-get install -y libglibmm-2.4-dev python3-gi python3-gi-cairo swig g++
```

### macOS: "Qt5 not found" (be wary, not properly tested)

**Solution:**
```bash
brew install qt@5
export PATH="/usr/local/opt/qt@5/bin:$PATH"
```

## Support

For detailed installation guides and troubleshooting:
- See [docs/03-pulseview-setup.md](../docs/03-pulseview-setup.md)
- Check existing [GitHub Issues](https://github.com/abhishek-kakkar/BeagleLogic/issues)

## License

These scripts are part of the BeagleLogic project and are provided under the same license as the main project.

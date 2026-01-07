# BeagleLogic Installation Guide

Complete installation instructions for BeagleLogic on BeagleBone Black running Debian 13 (Trixie) with Linux kernel 6.x.

Tested and verified on kernel 6.12 and 6.17; earlier 6.x versions may also work but are untested.

---

## Prerequisites

### Hardware
- BeagleBone Black (Rev C or later)
- PRUDAQ Cape (optional, for ADC functionality)
- 5V 2A power supply
- MicroSD card: 8GB minimum (recommended for easier installation)
- Network connection

### Software
- Debian 13 (Trixie)
- Linux Kernel 6.x (tested on 6.12 and 6.17)
- Root/sudo access
- Internet connection

### Disk Space

Installation size depends on components selected:

| Configuration | Disk Space | Storage Type | Components |
|--------------|------------|--------------|------------|
| **Minimal** | ~850 MB | eMMC or MicroSD | Core + TCP server (PulseView) |
| **Standard** | ~1.3 GB | MicroSD recommended | Core + TCP + Web interface |
| **Full** | ~1.8 GB | MicroSD required | All components + documentation |

Check available space before installing:
```bash
df -h .
```

**eMMC users (4GB):** Select Minimal installation only
**MicroSD users (8GB+):** Can choose any configuration

---

## Installation

### 1. Get the Code

Clone to `/opt/BeagleLogic` (recommended for consistency with earlier versions):

```bash
cd /opt
sudo git clone https://github.com/bryanrainwater/BeagleLogic.git BeagleLogic
# OR: sudo git clone https://github.com/abhishek-kakkar/BeagleLogic.git BeagleLogic

sudo chown -R debian:debian BeagleLogic
cd BeagleLogic
```

**Alternative:** Clone to any directory you prefer (home directory, external storage, etc.)

### 2. Run Installation Script

```bash
sudo ./beaglelogic_setup.sh --install
```

The script will:
- Check system compatibility
- Prompt for component selection (Web server, TCP server, Documentation)
- Install dependencies and build all components
- Configure auto-start services
- Prompt for reboot when complete

### 3. Reboot

```bash
sudo reboot
```

### 4. Verify Installation

After reboot:

```bash
# Kernel module loaded?
lsmod | grep beaglelogic

# Device created?
ls -l /dev/beaglelogic

# Services running?
systemctl status beaglelogic-startup.service

# Optional services (if you installed them):
systemctl status beaglelogic.service        # Web interface (port 4000)
systemctl status beaglelogic-tcp.service    # TCP server (port 5555)
systemctl status beaglelogic-docs.service   # Documentation (port 8000)
```

Or use the built-in status check:
```bash
sudo ./beaglelogic_setup.sh --status
```

---

## What Gets Installed

The installation script builds and installs:

1. **System Dependencies** - Build tools, kernel headers, PRU compiler
2. **PRU Software Support** - TI PRU support package (`/usr/lib/ti/pru-software-support-package/`)
3. **PRU Firmware** - Compiled and installed to `/lib/firmware/beaglelogic-pru*`
4. **Kernel Module** - `beaglelogic.ko` in `/lib/modules/$(uname -r)/extra/`
5. **Device Tree Overlay** - `beaglelogic-00A0.dtbo` in `/lib/firmware/`
6. **Test Applications** - Built in `testapp/` directory
7. **Web Server** (optional) - Node.js app on port 4000
8. **TCP Server** (optional) - Node.js server on port 5555 for PulseView
9. **Documentation** (optional) - Local MkDocs site
10. **System Integration** - Services, udev rules, auto-load configuration

All components are configured to start automatically at boot.

---

## Post-Installation Verification

### Quick Test

```bash
# Check kernel module
lsmod | grep beaglelogic
# Should show: beaglelogic

# Check device node
ls -l /dev/beaglelogic
# Should show: crw-rw---- 1 root beaglelogic

# Check PRU firmware loaded
cat /sys/class/remoteproc/remoteproc1/state
cat /sys/class/remoteproc/remoteproc2/state
# Both should show: running

# Quick capture test
# Find your BeagleLogic installation:
BEAGLELOGIC_PATH=$(grep "^BEAGLELOGIC_PATH=" /etc/beaglelogic/install.conf | cut -d'=' -f2- | tr -d '"')
cd ${BEAGLELOGIC_PATH}/testapp
sudo ./beaglelogic-testapp
# Select option 1 (Simple Capture) to test
```

### Access Points

After installation, the following services are available:

| Service | URL/Location | Port | Notes |
|---------|-------------|------|-------|
| Web Interface | `http://192.168.7.2:4000/` | 4000 | Optional - if installed |
| TCP Server (PulseView) | `tcp://192.168.7.2:5555` | 5555 | Optional - if installed |
| Documentation Server | `http://192.168.7.2:8000/` | 8000 | Optional - if installed |
| Device Node | `/dev/beaglelogic` | N/A | Always available |
| Installation Log | `/var/log/beaglelogic-install.log` | N/A | Always available |

**Note:** Optional services are only available if you selected them during installation.

---

## Troubleshooting

### Installation Failed

Check the installation log:
```bash
sudo cat /var/log/beaglelogic-install.log
```

Resume installation (script tracks state):
```bash
sudo ./beaglelogic_setup.sh --install
```

### Module Won't Load

```bash
# Check for errors
dmesg | grep beaglelogic
dmesg | grep error

# Try manual load
sudo modprobe beaglelogic
```

### Device Not Appearing

```bash
# Check module loaded
lsmod | grep beaglelogic

# Check overlay loaded
ls /proc/device-tree/chosen/overlays/

# Check U-Boot config
grep beaglelogic /boot/uEnv.txt
```

### Services Not Starting

```bash
# Check service status
systemctl status beaglelogic.service
journalctl -u beaglelogic.service -n 50

# Find installation path
BEAGLELOGIC_PATH=$(grep "^BEAGLELOGIC_PATH=" /etc/beaglelogic/install.conf | cut -d'=' -f2- | tr -d '"')

# Check dependencies installed
ls ${BEAGLELOGIC_PATH}/server/node_modules/
```

For more troubleshooting, see [docs/09-troubleshooting.md](docs/09-troubleshooting.md).

---

## Configuration

### Switching Firmware Modes

To switch between Logic Analyzer and PRUDAQ ADC modes:

```bash
# Find your BeagleLogic installation
BEAGLELOGIC_PATH=$(grep "^BEAGLELOGIC_PATH=" /etc/beaglelogic/install.conf | cut -d'=' -f2- | tr -d '"')
cd ${BEAGLELOGIC_PATH}

# Run configuration
sudo ./beaglelogic_setup.sh --configure
```

Available modes:
- Logic Analyzer (Standard 14-channel) - Default
- PRUDAQ ADC - Channel 0 only
- PRUDAQ ADC - Channel 1 only
- PRUDAQ ADC - Both Channels

### Managing Services

Available systemd services:
- `beaglelogic-startup.service` - Core initialization (always installed)
- `beaglelogic.service` - Web interface on port 4000 (optional)
- `beaglelogic-tcp.service` - TCP server on port 5555 (optional)
- `beaglelogic-docs.service` - Documentation on port 8000 (optional)

```bash
# Control services (replace SERVICE_NAME with specific service)
sudo systemctl start SERVICE_NAME
sudo systemctl stop SERVICE_NAME
sudo systemctl restart SERVICE_NAME
sudo systemctl status SERVICE_NAME

# Examples:
sudo systemctl restart beaglelogic.service      # Restart web interface
sudo systemctl status beaglelogic-tcp.service   # Check TCP server status

# Enable/disable auto-start
sudo systemctl enable beaglelogic.service
sudo systemctl disable beaglelogic.service

# View logs
journalctl -u beaglelogic.service -f
journalctl -u beaglelogic-tcp.service -n 50
```

---

## Uninstallation

```bash
# Stop and disable services
sudo systemctl stop beaglelogic*.service
sudo systemctl disable beaglelogic*.service

# Remove kernel module
sudo rmmod beaglelogic

# Remove installed files
sudo rm /lib/systemd/system/beaglelogic*.service
sudo rm /lib/modules/$(uname -r)/extra/beaglelogic.ko
sudo rm /lib/firmware/beaglelogic-*
sudo rm /etc/modules-load.d/beaglelogic.conf
sudo rm /etc/udev/rules.d/90-beaglelogic.rules
sudo rm -f /etc/beaglelogic/install.conf

# Edit /boot/uEnv.txt to remove BeagleLogic overlay line
# Then reboot
sudo reboot
```

**Note:** The BeagleLogic source directory is NOT automatically removed. Remove manually if desired.

---

## Support

- **Installation Log:** `/var/log/beaglelogic-install.log`
- **Quick Commands:** Run `sudo ./beaglelogic_setup.sh --status` to check installation status
- **Getting Started:** [docs/02-getting-started.md](docs/02-getting-started.md)
- **Troubleshooting:** [docs/09-troubleshooting.md](docs/09-troubleshooting.md)

---

**Last Updated:** January 2026

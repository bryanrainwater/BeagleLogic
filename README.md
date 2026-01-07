![logo](logo.png)
===========

## Kernel 6.x Port

This is an updated version of BeagleLogic ported from kernel 4.9 to **Linux kernel 6.x**.

**Testing Status:**
- Kernel module, PRU firmware, and device tree: **Tested on kernel 6.x (6.12, 6.17)**
- Web interface and PulseView: **Tested and working**
- Install scripts and testapp: **Updated and tested for kernel 6.x**

**Original BeagleLogic Project:**
- Original Author: Kumar Abhishek
- Original Repository: https://github.com/abhishek-kakkar/BeagleLogic
- Bootstrapped as a Google Summer of Code 2014 Project with BeagleBoard.org

**Kernel 6.x Port:**
- Port Author: Bryan Rainwater
- Testing Environment: Debian Trixie, Kernel 6.12/6.17

---

BeagleLogic realizes a logic analyzer on the BeagleBone / the BeagleBone Black using
the Programmable Real-Time units and matching firmware and Linux kernel modules on the
BeagleBone Black.

The software should also work on the BeagleBone White, but with limited memory support
(only 256 MB instead of 512 MB)

BeagleLogic can also be used in conjunction with the sigrok projects to capture and process
the data on the BeagleBone itself. The libsigrok bindings for BeagleLogic have been accepted
into the upstream libsigrok repository.

For detailed information and usage guide refer to
[the project wiki](https://github.com/abhishek-kakkar/BeagleLogic/wiki)

Directories:
* firmware: PRU Firmware (updated for kernel 6.x)
* kernel: Device Tree overlay source and kernel module source and Makefile (updated for kernel 6.x)
* server: Node.JS backend and static file server for the web interface
* webapp: A minimal web client for BeagleLogic. Uses sigrok-cli internally for data acquisition
* testapp: Comprehensive test application suite with interactive testing modes
* tcp-server-node: TCP server for PulseView integration
* scripts: Service files and startup scripts (updated for kernel 6.x)
* pulseview: Installation scripts for PulseView on Linux and macOS

**Note:** For kernel 6.x, you may need to increase DMA memory pools. Add to `/boot/firmware/uEnv.txt`:
```
coherent_pool=192M cma=320M
```

## Quick Start

For complete installation instructions, see [INSTALLATION_GUIDE.md](INSTALLATION_GUIDE.md)

```bash
# Clone the repository
cd /opt
sudo git clone https://github.com/abhishek-kakkar/BeagleLogic.git
cd BeagleLogic

# Run installation (interactive - will prompt for components)
sudo ./beaglelogic_setup.sh --install

# Reboot when installation completes
sudo reboot

# After reboot, verify
lsmod | grep beaglelogic
ls -l /dev/beaglelogic
```

---

License
--------

 * **PRU firmware & Device tree overlay** : GPLv2
 * **Kernel Module**: GPLv2
 * **Web interface** [/server and /webapp]: MIT

See [CONTRIBUTORS.md](CONTRIBUTORS.md) for the list of contributors.

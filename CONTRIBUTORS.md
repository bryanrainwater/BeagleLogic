# Contributors

This file lists all individuals who have contributed to the BeagleLogic project.

---

## Original Author

### Kumar Abhishek
**Email:** abhishek@theembeddedkitchen.net
**Years Active:** 2014-2020

**Contributions:**
- Original BeagleLogic kernel driver architecture and implementation
- PRU firmware for high-speed GPIO sampling (PRU0 and PRU1)
- Device tree overlay configuration
- DMA buffer management system
- Interrupt handling framework
- Sysfs attribute interface
- Character device file operations
- Documentation and examples

**Notable Work:**
- Created the scatter-gather DMA list system for PRU0
- Implemented 100MHz sampling using PRU assembly
- Designed the buffer state machine for ping-pong operation
- Developed the shared memory protocol between ARM and PRU cores

---

## Kernel 6.x Port

### Bryan Rainwater
**Years Active:** 2024-2026

**Contributions:**
- Updated kernel driver for Linux 6.x compatibility
- Migrated DMA management to coherent memory API
- Fixed buffer state machine bugs
- Implemented proper Ctrl+C signal handling
- Updated PRU interrupt handling for kernel 6.x
- Removed deprecated API calls (pruss_intc_trigger)
- Implemented PRU SRAM stop_flag mechanism for PRU control
- Modernized DMA mask configuration
- Updated device tree overlay for kernel 6.x
- Cleaned up interrupt routing configuration
- Added DMA device tree properties
- Enhanced documentation and troubleshooting guides
- Created comprehensive CHANGELOG
- Added detailed code comments

---

## License

All contributions to this project must comply with the project's licensing terms. See [LICENSE](LICENSE) for complete details.

---

## Acknowledgments

### Project Mentorship & Leadership
- **Jason Kridner** - BeagleBoard.org founder, Google Summer of Code 2014 mentor for original BeagleLogic project

### Hardware & Infrastructure
- **Texas Instruments** - PRU-ICSS subsystem, remoteproc framework, and development tools
- **BeagleBoard.org Foundation** - BeagleBone hardware platform and GSoC 2014 organizational support
- **Google / BeagleBoard.org** - PRUDAQ ADC cape design

### Software & Tools
- **Linux Kernel Community** - DMA API, remoteproc, and device tree infrastructure
- **TI PRU Software Support Package Team** - PRU compiler and support libraries

### Documentation & Support
- **Beagleboard.org Community** - Forums, wiki, and troubleshooting assistance
- **Linux Device Driver Community** - Best practices and code review

---

## Contact

For questions about contributions or the project in general:

- **Original Author:** Kumar Abhishek <abhishek@theembeddedkitchen.net>
- **Kernel 6.x Port:** Bryan Rainwater

---

Last updated: January 2026

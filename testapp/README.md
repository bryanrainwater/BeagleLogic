# BeagleLogic Test Application

Interactive test application demonstrating the BeagleLogic API.

---

## Quick Start

```bash
# Build the application
make

# Run (interactive - it will guide you!)
sudo ./beaglelogic-testapp
```

---

## Application Modes

The application provides 8 interactive testing modes:

### Basic Modes
1. **Simple Capture** - Basic data capture to file with configurable size and sample rate
2. **Continuous Logger** - Long-running capture with automatic file rotation (10 MB chunks)
3. **PRUDAQ ADC Capture** - 12-bit ADC capture with CSV output (requires PRUDAQ firmware)

### Educational Modes (Code Examples) - NOTE: currently being developed
4. **Continuous Blocking** - Demonstrates blocking read() with Ctrl+C to stop
5. **Continuous Poll** - Demonstrates non-blocking poll() with Enter to stop
6. **Oneshot Visual** - Terminal waveform visualization with multi-channel timing analysis

### Advanced
7. **Performance Test** - Benchmark throughput and reliability with detailed statistics
8. **Diagnostic Tests** - Comprehensive driver testing suite (13 tests available)

Plus **About** (option 9) for application information.

The application is fully interactive - just run it and follow the prompts!

---

## Building

```bash
# Build the application
make

# Clean
make clean

# Install to /usr/local/bin
sudo make install

# Uninstall
sudo make uninstall
```

---

## Educational Modes

Modes 4-6 are designed as **code examples** that demonstrate different programming patterns for BeagleLogic:

- **Mode 4 (Continuous Blocking)** - Shows simple blocking `read()` calls in a loop. Stop with Ctrl+C. Great starting point for understanding basic capture.

- **Mode 5 (Continuous Poll)** - Demonstrates non-blocking I/O with `poll()` for event-driven capture. Stop by pressing Enter. Shows proper handling of `EAGAIN` and file descriptor polling.

- **Mode 6 (Oneshot Visual)** - Quick signal verification with terminal-based waveform visualization. Displays timing grids, channel statistics, and ASCII waveforms for debugging signal connections.

These modes are educational references - check the source code to see implementation patterns you can adapt for your own applications.

---

## API Library

The application uses the BeagleLogic userspace library:

- **beaglelogic.c** - API implementation
- **libbeaglelogic.h** - API header

### Basic API Usage

```c
#include "libbeaglelogic.h"

// Open device
int fd = beaglelogic_open();

// Configure
beaglelogic_set_samplerate(fd, 1000000);  // 1 MHz
beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_CONTINUOUS);

// Capture (starts automatically on first read)
uint8_t buffer[1024];
ssize_t bytes_read = read(fd, buffer, sizeof(buffer));

// Close
beaglelogic_close(fd);
```

### Using mmap (Zero-Copy)

```c
void *mapped_buffer = beaglelogic_mmap(fd);
if (mapped_buffer) {
    // Access data directly without copying
    uint8_t *data = (uint8_t *)mapped_buffer;

    // Unmap when done
    beaglelogic_munmap(fd, mapped_buffer);
}
```

See [libbeaglelogic.h](libbeaglelogic.h) for complete API reference.

---

## Diagnostic Tests

Access via **mode 8** in the main menu. The application includes 13 comprehensive diagnostic tests:

**Basic Tests (1-3):** Quick driver validation
1. Open/close device
2. Configure parameters
3. mmap functionality

**Comprehensive Tests (4-10):** In-depth testing
4. Cache read test
5. Blocking read test
6. Non-blocking read test
7. Start/stop test
8. Poll test
9. Nonblock+poll test
10. 10-second stress test

**Advanced Tests (11-13):** Stress tests (32MB buffers)
11. Large buffer cache test
12. Large buffer read test
13. Cache invalidate after capture

**Test Suites Available:**
- **Quick Diagnostics** (tests 1-3) - Fast validation
- **Comprehensive Suite** (tests 1-10) - Thorough testing
- **Full Suite** (ALL tests 1-13) - Complete validation

All tests log to `/tmp/beaglelogic-test.log` and check dmesg for kernel errors.

---

## PRUDAQ ADC Mode

To use PRUDAQ ADC capture (**mode 3** in the main menu), first load PRUDAQ firmware
by switching modes to the PRUDAQ mode using the "beaglelogic_setup.sh" script.
```

---

## Analyzing Captured Data

### Quick Hexdump View

```bash
# Basic hex view (shows 16 bytes per line)
xxd capture.bin | head -20

# Binary view (shows individual bits)
xxd -b -c 1 capture.bin | head -20
```

### Visualizing Signal Patterns

#### Single Bit Column View

Extract and display bit 0 (channel 0 / P8.45) in a column:

```bash
# Show bit 0, one sample per line (easy to see transitions)
xxd -b -c 1 capture.bin | head -40 | awk '{print substr($2, 8, 1)}'
```

Output shows signal state vertically:
```
1  ← HIGH
1
0  ← LOW
0
0
1  ← HIGH
...
```

#### Grid View - Pattern Recognition

Display bit 0 in a grid (16 samples per row):

```bash
# Grid view (best for seeing repeating patterns)
od -An -v -tu1 -w16 capture.bin | head -20 | awk '{for(i=1;i<=NF;i++) printf "%d ", $i % 2; print ""}'
```

Output:
```
1 1 0 0 0 1 1 1 0 0 0 1 1 1 0 0
0 1 1 1 0 0 0 1 1 1 0 0 0 1 1 1
```

Pattern is now visible across each row!

#### ASCII Waveform - Visual Verification

Create a visual "waveform" of your signal:

```bash
# ASCII waveform (█ = HIGH, ░ = LOW)
od -An -v -tu1 capture.bin | awk '{for(i=1;i<=NF;i++) printf ($i % 2 ? "█" : "░")}' | head -c 160 && echo
```

Output:
```
██░░░███░░░███░░░███░░░███░░░███░░░███░░░
```

You can literally see the square wave!

#### Multi-Bit View - All Channels

Show all 8 channels at once:

```bash
# Display all bits in binary format
xxd -b -c 1 capture.bin | head -20 | awk '{print $2}'
```

Output shows each byte as 8 bits:
```
00000001  ← bit 7..0 (rightmost = channel 0/P8.45)
00000001
00000000
00000000
```

### Analyzing Continuous Captures

These same techniques work with data from `/dev/beaglelogic` in continuous mode:

```bash
# Capture 1 KB and visualize immediately
sudo dd if=/dev/beaglelogic bs=1024 count=1 2>/dev/null | \
  od -An -v -tu1 | \
  awk '{for(i=1;i<=NF;i++) printf ($i % 2 ? "█" : "░")}' | \
  head -c 80 && echo

# Monitor live signal (updates every second)
watch -n 1 'sudo dd if=/dev/beaglelogic bs=1024 count=1 2>/dev/null | \
  od -An -v -tu1 | \
  awk "{for(i=1;i<=NF;i++) printf (\$i % 2 ? \"█\" : \"░\")}" | \
  head -c 80'
```

### Python Analysis (Advanced). Note: not extensively tested

For more complex analysis:

```bash
# Waveform view
python3 -c "
import sys
data = open('capture.bin', 'rb').read(160)
print(''.join('█' if b&1 else '░' for b in data))
"

# Grid view with all 8 channels
python3 -c "
data = open('capture.bin', 'rb').read(160)
for i, byte in enumerate(data[:32]):
    print(f'{i:3d}: {byte:08b}')
"

# Statistics
python3 -c "
data = open('capture.bin', 'rb').read()
bit0 = [b&1 for b in data]
print(f'Total samples: {len(bit0)}')
print(f'HIGH samples: {sum(bit0)} ({100*sum(bit0)/len(bit0):.1f}%)')
print(f'LOW samples: {len(bit0)-sum(bit0)} ({100*(len(bit0)-sum(bit0))/len(bit0):.1f}%)')
"
```

### Important Notes

- **Sample Rate Matters**: Capture at ≥5× your signal frequency (10× recommended)
  - 1 MHz signal → use 10 MHz sample rate minimum
  - Otherwise you'll see aliasing and incorrect patterns

- **Bit Mapping**:
  - Bit 0 (LSB) = P8.45 (Channel 0)
  - Bit 1 = P8.46 (Channel 1)
  - See [Getting Started Guide](../docs/02-getting-started.md) for full pin mapping

- **File Size Check**:
  ```bash
  ls -lh capture.bin
  # Should match your capture size setting
  ```

---

## Troubleshooting

**Permission denied:**
```bash
# Add user to beaglelogic group
sudo usermod -aG beaglelogic $USER
# Log out and back in

# Or run with sudo
sudo ./beaglelogic-testapp
```

**Device not found:**
```bash
# Check kernel module loaded
lsmod | grep beaglelogic

# Check device exists
ls -l /dev/beaglelogic
```

**All zeros captured:**
```bash
# Test with internal pattern
echo 1 > /sys/devices/virtual/misc/beaglelogic/filltestpattern
sudo ./beaglelogic-testapp
# Should see incrementing pattern: 00 01 02 03...
```

---

## Files

| File | Purpose |
|------|---------|
| `beaglelogic-testapp.c` | Unified interactive test application (all modes) |
| `beaglelogic.c` | Userspace API library implementation |
| `libbeaglelogic.h` | API header with function declarations |
| `Makefile` | Build system |

---

**For more information:**
- [Getting Started Guide](../docs/02-getting-started.md)
- [API Reference](../docs/07-api-reference.md)
- [Architecture Overview](../docs/04-architecture.md)

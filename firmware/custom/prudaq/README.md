# BeagleLogic Support for the PRUDAQ Board

This directory contains modified PRU1 firmware for using BeagleLogic with the [PRUDAQ](https://github.com/google/prudaq) ADC cape.

## Overview

PRUDAQ is a high-speed analog-to-digital converter (ADC) cape for BeagleBone that uses an AD9201 dual channel ADC to capture analog signals. These firmware variants adapt BeagleLogic's PRU1 sampler to read ADC data instead of GPIO pins.

### Firmware Variants

Three firmware variants are provided:

1. **prudaq-ch0** - Captures I-channel (in-phase) only from AD9201
2. **prudaq-ch1** - Captures Q-channel (quadrature) only from AD9201
3. **prudaq-ch01** - Captures both I and Q channels interleaved

All variants output data through the standard `/dev/beaglelogic` interface, allowing use of existing BeagleLogic tools.

## Hardware Details

- **ADC**: Dual AD9201 10-bit, 20 MSPS ADCs
- **Clock Source**: External clock on PRU1 R31.16 (**P9_26** - UART1_RXD pin, follows clock signal edges)
- **Data Width**: 12-bit samples (10-bit ADC + alignment, upper bits masked)
- **Sampling**: Edge-triggered (waits for clock rising/falling edges via WBS/WBC instructions)

## Build Instructions

```bash
cd /opt/BeagleLogic/beaglelogic-firmware/custom/prudaq
make
```

This builds three firmware files:
- `release/prudaq-ch0.out`
- `release/prudaq-ch1.out`
- `release/prudaq-ch01.out`

## Installation

Deploy the desired firmware variant to `/lib/firmware` using the deploy targets:
```bash
sudo make deploy
```

This installs all three variants as:
- `/lib/firmware/beaglelogic-pru1-prudaq-ch0`
- `/lib/firmware/beaglelogic-pru1-prudaq-ch1`
- `/lib/firmware/beaglelogic-pru1-prudaq-ch01`

## Clock Generation (Optional)

The PRUDAQ firmware expects an **external clock on P9_26** (PRU1 R31 bit 16). For testing without external hardware, you can generate a clock using BeagleBone's PWM on P9_31.

**Note:** P9_31 PWM configuration requires pin multiplexing setup via device tree overlay in kernel 6.x. The BeagleLogic overlay (beaglelogic-00A0.dts) automatically configures this and exports the PWM channels.

### PWM Clock Setup

The PWM channels are automatically exported when the BeagleLogic overlay loads. Configure the frequency via sysfs:

```bash
# IMPORTANT: Always disable PWM before changing parameters to avoid locking the subsystem
echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable

# Set period for desired frequency (in nanoseconds)
# Example: 100ns period = 10 MHz
echo 100 > /sys/class/pwm/pwmchip0/pwm0/period

# Set duty cycle to 50% (half of period)
echo 50 > /sys/class/pwm/pwmchip0/pwm0/duty_cycle

# Enable the PWM output
echo 1 > /sys/class/pwm/pwmchip0/pwm0/enable
```

**Connection:** Wire P9_31 (PWM output) to P9_26 (PRUDAQ clock input) with the **shortest possible jumper wire**.

### Signal Integrity Tips

If you observe ringing or overshoot on the clock signal (visible on oscilloscope):

1. **Use shortest jumper wire possible** - Long wires act as transmission lines causing reflections
2. **Add series resistor** - Place a 33Ω to 47Ω resistor in series with P9_31 to dampen ringing
3. **Capacitive filtering** - Add a small capacitor (10-100pF) at the PRUDAQ clock input (P9_26) to ground

**Example circuit for clean signal:**
```
P9_31 ---[47Ω]--- P9_26 (PRUDAQ clock input)
                    |
                  [100pF]
                    |
                   GND
```

### Clock Frequency Examples

| Target Frequency | Period (ns) | Duty Cycle (ns) |
|------------------|-------------|-----------------|
| 20 MHz           | 50          | 25              |
| 10 MHz           | 100         | 50              |
| 5 MHz            | 200         | 100             |
| 4 MHz            | 250         | 125             |
| 1 MHz            | 1000        | 500             |

## Upgrading to 12-bit Resolution

The AD9201 hardware is 10-bit, but the PRU GPIO pins can capture up to 14 bits. 12-bits directly manageable on BeagleBone Black; however, to go to 14-bits, will need to disable eMMC and run OS solely from microSD. To upgrade from 10-bit to 12-bit resolution:

**1. Modify the bit mask in firmware:**

Edit the `.asm` file you're using and change the mask from `0x03FF` (10-bit) to `0x0FFF` (12-bit):

```assembly
; Original (10-bit):
LDI    R20.w0, 0x03FF

; Modified (12-bit):
LDI    R20.w0, 0x0FFF
```

**2. Rebuild the firmware:**

```bash
make clean
make
sudo make deploy
```

**3. Hardware considerations:**

- The extra 2 bits come from PRU1 GPIO pins immediately following the AD9201 data pins
- These pins should be configured as inputs in your device tree
- Ensure the additional GPIO pins are not being driven by other hardware
- The 12-bit data will use bits [11:0] of the 16-bit samples

**Note:** The original BeagleLogic/PRUDAQ design already has the next 2 PRU1 GPIO bits enabled as inputs, so in most cases you can simply change the mask without hardware modifications. The extra bits will just read as 0 if not connected to additional ADC outputs.

## Notes

- PRU0 firmware remains unchanged - only PRU1 is customized for ADC capture
- Sample rate is controlled by the external ADC clock, not by BeagleLogic configuration
- Data format matches BeagleLogic's standard output (32-byte bursts via scratchpad)
- Compatible with kernel 6.x BeagleLogic driver
- Default configuration is 10-bit to match AD9201 hardware specifications

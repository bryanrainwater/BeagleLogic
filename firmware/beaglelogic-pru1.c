/**
 * @file beaglelogic-pru1.c
 * @brief PRU1 firmware for BeagleLogic - High-speed GPIO sampler
 *
 * PRU1 Responsibilities:
 * - Perform high-speed sampling of GPIO pins at up to 100 MHz
 * - Temporarily buffer sampled data in PRU registers
 * - Signal PRU0 when data is ready for DMA transfer
 * - Support both 8-bit and 16-bit sampling modes
 *
 * The actual sampling logic is implemented entirely in assembly
 * (beaglelogic-pru1-core.asm) for maximum performance.
 *
 * This C file simply calls the assembly entry point.
 *
 * Copyright (C) 2014-17 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdint.h>

/*
 * Note: No resource table or interrupt map needed for PRU1
 *
 * PRU1 does not configure any ARM interrupts - it only signals PRU0
 * (via event 21, PRU1_TO_PRU0) which is configured in PRU0's intc_map_0.h.
 *
 * In pru-software-support-package v6.5+, resource tables are only
 * required if using RPMsg. Since BeagleLogic doesn't use RPMsg,
 * no resource table is needed.
 */

/* Assembly entry point - implements high-speed sampling loop */
extern void asm_main();

/**
 * main - PRU1 firmware entry point
 *
 * Simply jumps to the assembly code which implements the entire
 * sampling logic. PRU1 runs entirely in assembly for performance.
 */
void main()
{
	asm_main();
}

/*
 * PRU1 Firmware for BeagleLogic (PRUDAQ variants)
 *
 * Copyright (C) 2014-17 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * This file is a part of the BeagleLogic project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdint.h>

/*
 * Note: No resource table or interrupt map needed for PRU1 PRUDAQ firmware
 * 
 * This firmware variant is for PRUDAQ ADC capture and does not configure
 * any interrupts - it only signals interrupts to PRU0 (event 21) which is
 * configured in PRU0's intc_map_0.h.
 * 
 * In pru-software-support-package v6.5+, resource tables are only
 * required if using RPMsg. Since BeagleLogic PRUDAQ doesn't use RPMsg,
 * no resource table is needed.
 * 
 * This C file is shared by all three PRUDAQ variants:
 * - prudaq-ch0.asm  (I-channel only)
 * - prudaq-ch1.asm  (Q-channel only) 
 * - prudaq-ch01.asm (Both I and Q channels)
 */

extern void asm_main();

void main()
{
        asm_main();
}

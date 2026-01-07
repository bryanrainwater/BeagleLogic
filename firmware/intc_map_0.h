/*
 * Interrupt Controller Mapping for PRU0 firmware of BeagleLogic
 * Converted from resource_table_0.h for pru-software-support-package v6.5+
 * Copyright (C) 2017 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the
 *	  distribution.
 *
 *	* Neither the name of Texas Instruments Incorporated nor the names of
 *	  its contributors may be used to endorse or promote products derived
 *	  from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _INTC_MAP_0_H_
#define _INTC_MAP_0_H_

/*
 * ======== PRU INTC Map for BeagleLogic PRU0 ========
 *
 * Define the INTC mapping for interrupts going to the PRU cores:
 * 	ICSS Host interrupts 0, 1
 *
 * Note that INTC interrupts going to the ARM Linux host should NOT be defined
 * in this file (ICSS Host interrupts 2-9). Those should be configured in the
 * device tree.
 *
 * Original resource table mappings:
 * - SYSEV_PRU0_TO_ARM (16) -> channel 2 -> host 2 [REMOVED - goes to ARM]
 * - SYSEV_ARM_TO_PRU0 (17) -> channel 0 -> host 0 [KEPT - goes to PRU0]
 * - SYSEV_PRU1_TO_PRU0 (20) -> channel 0 -> host 0 [KEPT - goes to PRU0]
 * - SYSEV_PRU0_TO_ARM_A (22) -> channel 4 -> host 4 [REMOVED - goes to ARM]
 * - SYSEV_PRU0_TO_ARM_B (24) -> channel 5 -> host 5 [REMOVED - goes to ARM]
 * - SYSEV_ARM_TO_PRU0_A (23) -> channel 1 -> host 1 [KEPT - goes to PRU0]
 *
 * For interrupts going to ARM (events 22, 24), add to your device tree:
 *
 * &your_beaglelogic_driver {
 * 	interrupt-parent = <&pruss_intc>;
 * 	interrupts = <22 4 4>, <24 5 5>;
 *  interrupt-names = "from_bl_1", "from_bl_2";
 * };
 */

#include <stddef.h>
#include <rsc_types.h>

#include "pru_defs.h"

/*
 * .pru_irq_map is used by the RemoteProc driver during initialization. However,
 * the map is NOT used by the PRU firmware. That means DATA_SECTION and RETAIN
 * are required to prevent the PRU compiler from optimizing out .pru_irq_map.
 */
#pragma DATA_SECTION(my_irq_rsc, ".pru_irq_map")
#pragma RETAIN(my_irq_rsc)

struct pru_irq_rsc my_irq_rsc = {
	0,			/* type = 0 */
	3,			/* number of system events being mapped */
	{
		/* Only map interrupts going TO this PRU (host interrupts 0, 1) */
		{SYSEV_ARM_TO_PRU0,   0, 0},	/* sysevt 17 -> channel 0 -> host 0 */
		{SYSEV_PRU1_TO_PRU0,  0, 0},	/* sysevt 20 -> channel 0 -> host 0 */
		{SYSEV_ARM_TO_PRU0_A, 1, 1},	/* sysevt 23 -> channel 1 -> host 1 */
		/* 
		 * Interrupts going to ARM (hosts 2-9) removed from this file.
		 * Configure these in your Linux device tree instead:
		 * - SYSEV_PRU0_TO_ARM (16)
		 * - SYSEV_PRU0_TO_ARM_A (22)
		 * - SYSEV_PRU0_TO_ARM_B (24)
		 */
	},
};

#endif /* _INTC_MAP_0_H_ */

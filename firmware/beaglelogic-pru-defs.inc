;*******************************************************************************
;* @file beaglelogic-pru-defs.inc
;* @brief Assembly constant definitions for BeagleLogic PRU firmware
;*
;* This file defines constants, register aliases, and memory addresses used
;* by both PRU0 and PRU1 assembly code. These definitions simplify access to
;* PRU hardware resources and maintain consistency across the firmware.
;*
;* Copyright (C) 2014 Kumar Abhishek <abhishek@theembeddedkitchen.net>
;*******************************************************************************

	;=========================================================================
	;* Constant Table Pointer Aliases
	;*
	;* The PRU provides constant table pointers (C0-C31) for efficient access
	;* to various memory regions and peripherals. These aliases make the code
	;* more readable.
	;=========================================================================

	.asg C0, CONST_PRUSSINTC       ; PRU interrupt controller
	.asg C4, CONST_PRUCFG          ; PRU configuration registers
	.asg C24, PRUDRAM              ; PRU data RAM
	.asg C31, CONST_DDR            ; External DDR memory

	;=========================================================================
	;* PRU Interrupt Controller Register Offsets
	;*
	;* These offsets are used to access INTC registers for interrupt handling.
	;=========================================================================

	.asg 0x10, GER_OFFSET          ; Global Enable Register
	.asg 0x24, SICR_OFFSET         ; System Interrupt Status Clear Register
	.asg 0x28, EISR_OFFSET         ; Enable Interrupt Set Register
	.asg 0x34, HIESR_OFFSET        ; Host Interrupt Enable Set Register

	;=========================================================================
	;* Memory Addresses
	;=========================================================================

	.asg 0x02000, CONST_OTHERPRU_MEM    ; Other PRU's data RAM base
	.asg 0x10000, CONST_SHARED_MEM      ; Shared memory base address

	;=========================================================================
	;* System Event Numbers for Interrupts
	;*
	;* These map to the system events used for communication between PRUs
	;* and the ARM host processor.
	;=========================================================================

	.asg 16, PRU0_ARM_INTERRUPT         ; PRU0 -> ARM (general)
	.asg 17, ARM_PRU0_INTERRUPT         ; ARM -> PRU0 (general)
	.asg 18, PRU1_ARM_INTERRUPT         ; PRU1 -> ARM (general)
	.asg 19, ARM_PRU1_INTERRUPT         ; ARM -> PRU1 (general)
	.asg 20, PRU1_PRU0_INTERRUPT        ; PRU1 -> PRU0 (data ready)
	.asg 21, PRU0_PRU1_INTERRUPT        ; PRU0 -> PRU1
	.asg 22, PRU0_ARM_INTERRUPT_A       ; PRU0 -> ARM (buffer ready)
	.asg 24, PRU0_ARM_INTERRUPT_B       ; PRU0 -> ARM (capture complete)

	;=========================================================================
	;* PRU Control Register Addresses
	;*
	;* Base address and offsets for PRU control registers.
	;* CTPPR_0 is used to configure constant table pointers.
	;=========================================================================

	.asg 0x22028, CTPPR_0               ; Constant Table Pointer Register 0

	; PRU Control Register Map (Base: 0x22000 for PRU0, 0x24000 for PRU1)
	; CONTROL         0x22000     - Control register (enable, reset, etc.)
	; STATUS          0x22004     - Status register (run state, etc.)
	; WAKEUP_EN       0x22008     - Wakeup enable register
	; CYCLE           0x2200C     - Cycle counter
	; STALL           0x22010     - Stall counter
	; CTBIR_0         0x22020     - Constant Table Block Index Register 0
	; CTBIR_1         0x22024     - Constant Table Block Index Register 1
	; CTPPR_0         0x22028     - Constant Table Pointer Register 0
	; CTPPR_1         0x2202C     - Constant Table Pointer Register 1

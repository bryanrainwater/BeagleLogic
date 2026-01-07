;*******************************************************************************
;* PRU1 Firmware for BeagleLogic (PRUDAQ variant - I-channel only)
;*
;* Copyright (C) 2014 Kumar Abhishek <abhishek@theembeddedkitchen.net>
;* Mar '16: Modified by Kumar Abhishek for supporting the PRUDAQ board
;* Adapted by Jason Holt to follow an external clock signal
;*
;* This firmware variant captures the I-channel (in-phase) only from the
;* AD9201 ADC on the PRUDAQ cape and outputs to /dev/beaglelogic
;*
;* Hardware Details:
;* - AD9201: 10-bit, 20 MSPS ADC
;* - Clock Input: R31.16 (external clock on P9_26 - UART1_RXD, edge-triggered sampling)
;* - Data Input: R31.w0 (10-bit, upper bits masked to 0x03FF)
;* - Samples 16 I-channel values per transfer (32 bytes total)
;*
;* Sampling Flow:
;* 1. Wait for clock falling edge (WBC R31, 16)
;* 2. Wait for clock rising edge (WBS R31, 16)
;* 3. Delay ~15ns (3 NOPs) for ADC setup time
;* 4. Read ADC value into register word (MOV Rx.w0, R31.w0)
;* 5. Repeat for 16 samples
;* 6. Mask unused bits and transfer to PRU0 via scratchpad
;*
;* This program is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License version 2 as
;* published by the Free Software Foundation.
;*******************************************************************************

	.include "beaglelogic-pru-defs.inc"

;*******************************************************************************
;* Macro Definitions
;*******************************************************************************

;* NOP macro - Simple no-operation using harmless ADD
NOP	.macro
	 ADD R0.b0, R0.b0, R0.b0
	.endm

;* DELAY macro - Software delay loop (unused in PRUDAQ - clock-driven sampling)
;* Parameters:
;*   Rx  - Register containing delay count (cycles - 2)
;*   op  - Operation to execute after delay completes
DELAY	.macro Rx, op
	SUB	R0, Rx, 2
	QBEQ	$E?, R0, 0
$M?:	SUB	R0, R0, 1
	QBNE	$M?, R0, 0
$E?:	op
	.endm

	.sect ".text:main"
	.global asm_main
asm_main:
	; Set C28 in this PRU's bank =0x24000
	LDI32  R0, CTPPR_0+0x2000               ; Add 0x2000
	LDI    R1, 0x00000240                   ; C28 = 00_0240_00h = PRU1 CFG Registers
	SBBO   &R1, R0, 0, 4

	; Configure R2 = 0x0000 - ptr to PRU1 RAM
	LDI    R2, 0

	; Enable the cycle counter
	LBCO   &R0, C28, 0, 4
	SET    R0, R0, 3
	SBCO   &R0, C28, 0, 4

	; Load Cycle count reading to registers [LBCO=4 cycles, SBCO=2 cycles]
	LBCO   &R0, C28, 0x0C, 4
	SBCO   &R0, C24, 0, 4

	; Load magic bytes into R2
	LDI32  R0, 0xBEA61E10

	; Wait for PRU0 to load configuration into R14[samplerate] and R15[unit]
	; This will occur from an downcall issued to us by PRU0
	HALT

	; Jump to the appropriate sample loop
	; TODO

	LDI    R31, PRU0_ARM_INTERRUPT_B + 16   ; Signal SYSEV_PRU0_TO_ARM_B to kernel driver
	HALT

	; Sample starts here
	; Maintain global bytes transferred counter (8 byte bursts)
	LDI    R29, 0
	LDI    R20.w0, 0x03FF                ; For masking unused bits
	LDI    R20.w2, 0x03FF

sampleAD9201:
	;=========================================================================
	; AD9201 I-Channel Sampling Loop
	;
	; This loop samples the I-channel from the AD9201 ADC, synchronized to
	; an external clock on R31.16. It captures 16 samples per iteration and
	; transfers them to PRU0 for DMA to system memory.
	;
	; Clock Synchronization:
	; - WBC R31, 16: Wait for Bit Clear (falling edge on clock)
	; - WBS R31, 16: Wait for Bit Set (rising edge on clock)
	; - Sampling occurs after rising edge + 15ns setup time
	;
	; Register Usage:
	; - R20: Bit mask (0x0FFF) to zero upper 4 bits of 16-bit ADC data
	; - R21-R28: Buffer for 16 samples (2 samples per register = 32 bytes)
	; - R29: Global byte counter (incremented by 32 each transfer)
	; - R31.16: Clock input (P9_26)
	; - R31.w0: ADC data input
	;=========================================================================
	WBC    R31, 16                       ; Wait for falling edge (P9_26)
	WBS    R31, 16                       ; Wait for rising edge
	NOP                                  ; 3 cycles ~15ns delay before readout
$sampleAD9201$2:
	NOP
	NOP
	MOV    R21.w0, R31.w0                ; Read I0

	WBC    R31, 16
	WBS    R31, 16
	NOP
	NOP
	NOP
	MOV    R21.w2, R31.w0                ; I1

	WBC    R31, 16
	WBS    R31, 16
	AND    R21, R21, R20                 ; Mask unused bits
	NOP
	NOP
	MOV    R22.w0, R31.w0                ; I2

	WBC    R31, 16
	WBS    R31, 16
	NOP
	NOP
	NOP
	MOV    R22.w2, R31.w0                ; I3

	WBC    R31, 16
	WBS    R31, 16
	AND    R22, R22, R20
	NOP
	MOV    R23.w0, R31.w0                ; I4

	WBC    R31, 16
	WBS    R31, 16
	NOP
	NOP
	NOP
	MOV    R23.w2, R31.w0                ; I5

	WBC    R31, 16
	WBS    R31, 16
	AND    R23, R23, R20
	NOP
	NOP
	MOV    R24.w0, R31.w0                ; I6

	WBC    R31, 16
	WBS    R31, 16
	NOP
	NOP
	NOP
	MOV    R24.w2, R31.w0                ; I7

	WBC    R31, 16
	WBS    R31, 16
	AND    R24, R24, R20
	NOP
	NOP
	MOV    R25.w0, R31.w0                ; I8

	WBC    R31, 16
	WBS    R31, 16
	NOP
	NOP
	NOP
	MOV    R25.w2, R31.w0                ; I9

	WBC    R31, 16
	WBS    R31, 16
	AND    R25, R25, R20
	NOP
	NOP
	MOV    R26.w0, R31.w0                ; I10

	WBC    R31, 16
	WBS    R31, 16
	NOP
	NOP
	NOP
	MOV    R26.w2, R31.w0                ; I11

	WBC    R31, 16
	WBS    R31, 16
	AND    R26, R26, R20
	NOP
	NOP
	MOV    R27.w0, R31.w0                ; I12

	WBC    R31, 16
	WBS    R31, 16
	NOP
	NOP
	NOP
	MOV    R27.w2, R31.w0                ; I13

	WBC    R31, 16
	WBS    R31, 16
	AND    R27, R27, R20
	NOP
	NOP
	MOV    R28.w0, R31.w0                ; I14

	WBC    R31, 16
	WBS    R31, 16
	NOP
	ADD    R29, R29, 32                  ; Maintain global byte counter
	NOP
	MOV    R28.w2, R31.w0                ; I15

	WBC    R31, 16
	WBS    R31, 16
	AND    R28, R28, R20
	XOUT   10, &R21, 36                     ; Move data across the broadside
	LDI    R31, PRU1_PRU0_INTERRUPT + 16    ; Jab PRU0
	MOV    R28.w2, R31.w0                ; I0 (repeat)
	WBC    R31, 16
	WBS    R31, 16
	JMP    $sampleAD9201$2

; End-of-firmware
	HALT

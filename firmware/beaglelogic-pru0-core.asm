;*******************************************************************************
;* PRU0 Core Assembly - BeagleLogic DMA Transfer Loop
;* Version: Stop Flag Check Per Buffer (PRIMARY VERSION)
;*
;* This version checks a stop flag in the context structure once per buffer,
;* providing optimal performance with negligible overhead (~0.025%).
;*
;* Stop flag location: context.stop_flag at offset 0x18 (before buffer list at 0x1C)
;* - Driver writes non-zero value to request stop
;* - PRU0 checks once per buffer (every ~100us @ 40MHz) in continuous mode only
;* - Overhead: 5 cycles per buffer = 25ns per 102us = 0.025%
;*
;* This assembly file implements the critical DMA transfer loop for PRU0.
;* It reads sampled data from PRU1 and writes it to system memory buffers.
;*
;* Flow:
;* 1. Load scatter-gather buffer list from shared memory
;* 2. For each buffer:
;*    a. Check stop flag (continuous mode only)
;*    b. Wait for PRU1 to signal data ready
;*    c. Transfer 32 bytes from PRU1 to system memory via XIN/SBBO
;*    d. Signal ARM when buffer is full
;* 3. Repeat for continuous mode or stop for one-shot mode
;*
;* Copyright (C) 2014 Kumar Abhishek <abhishek@theembeddedkitchen.net>
;* Copyright (C) 2024-2026 Bryan Rainwater
;*
;* This program is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License version 2 as
;* published by the Free Software Foundation.
;*******************************************************************************

;* Import symbols from C file (context structure, constants)
	.cdecls "beaglelogic-pru0.c"

;* Stop flag location in capture context structure
;* Context structure is at PRU0 DRAM offset 0, accessed via C24 or R14
;* Stop flag offset: 0x18 (7th field in context, before buffer list at 0x1C)
STOP_FLAG_OFFSET	.set	0x18

;* Function: run
;* Prototype: void run(struct capture_context *ctx, u32 trigger_flags)
;* R14 = ctx pointer
;* R15 = trigger_flags (0=one-shot, 1=continuous)
	.clink
	.global run
run:
	; Initialize loop constants
	LDI	R17, (MAX_BUFLIST_ENTRIES * 8)  ; Max buffer list size in bytes
	LDI	R0, SYSEV_PRU1_TO_PRU0          ; Event number to clear PRU1 interrupt
	LDI	R1, 0                            ; Constant zero

$run$0:
	; Calculate buffer list address (ctx + 28 bytes offset)
	; Context: magic(4) + cmd(4) + resp(4) + samplediv(4) + sampleunit(4) + triggerflags(4) + stop_flag(4) = 28 bytes
	ADD	R16, R14, 28                    ; R16 = pointer to buffer list

$run$1:
	; Load next buffer descriptor (start and end addresses)
	LBBO	&R18, R16, 0, 8             ; R18=start_addr, R19=end_addr
	QBEQ	$run$canexit, R18, 0        ; If start_addr==0, list is done

	; =========================================================================
	; STOP FLAG CHECK - Once per buffer (continuous mode only)
	; =========================================================================
	; Check for stop request in CONTINUOUS mode only (bit 0 of R15)
	; Oneshot mode exits naturally when buffers are full, no stop check needed
	QBBC	$run$1_skip_stop_check, R15, 0  ; Skip stop check if oneshot (R15 bit 0 = 0)

	; Continuous mode: check stop flag in context structure
	; Context is at PRU0 DRAM offset 0, stop_flag at offset 0x18 (24 decimal)
	; Use LBCO with C24 (PRU0 DRAM base) + offset 0x18 (within 8-bit range)
	LBCO	&R2, C24, STOP_FLAG_OFFSET, 4  ; Read stop_flag from context (4 cycles)
	QBEQ	$run$1_skip_stop_check, R2, 0  ; Continue if flag is 0 (1 cycle)

	; Stop requested - exit gracefully
	JMP	$run$exit                      ; (1 cycle)

$run$1_skip_stop_check:
	; =========================================================================
	; END STOP FLAG CHECK (Total: 5 cycles if no stop, 6 cycles if stopping)
	; =========================================================================

$run$2:
	; Wait for PRU1 to signal data is ready (bit 30 of R31)
	WBS	R31, 30                         ; Wait for bit set
	SBCO	&R0, C0, 0x24, 4            ; Clear the interrupt (SECR0 register)

	; Transfer 32 bytes from PRU1 scratchpad to system memory
	XIN	10, &R21, 36                    ; Read 36 bytes from PRU1 (scratchpad 10)
	SBBO	&R21, R18, 0, 32            ; Write 32 bytes to DMA buffer
	ADD	R18, R18, 32                ; Advance buffer pointer

	; Check if current buffer is full
	QBLT	$run$2, R19, R18            ; Loop if end_addr > current_addr

	; Buffer full - signal ARM and check for stop request
	LDI	R31, 32 | (SYSEV_PRU0_TO_ARM_A - 16)  ; Trigger buffer-ready interrupt
	QBBS	$run$exit, R31, 31          ; Exit if stop bit set (bit 31)

	; Move to next buffer in scatter-gather list
	ADD	R16, R16, 8                 ; Advance to next 8-byte descriptor
	QBLT	$run$1, R17, R16            ; Loop if more entries exist

	; Wrapped around - reset pointer for continuous mode
	LDI	R14, 0
	JMP	$run$exit

$run$canexit:
	; Check if continuous mode (bit 0 of R15)
	QBBS	$run$0, R15, 0              ; Restart if continuous mode

$run$error:
	MOV	R14, R29                        ; Save error code

$run$exit:
	JMP	R3.w2                           ; Return to caller

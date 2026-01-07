/**
 * @file beaglelogic-pru0.c
 * @brief PRU0 firmware for BeagleLogic - DMA coordinator and command handler
 *
 * PRU0 Responsibilities:
 * - Receives commands from the kernel driver via shared memory
 * - Configures PRU1 with sample rate and sample unit settings
 * - Manages the scatter-gather buffer list
 * - Coordinates DMA transfers by reading sampled data from PRU1
 * - Signals buffer-ready and capture-complete interrupts to the kernel
 *
 * The heavy assembly core logic is in beaglelogic-pru0-core.asm
 *
 * Copyright (C) 2014-17 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Compile for PRU0 */
#define PRU0

#include <stdint.h>
#include <stdio.h>
#include <pru_cfg.h>
#include <pru_intc.h>
#include <rsc_types.h>

#include "pru_defs.h"
#include "intc_map_0.h"

/* Firmware version (v0.3) */
#define MAJORVER	0
#define MINORVER	3

/* Maximum scatter-gather list entries (each entry is 8 bytes) */
#define MAX_BUFLIST_ENTRIES	128

/**
 * Command codes sent from kernel driver to PRU0
 */
#define CMD_GET_VERSION	1   /* Query firmware version */
#define CMD_GET_MAX_SG	2   /* Query max scatter-gather entries */
#define CMD_SET_CONFIG 	3   /* Set capture configuration */
#define CMD_START	4   /* Start sampling operation */
#define CMD_STOP	5   /* Stop sampling operation (continuous mode) */

/* Magic number for structure validation */
#define FW_MAGIC	0xBEA61E10

/**
 * struct buflist - Scatter-gather buffer descriptor
 * @dma_start_addr: Physical address where buffer starts
 * @dma_end_addr: Physical address where buffer ends (exclusive)
 */
typedef struct buflist {
	uint32_t dma_start_addr;
	uint32_t dma_end_addr;
} bufferlist;

/**
 * struct capture_context - Shared memory structure at PRU0 SRAM offset 0x0000
 *
 * This structure provides bidirectional communication between the kernel
 * driver and the PRU firmware. The kernel writes configuration and commands,
 * and the PRU writes responses.
 *
 * @magic: Magic number for validation (0xBEA61E10)
 * @cmd: Command code from kernel to PRU
 * @resp: Response code from PRU to kernel
 * @samplediv: Sample rate divisor (sample_rate = 100MHz / samplediv)
 * @sampleunit: Sample width (0=16-bit, 1=8-bit)
 * @triggerflags: Capture mode (0=one-shot, 1=continuous)
 * @stop_flag: Stop request flag (0=run, 1=stop) - written by kernel, read by PRU
 * @list: Scatter-gather buffer list (null-terminated)
 */
struct capture_context {
	uint32_t magic;
	uint32_t cmd;
	uint32_t resp;
	uint32_t samplediv;
	uint32_t sampleunit;
	uint32_t triggerflags;
	uint32_t stop_flag;
	bufferlist list[MAX_BUFLIST_ENTRIES];
} cxt __attribute__((location(0))) = {0};

/* Run state flag: 1 when capture is active */
uint16_t state_run = 0;

/**
 * resume_other_pru - Resume PRU1 execution
 *
 * Resumes the other PRU (PRU1) after it has been halted.
 * This is used to restart PRU1 for the next capture cycle.
 */
static inline void resume_other_pru(void) {
	uint32_t i;

	i = (uint16_t)PCTRL_OTHER(0x0000);
	i |= (((uint16_t)PCTRL_OTHER(0x0004) + 1) << 16) | CONTROL_ENABLE;
	i &= ~CONTROL_SOFT_RST_N;
	PCTRL_OTHER(0x0000) = i;
}

/**
 * wait_other_pru_timeout - Wait for PRU1 to halt
 * @timeout: Maximum iterations to wait
 *
 * Return: 0 if halted, -1 on timeout
 */
static inline int wait_other_pru_timeout(uint32_t timeout) {
	do {
		if ((PCTRL_OTHER(0x0000) & CONTROL_RUNSTATE) == 0)
			return 0;
	} while (timeout--);
	return -1;
}

/**
 * configure_capture - Configure PRU1 with sample rate and unit
 *
 * Writes sample configuration to PRU1's registers and verifies readiness.
 * PRU1 should be halted and waiting for configuration.
 *
 * Return: 0 on success, -1 on error
 */
int configure_capture() {
	/* Verify PRU1 is halted and waiting */
	if (wait_other_pru_timeout(200))
		return -1;

	/* Verify PRU1 magic number to ensure firmware is loaded */
	if (pru_other_read_reg(0) != FW_MAGIC)
		return -1;

	/* Write configuration to PRU1 registers */
	pru_other_write_reg(14, cxt.samplediv);   /* R14: sample rate divisor */
	pru_other_write_reg(15, cxt.sampleunit);  /* R15: sample unit (8/16-bit) */

	/* Resume PRU1, let it configure, then wait for it to halt again */
	resume_other_pru();
	__delay_cycles(10);
	if (wait_other_pru_timeout(200))
		return -1;

	/* PRU1 is now ready to sample */
	return 0;
}

/**
 * handle_command - Process command from kernel driver
 * @cmd: Command code
 *
 * Return: Response code (>= 0 on success, -1 on error)
 */
static int handle_command(uint32_t cmd) {
	switch (cmd) {
		case CMD_GET_VERSION:
			return (MINORVER | (MAJORVER << 8));

		case CMD_GET_MAX_SG:
			return MAX_BUFLIST_ENTRIES;

		case CMD_SET_CONFIG:
			return configure_capture();

		case CMD_START:
			state_run = 1;
			return 0;
	}
	return -1;
}

/* External assembly function that performs the DMA transfer loop */
extern void run(struct capture_context *ctx, uint32_t trigger_flags);

/**
 * main - PRU0 firmware main loop
 *
 * Initializes the PRU, processes commands from the kernel driver,
 * and coordinates the capture operation with PRU1.
 */
int main(void) {
	/* Enable OCP master port for DMA access to system memory */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/* Initialize magic number for validation */
	cxt.magic = FW_MAGIC;

	/* Clear all pending interrupts */
	CT_INTC.SECR0 = 0xFFFFFFFF;

	/* Main command processing loop */
	while (1) {
		/* Check for and process commands from kernel */
		if (cxt.cmd != 0) {
			cxt.resp = handle_command(cxt.cmd);
			cxt.cmd = 0;  /* Clear command to signal completion */
		}

		/* Start capture operation when triggered */
		if (state_run == 1) {
			/* Clear pending interrupts before starting */
			CT_INTC.SECR0 = 0xFFFFFFFF;

			/* Resume PRU1 to begin sampling */
			resume_other_pru();

			/* Run the DMA transfer loop (assembly code) */
			run(&cxt, cxt.triggerflags);

			/* Signal capture complete to kernel */
			SIGNAL_EVENT(SYSEV_PRU0_TO_ARM_B);

			/* Reset PRU1 for next capture */
			PCTRL_OTHER(0x0000) &= (uint16_t)~CONTROL_SOFT_RST_N;
			state_run = 0;
		}
	}
}

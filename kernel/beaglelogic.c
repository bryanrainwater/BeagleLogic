/**
 * @file beaglelogic.c
 * @brief Kernel driver for BeagleLogic Logic Analyzer on BeagleBone platforms
 *
 * BeagleLogic is a 100MHz, 14-channel logic analyzer that runs on BeagleBone
 * hardware. It leverages the Programmable Real-time Units (PRUs) for high-speed
 * sampling and DMA for efficient data transfer to system memory.
 *
 * Architecture:
 * - PRU1: Performs high-speed GPIO sampling at up to 100MHz
 * - PRU0: Manages DMA transfers and coordinates with the kernel driver
 * - Kernel Driver: Allocates DMA buffers, handles interrupts, provides user API
 *
 * Original work:
 * Copyright (C) 2014-2020 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * Kernel 6.x port and updates:
 * Copyright (C) 2024-2026 Bryan Rainwater
 *
 * Major changes for kernel 6.x:
 * - Migrated to DMA coherent memory API (dma_alloc_coherent)
 * - Fixed buffer state machine and Ctrl+C handling
 * - Updated PRU interrupt handling (direct INTC register access)
 * - Modernized DMA mask configuration
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/* Kernel headers - Modern API (kernel 6.x) */
#include <linux/atomic.h>
#include <linux/uaccess.h>

/* Core kernel APIs */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/poll.h>

/* Platform and device tree */
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

/* PRU subsystem (kernel 6.x API) */
#include <linux/remoteproc/pruss.h>
#include <linux/remoteproc.h>
#include <linux/pruss_driver.h>

/* Character device interface */
#include <linux/miscdevice.h>
#include <linux/fs.h>

/* Memory and DMA management */
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

/* Interrupt handling */
#include <linux/interrupt.h>
#include <linux/irqreturn.h>

/* Sysfs and device management */
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/device.h>

#include "beaglelogic.h"

/**
 * enum bufstates - DMA buffer state tracking
 *
 * @STATE_BL_BUF_ALLOC: Buffer allocated but not yet DMA-mapped
 * @STATE_BL_BUF_MAPPED: Buffer DMA-mapped and ready for use
 * @STATE_BL_BUF_UNMAPPED: Buffer filled with data, ready for userspace read
 * @STATE_BL_BUF_DROPPED: Buffer overrun detected, data may be corrupted
 */
enum bufstates {
	STATE_BL_BUF_ALLOC,
	STATE_BL_BUF_MAPPED,
	STATE_BL_BUF_UNMAPPED,
	STATE_BL_BUF_DROPPED
};

/**
 * PRU Firmware Commands
 *
 * These commands are sent to the PRU firmware through shared memory.
 * The firmware sets the response code in the same structure.
 */
#define CMD_GET_VERSION 1   /* Query firmware version (response: version number) */
#define CMD_GET_MAX_SG  2   /* Query max scatter-gather entries (response: max count) */
#define CMD_SET_CONFIG  3   /* Configure capture parameters (response: 0 on success) */
#define CMD_START       4   /* Begin sampling operation (response: 0 on success) */

/**
 * struct buflist - PRU-side DMA buffer descriptor
 *
 * Each entry describes a contiguous DMA buffer region. The PRU firmware
 * reads this scatter-gather list to know where to write captured data.
 *
 * @dma_start_addr: Physical start address of buffer
 * @dma_end_addr: Physical end address of buffer (exclusive)
 */
struct buflist {
	uint32_t dma_start_addr;
	uint32_t dma_end_addr;
};

/**
 * struct capture_context - Shared memory structure for PRU communication
 *
 * This structure resides in PRU0 SRAM at offset 0x0000 and provides
 * bidirectional communication between the kernel driver and PRU firmware.
 *
 * @magic: Magic number (0xBEA61E10) for firmware validation
 * @cmd: Command code sent from kernel to PRU
 * @resp: Response code returned from PRU to kernel
 * @samplediv: Sample rate divisor (sample_rate = 100MHz / samplediv)
 * @sampleunit: Sample width (0=16-bit, 1=8-bit)
 * @triggerflags: Capture mode (0=one-shot, 1=continuous)
 * @stop_flag: Stop request flag (0=run, 1=stop requested by driver)
 * @list_head: First entry of scatter-gather buffer list
 */
struct capture_context {
#define BL_FW_MAGIC	0xBEA61E10
	uint32_t magic;
	uint32_t cmd;
	uint32_t resp;
	uint32_t samplediv;
	uint32_t sampleunit;
	uint32_t triggerflags;
	uint32_t stop_flag;
	struct buflist list_head;
};

/* Forward declaration */
static const struct file_operations pru_beaglelogic_fops;

/**
 * struct logic_buffer - DMA buffer descriptor for captured data
 *
 * Buffers form a circular linked list to enable efficient streaming reads.
 * Each buffer represents one DMA-coherent memory region used for data capture.
 *
 * @buf: Virtual address of buffer (kernel space)
 * @phys_addr: Physical address for DMA operations
 * @size: Size of buffer in bytes
 * @state: Current buffer state (see enum bufstates)
 * @index: Buffer index in the array
 * @next: Pointer to next buffer (circular list)
 */
struct logic_buffer {
	void *buf;
	dma_addr_t phys_addr;
	size_t size;
	unsigned short state;
	unsigned short index;
	struct logic_buffer *next;
};

/**
 * struct beaglelogic_private_data - Firmware configuration data
 *
 * @fw_names: Array of firmware filenames for each PRU
 */
struct beaglelogic_private_data {
	const char *fw_names[PRUSS_NUM_PRUS];
};

/**
 * struct beaglelogicdev - Main driver context for BeagleLogic device
 *
 * This structure contains all the state, configuration, and resources
 * needed to operate the BeagleLogic logic analyzer.
 *
 * @miscdev: Character device registration structure
 * @pruss: Handle to PRUSS (PRU subsystem) instance
 * @pru0: Remote processor handle for PRU0 (DMA coordinator)
 * @pru1: Remote processor handle for PRU1 (high-speed sampler)
 * @pru0sram: Memory region for PRU0 shared RAM
 * @prussio_vaddr: Virtual address of PRU INTC for interrupt triggering
 * @fw_data: Firmware configuration (names of PRU firmware files)
 * @to_bl_irq: IRQ number for kernel->PRU interrupts
 * @from_bl_irq_1: IRQ number for buffer-ready interrupts from PRU
 * @from_bl_irq_2: IRQ number for capture-complete interrupts from PRU
 * @coreclockfreq: PRU core clock frequency (typically 200MHz)
 * @p_dev: Parent platform device
 * @mutex: Mutex protecting device state during operations
 * @buffers: Array of DMA buffers for captured data
 * @lastbufready: Pointer to most recently filled buffer
 * @bufbeingread: Pointer to buffer currently being read by userspace
 * @bufcount: Number of allocated buffers
 * @wait: Wait queue for blocking read operations
 * @previntcount: Previous interrupt count (for debugging)
 * @cxt_pru: Pointer to shared memory structure in PRU0 SRAM
 * @maxbufcount: Maximum number of buffers supported by firmware
 * @bufunitsize: Size of each buffer unit in bytes
 * @samplerate: Current sample rate in Hz
 * @triggerflags: Capture mode (one-shot or continuous)
 * @sampleunit: Sample width (8-bit or 16-bit)
 * @state: Current device state (see enum beaglelogic_states)
 * @lasterror: Last error code (0 if no error)
 */
struct beaglelogicdev {
	/* Device registration */
	struct miscdevice miscdev;

	/* PRU subsystem handles */
	struct pruss *pruss;
	struct rproc *pru0, *pru1;
	struct pruss_mem_region pru0sram;  /* PRU0 DRAM (context + stop flag at offset 0x18) */
	void __iomem *prussio_vaddr;

	/* Firmware configuration */
	const struct beaglelogic_private_data *fw_data;

	/* Interrupt resources */
	int to_bl_irq;
	int from_bl_irq_1;
	int from_bl_irq_2;

	/* Hardware configuration */
	uint32_t coreclockfreq;
	struct device *p_dev;

	/* Synchronization */
	struct mutex mutex;

	/* Buffer management */
	struct logic_buffer *buffers;
	struct logic_buffer *lastbufready;
	struct logic_buffer *bufbeingread;
	uint32_t bufcount;
	wait_queue_head_t wait;

	/* ISR bookkeeping */
	uint32_t previntcount;

	/* PRU communication */
	struct capture_context *cxt_pru;

	/* Capture configuration */
	uint32_t maxbufcount;
	uint32_t bufunitsize;
	uint32_t samplerate;
	uint32_t triggerflags;
	uint32_t sampleunit;

	/* Device state */
	uint32_t state;
	uint32_t lasterror;
};

/**
 * struct logic_buffer_reader - Per-file reader state
 *
 * Each open file descriptor maintains its own read position.
 *
 * @bldev: Pointer to parent BeagleLogic device
 * @buf: Current buffer being read
 * @pos: Current position within buffer
 * @remaining: Bytes remaining in current buffer
 */
struct logic_buffer_reader {
	struct beaglelogicdev *bldev;
	struct logic_buffer *buf;
	uint32_t pos;
	uint32_t remaining;
};

#define to_beaglelogicdev(dev)	container_of((dev), \
		struct beaglelogicdev, miscdev)

#define DRV_NAME	"beaglelogic"
#define DRV_VERSION	"1.2"

/* Function Prototypes */
uint32_t beaglelogic_get_samplerate(struct device *);
int beaglelogic_set_samplerate(struct device *, uint32_t);
uint32_t beaglelogic_get_sampleunit(struct device *);
int beaglelogic_set_sampleunit(struct device *, uint32_t);
uint32_t beaglelogic_get_triggerflags(struct device *);
int beaglelogic_set_triggerflags(struct device *, uint32_t);
static void beaglelogic_fill_buffer_testpattern(struct device *dev);
irqreturn_t beaglelogic_serve_irq(int, void *);
int beaglelogic_write_configuration(struct device *);
int beaglelogic_start(struct device *);
void beaglelogic_stop(struct device *);
ssize_t beaglelogic_f_read (struct file *, char __user *, size_t, loff_t *);
int beaglelogic_f_mmap(struct file *, struct vm_area_struct *);
__poll_t beaglelogic_f_poll(struct file *,struct poll_table_struct *);


/* ========================================================================
 * Buffer Management Section
 * ========================================================================
 *
 * This section handles DMA buffer allocation, mapping, and management.
 * Updated for kernel 6.x to use dma_alloc_coherent for proper DMA support.
 *
 * Key changes from earlier kernels:
 * - Use dma_alloc_coherent() instead of manual allocation + dma_map_single()
 * - Coherent buffers are pre-mapped at allocation time
 * - Buffer state is set to STATE_BL_BUF_MAPPED immediately
 * - No explicit DMA mapping/unmapping needed
 */

/**
 * beaglelogic_memalloc - Allocate DMA-coherent buffers for data capture
 * @dev: Device pointer
 * @bufsize: Total requested buffer size in bytes
 *
 * Allocates a set of DMA-coherent buffers organized as a circular list.
 * At least 2 buffers should be allocated.
 *
 * Return: 0 on success, negative error code on failure
 */
static int beaglelogic_memalloc(struct device *dev, uint32_t bufsize)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int i, cnt;
	void *buf;
	dma_addr_t dma_handle;

	/* Check if BL is in use */
	if (!mutex_trylock(&bldev->mutex))
		return -EBUSY;

	/* Compute no. of buffers to allocate, round up
	 * We need at least two buffers for ping-pong action */
	cnt = max(DIV_ROUND_UP(bufsize, bldev->bufunitsize), (uint32_t)2);

	/* Too large? */
	if (cnt > bldev->maxbufcount) {
		dev_err(dev, "Not enough memory\n");
		mutex_unlock(&bldev->mutex);
		return -ENOMEM;
	}


	/* Validate buffer unit size
	 * In kernel 6.x, we use dma_alloc_coherent which has platform-specific
	 * limits. The actual limit depends on CMA/coherent pool size.
	 * We'll let dma_alloc_coherent fail naturally if size is too large.
	 */
	if (bldev->bufunitsize > (32 * 1024 * 1024)) {
		dev_warn(dev, "Large buffer unit size (%u bytes) may fail due to coherent DMA limits\n",
		         bldev->bufunitsize);
		/* Don't hard-fail, let dma_alloc_coherent decide */
	}


	bldev->bufcount = cnt;

	/* Allocate buffer list */
	bldev->buffers = devm_kzalloc(dev, sizeof(struct logic_buffer) * (cnt),
			GFP_KERNEL);
	if (!bldev->buffers)
		goto failnomem;


	/* Allocate DMA buffers using dma_alloc_coherent */
	for (i = 0; i < cnt; i++) {
		buf = dma_alloc_coherent(dev, bldev->bufunitsize, 
		                         &dma_handle, GFP_KERNEL);
		if (!buf) {
			dev_err(dev, "Failed to allocate DMA buffer %d\n", i);
			goto failrelease;
		}

		/* Fill with 0xFF */
		memset(buf, 0xFF, bldev->bufunitsize);

		/* Set the buffers */
		bldev->buffers[i].buf = buf;
		bldev->buffers[i].phys_addr = dma_handle;
		bldev->buffers[i].size = bldev->bufunitsize;
		bldev->buffers[i].index = i;

		/* CRITICAL: Mark as mapped - coherent memory is pre-mapped */
		bldev->buffers[i].state = STATE_BL_BUF_MAPPED;

		/* Circularly link the buffers */
		bldev->buffers[i].next = &bldev->buffers[(i + 1) % cnt];
	}

	/* Update global state */
	//bldev->state = STATE_BL_MEMALLOCD;

	/* Write log and unlock */
	dev_info(dev, "Successfully allocated %d bytes of coherent DMA memory.\n",
			cnt * bldev->bufunitsize);

	mutex_unlock(&bldev->mutex);

	/* Done */
	return 0;
	
failrelease:
	/* Free any buffers we allocated */
	for (i = 0; i < cnt; i++) {
		if (bldev->buffers[i].buf) {
			dma_free_coherent(dev, bldev->buffers[i].size,
			                  bldev->buffers[i].buf,
			                  bldev->buffers[i].phys_addr);
		}
	}
	devm_kfree(dev, bldev->buffers);
	bldev->bufcount = 0;
	bldev->buffers = NULL;
	dev_err(dev, "Sample buffer allocation failed\n");
	
failnomem:
	dev_err(dev, "Not enough memory\n");
	mutex_unlock(&bldev->mutex);
	return -ENOMEM;
}

/**
 * beaglelogic_memfree - Free all allocated DMA buffers
 * @dev: Device pointer
 *
 * Releases all DMA-coherent buffers previously allocated by beaglelogic_memalloc().
 * This function is safe to call even if no buffers are allocated.
 */
static void beaglelogic_memfree(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int i;

	mutex_lock(&bldev->mutex);
	if (bldev->buffers) {
		for (i = 0; i < bldev->bufcount; i++) {
			if (bldev->buffers[i].buf) {
				/* Use dma_free_coherent instead of kfree */
				dma_free_coherent(dev, bldev->buffers[i].size,
				                  bldev->buffers[i].buf,
				                  bldev->buffers[i].phys_addr);
			}
		}

		devm_kfree(dev, bldev->buffers);
		bldev->buffers = NULL;
		bldev->bufcount = 0;
	}
	mutex_unlock(&bldev->mutex);
}

/**
 * beaglelogic_map_buffer - Verify or map a buffer for DMA
 * @dev: Device pointer
 * @buf: Buffer to map
 *
 * For DMA-coherent memory (kernel 6.x), this function mainly verifies
 * that the buffer is in the correct state. The actual mapping happens
 * at allocation time with dma_alloc_coherent().
 *
 * Return: 0 on success, -EINVAL on error
 */
static int beaglelogic_map_buffer(struct device *dev, struct logic_buffer *buf)
{
	/* For coherent DMA memory:
	 * - Memory is already mapped (done at allocation time)
	 * - phys_addr is already set
	 * - state should already be STATE_BL_BUF_MAPPED
	 * 
	 * Just verify the state and return success
	 */
	if (buf->state == STATE_BL_BUF_MAPPED)
		return 0;

	/* Coherent DMA: buffer is always physically mapped.
	 * State is managed by IRQ handler (MAPPED/UNMAPPED).
	 * Don't modify state here. */
	if (buf->phys_addr != 0) {
		return 0;  // Success, don't touch state
	}

	/* Something is really wrong */
	dev_err(dev, "Buffer not properly allocated (phys_addr=0)\n");
	return -EINVAL;
}

/**
 * beaglelogic_unmap_buffer - Mark buffer as unmapped (logical operation)
 * @dev: Device pointer
 * @buf: Buffer to unmap
 *
 * For DMA-coherent memory, this doesn't actually unmap the buffer
 * (it stays mapped until freed). This only updates the buffer state
 * to indicate the buffer is filled with data and ready for userspace.
 */
static void beaglelogic_unmap_buffer(struct device *dev,
                                     struct logic_buffer *buf)
{
	/* For coherent DMA memory:
	 * - Don't actually unmap (it stays mapped until freed)
	 * - Just update state for tracking purposes
	 */
	buf->state = STATE_BL_BUF_UNMAPPED;
}

/**
 * beaglelogic_map_and_submit_all_buffers - Prepare scatter-gather list for PRU
 * @dev: Device pointer
 *
 * Creates a scatter-gather list of buffer descriptors in PRU0 SRAM.
 * The PRU firmware reads this list to know where to write captured data.
 * The list is null-terminated.
 *
 * Return: 0 on success, 1 on failure
 */
static int beaglelogic_map_and_submit_all_buffers(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	struct buflist *pru_buflist = &bldev->cxt_pru->list_head;
	int i, j;
	dma_addr_t addr;

	if (!pru_buflist)
		return -1;

	/* Map all buffers (for coherent memory, this just verifies state) */
	for (i = 0; i < bldev->bufcount; i++) {
		if (beaglelogic_map_buffer(dev, &bldev->buffers[i]))
			goto fail;
	}

	/* Write buffer table to the PRU memory, and null terminate */
	for (i = 0; i < bldev->bufcount; i++) {
		addr = bldev->buffers[i].phys_addr;
		pru_buflist[i].dma_start_addr = addr;
		pru_buflist[i].dma_end_addr = addr + bldev->buffers[i].size;
	}
	pru_buflist[i].dma_start_addr = 0;
	pru_buflist[i].dma_end_addr = 0;

	/* Update state to ready */
	if (i)
		bldev->state = STATE_BL_ARMED;

	return 0;
	
fail:
	/* For coherent memory, we don't actually unmap, but update state */
	for (j = 0; j < i; j++)
		beaglelogic_unmap_buffer(dev, &bldev->buffers[j]);

	dev_err(dev, "DMA buffer preparation failed at i=%d\n", i);

	bldev->state = STATE_BL_ERROR;
	return 1;
}

/**
 * beaglelogic_fill_buffer_testpattern - Fill buffers with test pattern
 * @dev: Device pointer
 *
 * Fills all allocated buffers with a pattern of incrementing 32-bit integers.
 * This is useful for testing and debugging to detect dropped bytes or buffers.
 * Triggered by the IOCTL_BL_FILL_TEST_PATTERN ioctl command.
 */
static void beaglelogic_fill_buffer_testpattern(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int i, j;
	uint32_t cnt = 0, *addr;

	mutex_lock(&bldev->mutex);
	for (i = 0; i < bldev->bufcount; i++) {
		addr = bldev->buffers[i].buf;

		for (j = 0; j < bldev->buffers[i].size / sizeof(cnt); j++)
			*addr++ = cnt++;
	}
	mutex_unlock(&bldev->mutex);
}


/* ========================================================================
 * Device Configuration Section
 * ========================================================================
 *
 * Functions for getting and setting device parameters (sample rate,
 * sample unit, trigger flags). All setters use mutex locking to
 * ensure thread-safe configuration.
 */

/**
 * beaglelogic_get_samplerate - Get current sample rate
 * @dev: Device pointer
 *
 * Return: Current sample rate in Hz
 */
uint32_t beaglelogic_get_samplerate(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	return bldev->samplerate;
}

/**
 * beaglelogic_set_samplerate - Set desired sample rate
 * @dev: Device pointer
 * @samplerate: Requested sample rate in Hz
 *
 * Sets the sample rate to the nearest achievable value. The actual rate
 * is determined by the PRU core frequency divided by an integer divisor.
 *
 * Return: 0 on success, -EINVAL if rate is out of range, -EBUSY if device is in use
 */
int beaglelogic_set_samplerate(struct device *dev, uint32_t samplerate)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	/* Validate range: 1 Hz to half the core clock frequency */
	if (samplerate > bldev->coreclockfreq / 2 || samplerate < 1)
		return -EINVAL;

	if (mutex_trylock(&bldev->mutex)) {
		/* Calculate nearest achievable sample rate based on integer divisor */
		bldev->samplerate = (bldev->coreclockfreq / 2) /
				((bldev->coreclockfreq / 2) / samplerate);
		mutex_unlock(&bldev->mutex);
		return 0;
	}
	return -EBUSY;
}

uint32_t beaglelogic_get_sampleunit(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	return bldev->sampleunit;
}

int beaglelogic_set_sampleunit(struct device *dev, uint32_t sampleunit)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	if (sampleunit > 2)
		return -EINVAL;

	if (mutex_trylock(&bldev->mutex)) {
		bldev->sampleunit = sampleunit;
		mutex_unlock(&bldev->mutex);

		return 0;
	}
	return -EBUSY;
}

uint32_t beaglelogic_get_triggerflags(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	return bldev->triggerflags;
}

int beaglelogic_set_triggerflags(struct device *dev, uint32_t triggerflags)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	if (triggerflags > 1)
		return -EINVAL;

	if (mutex_trylock(&bldev->mutex)) {
		bldev->triggerflags = triggerflags;
		mutex_unlock(&bldev->mutex);

		return 0;
	}
	return -EBUSY;
}

/* ========================================================================
 * PRU Communication Section
 * ========================================================================
 *
 * Functions for sending commands to and receiving responses from the
 * PRU firmware through shared memory.
 */

/**
 * beaglelogic_send_cmd - Send command to PRU firmware
 * @bldev: BeagleLogic device pointer
 * @cmd: Command code (CMD_GET_VERSION, CMD_GET_MAX_SG, etc.)
 *
 * Writes a command to the shared memory structure and polls for completion.
 * The PRU firmware clears the command field and writes a response when done.
 *
 * Return: Response code from PRU on success, -1 on timeout
 */
static int beaglelogic_send_cmd(struct beaglelogicdev *bldev, uint32_t cmd)
{
#define TIMEOUT     200
	uint32_t timeout = TIMEOUT;

	bldev->cxt_pru->cmd = cmd;

	/* Wait for firmware to process the command */
	while (--timeout && bldev->cxt_pru->cmd != 0)
		cpu_relax();

	if (timeout == 0)
		return -1;

	return bldev->cxt_pru->resp;
}

/**
 * beaglelogic_request_stop - Signal PRU firmware to stop capture
 * @bldev: BeagleLogic device pointer
 *
 * Triggers a PRU interrupt to request the firmware stop capturing data.
 * For kernel 6.x, we write directly to the PRU INTC SISR register
 * since the pruss_intc_trigger() API was removed.
 *
 * The interrupt (SYSEV_ARM_TO_PRU0_A, event 23) signals the PRU to
 * finish the current buffer and halt.
 */
static void beaglelogic_request_stop(struct beaglelogicdev *bldev)
{
	/*
	 * Signal PRU0 to stop by writing to context structure stop_flag field.
	 * Stop flag location: PRU0 DRAM offset 0x18 (context.stop_flag)
	 *
	 * Context structure layout:
	 *   0x00: magic (4 bytes)
	 *   0x04: cmd (4 bytes)
	 *   0x08: resp (4 bytes)
	 *   0x0C: samplediv (4 bytes)
	 *   0x10: sampleunit (4 bytes)
	 *   0x14: triggerflags (4 bytes)
	 *   0x18: stop_flag (4 bytes) <-- write here
	 *   0x1C: buffer list starts
	 *
	 * In kernel 6.x, Event 23 (ARM→PRU0) cannot be triggered because
	 * it routes to Host 1 (PRU0), and the irq_pruss_intc driver only
	 * configures ARM-bound events (hosts 2-9).
	 *
	 * Solution: PRU0 firmware checks this flag once per buffer (continuous
	 * mode). Writing a non-zero value requests graceful stop.
	 */
	u32 __iomem *stop_flag = (u32 __iomem *)(bldev->pru0sram.va + 0x18);
	u32 readback;

	writel(1, stop_flag);  /* Set stop flag to non-zero */
	readback = readl(stop_flag);  /* Force write completion and verify */

	dev_info(bldev->p_dev, "Stop flag: wrote 1, readback %u (context.stop_flag at offset 0x18)\n",
		 readback);
}

/**
 * beaglelogic_serve_irq - Interrupt handler for PRU events
 * @irqno: IRQ number that triggered
 * @data: Pointer to beaglelogicdev structure
 *
 * Handles two types of interrupts from the PRU:
 * 1. Buffer ready (from_bl_irq_1): A buffer has been filled with data
 * 2. Capture complete (from_bl_irq_2): Capture session has ended
 *
 * Return: IRQ_HANDLED
 */
irqreturn_t beaglelogic_serve_irq(int irqno, void *data)
{
	struct beaglelogicdev *bldev = data;
	struct device *dev = bldev->miscdev.this_device;
	uint32_t state = bldev->state;

	dev_dbg(dev, "Beaglelogic IRQ #%d\n", irqno);
	if (irqno == bldev->from_bl_irq_1) {
		/* Manage the buffers */
		bldev->lastbufready = bldev->bufbeingread;
		beaglelogic_unmap_buffer(dev, bldev->lastbufready);

		/* Avoid a false buffer overrun warning on the last run */
		if (bldev->triggerflags != BL_TRIGGERFLAGS_ONESHOT ||
			bldev->bufbeingread->next->index != 0) {
			bldev->bufbeingread = bldev->bufbeingread->next;
			beaglelogic_map_buffer(dev, bldev->bufbeingread);
		}
		wake_up_interruptible(&bldev->wait);
	} else if (irqno == bldev->from_bl_irq_2) {
		/* This interrupt occurs twice:
		 *  1. After a successful configuration of PRU capture
		 *  2. After the last buffer transferred  */
		state = bldev->state;
		if (state <= STATE_BL_ARMED) {
			dev_dbg(dev, "config written, BeagleLogic ready\n");
			return IRQ_HANDLED;
		}
		else if (state != STATE_BL_REQUEST_STOP &&
				state != STATE_BL_RUNNING) {
			dev_err(dev, "Unexpected stop request \n");
			bldev->state = STATE_BL_ERROR;
			return IRQ_HANDLED;
		}
		dev_info(dev, "PRU stop acknowledged (state: %d -> INITIALIZED)\n", state);
		bldev->state = STATE_BL_INITIALIZED;

		/* In oneshot mode, PRU stops automatically when all buffers are filled.
		 * Release the mutex so the device can be restarted without closing the fd.
		 * In continuous mode with explicit stop, mutex is released by beaglelogic_stop(). */
		if (bldev->triggerflags == BL_TRIGGERFLAGS_ONESHOT &&
		    (state & STATE_BL_RUNNING)) {
			dev_info(dev, "Oneshot capture complete, releasing mutex for next capture\n");
			mutex_unlock(&bldev->mutex);
		}

		wake_up_interruptible(&bldev->wait);
	}

	return IRQ_HANDLED;
}

/**
 * beaglelogic_write_configuration - Send capture configuration to PRU
 * @dev: Device pointer
 *
 * Writes the current sample rate, sample unit, and trigger flags to
 * the shared memory structure and sends CMD_SET_CONFIG to the PRU.
 * The mutex must be held when calling this function.
 *
 * Return: 0 on success
 */
int beaglelogic_write_configuration(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int ret;

	/* Hand over the settings */
	bldev->cxt_pru->samplediv =
		(bldev->coreclockfreq / 2) / bldev->samplerate;
	bldev->cxt_pru->sampleunit = bldev->sampleunit;
	bldev->cxt_pru->triggerflags = bldev->triggerflags;
	ret = beaglelogic_send_cmd(bldev, CMD_SET_CONFIG);

	dev_dbg(dev, "PRU Config written, err code = %d\n", ret);
	return 0;
}

/**
 * beaglelogic_start - Begin data capture operation
 * @dev: Device pointer
 *
 * Configures the PRU firmware and starts the sampling operation.
 * This function acquires the device mutex and holds it for the
 * duration of the capture (released by beaglelogic_stop).
 *
 * Return: 0 on success, -1 on failure
 */
int beaglelogic_start(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	u32 __iomem *stop_flag;

	/* This mutex will be locked for the entire duration BeagleLogic runs */
	mutex_lock(&bldev->mutex);

	/* Clear stop flag at start of new capture session (context.stop_flag at offset 0x18) */
	stop_flag = (u32 __iomem *)(bldev->pru0sram.va + 0x18);
	writel(0, stop_flag);
	dev_info(dev, "Cleared stop flag (readback=%u)\n", readl(stop_flag));

	if (beaglelogic_write_configuration(dev)) {
		mutex_unlock(&bldev->mutex);
		return -1;
	}
	bldev->bufbeingread = &bldev->buffers[0];
	beaglelogic_send_cmd(bldev, CMD_START);

	/* All set now. Start the PRUs and wait for IRQs */
	bldev->state = STATE_BL_RUNNING;
	bldev->lasterror = 0;

	dev_info(dev, "capture started with sample rate=%d Hz, sampleunit=%d, "\
			"triggerflags=%d",
			bldev->samplerate,
			bldev->sampleunit,
			bldev->triggerflags);
	return 0;
}

/**
 * beaglelogic_stop - Stop ongoing data capture
 * @dev: Device pointer
 *
 * Requests the PRU to stop capturing and waits for it to complete.
 * The stop takes effect after the current buffer is filled.
 * Releases the device mutex acquired by beaglelogic_start().
 */
void beaglelogic_stop(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int ret;

	if (mutex_is_locked(&bldev->mutex)) {
		if (bldev->state == STATE_BL_RUNNING)
		{
			dev_info(dev, "Requesting PRU to stop capture (triggerflags=%d)\n",
				 bldev->triggerflags);
			beaglelogic_request_stop(bldev);
			bldev->state = STATE_BL_REQUEST_STOP;

			/* Wait for the PRU to signal completion (10 second timeout) */
			ret = wait_event_interruptible_timeout(bldev->wait,
					bldev->state == STATE_BL_INITIALIZED,
					msecs_to_jiffies(10000));

			/* Handle different return values */
			if (ret == 0) {
				/* Timeout - PRU did not respond, need hardware reset */
				dev_err(dev, "Stop timeout after 10 seconds - performing PRU hardware reset\n");
				dev_err(dev, "This may indicate a PRU firmware issue in continuous mode\n");

				/* Shutdown PRUs (they're in a bad state) */
				rproc_shutdown(bldev->pru1);
				rproc_shutdown(bldev->pru0);

				/* Reboot PRUs with fresh state */
				ret = rproc_boot(bldev->pru0);
				if (ret) {
					dev_err(dev, "Failed to reboot PRU0 after timeout: %d\n", ret);
					bldev->state = STATE_BL_ERROR;
				} else {
					ret = rproc_boot(bldev->pru1);
					if (ret) {
						dev_err(dev, "Failed to reboot PRU1 after timeout: %d\n", ret);
						bldev->state = STATE_BL_ERROR;
					} else {
						dev_info(dev, "PRUs successfully reset after timeout\n");
						bldev->state = STATE_BL_INITIALIZED;
					}
				}
			} else if (ret == -ERESTARTSYS) {
				/* Interrupted by signal (Ctrl+C) - force cleanup */
				dev_warn(dev, "Stop interrupted, forcing state reset\n");
				bldev->state = STATE_BL_INITIALIZED;
			}
			/* else: ret > 0 means success (remaining jiffies) */
		}
		/* Release */
		mutex_unlock(&bldev->mutex);

		dev_info(dev, "capture session ended\n");
	}
}

/* fops */
static int beaglelogic_f_open(struct inode *inode, struct file *filp)
{
	struct logic_buffer_reader *reader;
	struct miscdevice *miscdev = filp->private_data;
	struct beaglelogicdev *bldev = container_of(miscdev, struct beaglelogicdev, miscdev);
	struct device *dev = bldev->miscdev.this_device;
	int i;

	if (bldev->bufcount == 0)
		return -ENOMEM;

	reader = devm_kzalloc(dev, sizeof(*reader), GFP_KERNEL);
	if (!reader)
		return -ENOMEM;
	
	reader->bldev = bldev;
	reader->buf = NULL;
	reader->pos = 0;
	reader->remaining = 0;

	filp->private_data = reader;

	/* The buffers will be mapped/resubmitted at the time of allocation
	 * Here, we just map the first buffer */
	if (!bldev->buffers)
		return -ENOMEM;

	/* ADDED FIX: For coherent DMA memory, verify/fix buffer states
	 * Coherent memory is already mapped at allocation time, but the
	 * state might not be set correctly. This prevents the
	 * "Buffer not in mapped state" error. */
	mutex_lock(&bldev->mutex);
	for (i = 0; i < bldev->bufcount; i++) {
		if (bldev->buffers[i].state != STATE_BL_BUF_MAPPED) {
			/* If buffer has a valid physical address, it's coherent
			 * memory that's already mapped - just fix the state */
			if (bldev->buffers[i].phys_addr != 0) {
				dev_info(dev, "Correcting buffer %d state to MAPPED\n", i);
				bldev->buffers[i].state = STATE_BL_BUF_MAPPED;
			} else {
				dev_err(dev, "Buffer %d has no physical address!\n", i);
				mutex_unlock(&bldev->mutex);
				devm_kfree(dev, reader);
				return -EINVAL;
			}
		}
	}
	mutex_unlock(&bldev->mutex);

	/* Map the first buffer (for coherent memory, this just verifies state) */
	beaglelogic_map_buffer(dev, &bldev->buffers[0]);

	return 0;
}

/* Read the sample (ring) buffer. */
ssize_t beaglelogic_f_read (struct file *filp, char __user *buf,
                          size_t sz, loff_t *offset)
{
	int count;
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;
	struct device *dev = bldev->miscdev.this_device;

	if (bldev->state == STATE_BL_ERROR)
		return -EIO;

	if (reader->pos > 0)
		goto perform_copy;

	if (reader->buf == NULL) {
		/* First time init */
		reader->buf = &reader->bldev->buffers[0];
		reader->remaining = reader->buf->size;

		if (bldev->state != STATE_BL_RUNNING) {
			/* Start the capture */
			if (beaglelogic_start(dev))
				return -ENOEXEC;
		}
	} else {
		/* EOF Condition, back to buffer 0 and stopped */
		if (reader->buf == bldev->buffers &&
				bldev->state == STATE_BL_INITIALIZED)
			return 0;
	}

	if (filp->f_flags & O_NONBLOCK) {
		if (reader->buf->state != STATE_BL_BUF_UNMAPPED)
			return -EAGAIN;
	} else {
		if (wait_event_interruptible(bldev->wait,
				reader->buf->state == STATE_BL_BUF_UNMAPPED))
			return -ERESTARTSYS;
	}
perform_copy:
	count = min(reader->remaining, sz);

	if (copy_to_user(buf, reader->buf->buf + reader->pos, count))
		return -EFAULT;

	/* Detect buffer drop */
	if (reader->buf->state == STATE_BL_BUF_MAPPED) {
		dev_warn(dev, "buffer may be dropped at index %d \n",
				reader->buf->index);
		reader->buf->state = STATE_BL_BUF_DROPPED;
		bldev->lasterror = 0x10000 | reader->buf->index;
	}

	reader->pos += count;
	reader->remaining -= count;

	if (reader->remaining == 0) {
		/* Change the buffer */
		reader->buf = reader->buf->next;
		reader->pos = 0;
		reader->remaining = reader->buf->size;
	}

	return count;
}

/* Map the PRU buffers to user space [cache coherency managed by driver] */
int beaglelogic_f_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int i, ret;
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;

	unsigned long addr = vma->vm_start;

	if (vma->vm_end - vma->vm_start > bldev->bufunitsize * bldev->bufcount)
		return -EINVAL;

	for (i = 0; i < bldev->bufcount; i++) {
		ret = remap_pfn_range(vma, addr,
				(bldev->buffers[i].phys_addr) >> PAGE_SHIFT,
				bldev->buffers[i].size,
				vma->vm_page_prot);

		if (ret)
			return -EINVAL;

		addr += bldev->buffers[i].size;
	}
	return 0;
}

/* Configuration through ioctl */
static long beaglelogic_f_ioctl(struct file *filp, unsigned int cmd,
		  unsigned long arg)
{
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;
	struct device *dev = bldev->miscdev.this_device;

	uint32_t val;

	dev_dbg(dev, "BeagleLogic: IOCTL called cmd = %08X, "\
			"arg = %08lX\n", cmd, arg);

	switch (cmd) {
		case IOCTL_BL_GET_VERSION:
			return 0;

		case IOCTL_BL_GET_SAMPLE_RATE:
			if (copy_to_user((void * __user)arg,
					&bldev->samplerate,
					sizeof(bldev->samplerate)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_SET_SAMPLE_RATE:
			if (beaglelogic_set_samplerate(dev, (uint32_t)arg))
				return -EFAULT;
			return 0;

		case IOCTL_BL_GET_SAMPLE_UNIT:
			if (copy_to_user((void * __user)arg,
					&bldev->sampleunit,
					sizeof(bldev->sampleunit)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_SET_SAMPLE_UNIT:
			if (beaglelogic_set_sampleunit(dev, (uint32_t)arg))
				return -EFAULT;
			return 0;

		case IOCTL_BL_GET_TRIGGER_FLAGS:
			if (copy_to_user((void * __user)arg,
					&bldev->triggerflags,
					sizeof(bldev->triggerflags)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_SET_TRIGGER_FLAGS:
			if (beaglelogic_set_triggerflags(dev, (uint32_t)arg))
				return -EFAULT;
			return 0;

		case IOCTL_BL_GET_CUR_INDEX:
			if (copy_to_user((void * __user)arg,
					&bldev->bufbeingread->index,
					sizeof(bldev->bufbeingread->index)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_CACHE_INVALIDATE:
			for (val = 0; val < bldev->bufcount; val++) {
				beaglelogic_unmap_buffer(dev,
						&bldev->buffers[val]);
			}
			return 0;

		case IOCTL_BL_GET_BUFFER_SIZE:
			val = bldev->bufunitsize * bldev->bufcount;
			if (copy_to_user((void * __user)arg,
					&val,
					sizeof(val)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_SET_BUFFER_SIZE:
			beaglelogic_memfree(dev);
			val = beaglelogic_memalloc(dev, arg);
			if (!val)
				return beaglelogic_map_and_submit_all_buffers(dev);
			return val;

		case IOCTL_BL_GET_BUFUNIT_SIZE:
			if (copy_to_user((void * __user)arg,
					&bldev->bufunitsize,
					sizeof(bldev->bufunitsize)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_SET_BUFUNIT_SIZE:
			if ((uint32_t)arg < 32)
				return -EINVAL;
			bldev->bufunitsize = round_up(arg, 32);
			beaglelogic_memfree(dev);
			return 0;

		case IOCTL_BL_FILL_TEST_PATTERN:
			beaglelogic_fill_buffer_testpattern(dev);
			return 0;

		case IOCTL_BL_START:
			/* Reset and reconfigure the reader object and then start */
			reader->buf = &bldev->buffers[0];
			reader->pos = 0;
			reader->remaining = reader->buf->size;

			beaglelogic_start(dev);
			return 0;

		case IOCTL_BL_STOP:
			beaglelogic_stop(dev);
			return 0;

	}
	return -ENOTTY;
}

/* llseek to offset zero resets the LA */
static loff_t beaglelogic_f_llseek(struct file *filp, loff_t offset, int whence)
{
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;
	struct device *dev = bldev->miscdev.this_device;

	loff_t i = offset;
	uint32_t j;

	if (whence == SEEK_CUR) {
		while (i > 0) {
			if (reader->buf->state == STATE_BL_BUF_MAPPED) {
				dev_warn(dev, "buffer may be dropped at index %d \n",
						reader->buf->index);
				reader->buf->state = STATE_BL_BUF_DROPPED;
				bldev->lasterror = 0x10000 | reader->buf->index;
			}

			j = min((uint32_t)i, reader->remaining);
			reader->pos += j;

			if ((reader->remaining -= j) == 0) {
				/* Change the buffer */
				reader->buf = reader->buf->next;
				reader->pos = 0;
				reader->remaining = reader->buf->size;
			}

			i -= j;
		}
		return offset;
	}

	if (whence == SEEK_SET && offset == 0) {
		/* The next read triggers the LA */
		reader->buf = NULL;
		reader->pos = 0;
		reader->remaining = 0;

		/* Stop and map the first buffer */
		beaglelogic_stop(dev);
		beaglelogic_map_buffer(dev, &reader->bldev->buffers[0]);

		return 0;
	}

	return -EINVAL;
}

/* Poll the file descriptor */
unsigned int beaglelogic_f_poll(struct file *filp,
		struct poll_table_struct *tbl)
{
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;
	struct logic_buffer *buf;

	/* Raise an error if polled without starting the LA first */
	if (reader->buf == NULL && bldev->state != STATE_BL_RUNNING)
		return -ENOEXEC;

	buf = reader->buf;
	if (buf->state == STATE_BL_BUF_UNMAPPED)
		return (POLLIN | POLLRDNORM);

	poll_wait(filp, &bldev->wait, tbl);

	return 0;
}

/* Device file close handler */
static int beaglelogic_f_release(struct inode *inode, struct file *filp)
{
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;
	struct device *dev = bldev->miscdev.this_device;

	/* Stop & Release */
	beaglelogic_stop(dev);
	devm_kfree(dev, reader);

	return 0;
}

/* File operations struct */
static const struct file_operations pru_beaglelogic_fops = {
	.owner = THIS_MODULE,
	.open = beaglelogic_f_open,
	.unlocked_ioctl = beaglelogic_f_ioctl,
	.read = beaglelogic_f_read,
	.llseek = beaglelogic_f_llseek,
	.mmap = beaglelogic_f_mmap,
	.poll = beaglelogic_f_poll,
	.release = beaglelogic_f_release,
};
/* fops */

/* begin sysfs attrs */
static ssize_t bl_bufunitsize_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bldev->bufunitsize);
}

static ssize_t bl_bufunitsize_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	uint32_t val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	if (val < 32)
		return -EINVAL;

	bldev->bufunitsize = round_up(val, 32);

	/* Free up previously allocated buffers */
	beaglelogic_memfree(dev);

	return count;
}

static ssize_t bl_memalloc_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			bldev->bufcount * bldev->bufunitsize);
}

static ssize_t bl_memalloc_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	uint32_t val;
	int ret;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	/* Check value of memory to reserve */
	if (val > bldev->maxbufcount * bldev->bufunitsize)
		return -EINVAL;

	/* Free buffers and reallocate */
	beaglelogic_memfree(dev);
	ret = beaglelogic_memalloc(dev, val);

	if (!ret)
		beaglelogic_map_and_submit_all_buffers(dev);
	else
		return ret;

	return count;
}

static ssize_t bl_samplerate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			beaglelogic_get_samplerate(dev));
}

static ssize_t bl_samplerate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	/* Check value of sample rate - 100 kHz to 100MHz */
	if (beaglelogic_set_samplerate(dev, val))
		return -EINVAL;

	return count;
}

static ssize_t bl_sampleunit_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint32_t ret = beaglelogic_get_sampleunit(dev);
	int cnt = scnprintf(buf, PAGE_SIZE, "%d:", ret);

	switch (ret)
	{
		case BL_SAMPLEUNIT_16_BITS:
			cnt += scnprintf(buf, PAGE_SIZE, "16bit\n");
			break;

		case BL_SAMPLEUNIT_8_BITS:
			cnt += scnprintf(buf, PAGE_SIZE, "8bit\n");
			break;
	}
	return cnt;
}

static ssize_t bl_sampleunit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	uint32_t val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	/* Check value of sample unit - only 0 or 1 currently */
	if ((err = beaglelogic_set_sampleunit(dev, val)))
		return err;

	return count;
}

static ssize_t bl_triggerflags_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	switch (beaglelogic_get_triggerflags(dev)) {
		case BL_TRIGGERFLAGS_ONESHOT:
			return scnprintf(buf, PAGE_SIZE, "0:oneshot\n");

		case BL_TRIGGERFLAGS_CONTINUOUS:
			return scnprintf(buf, PAGE_SIZE, "1:continuous\n");
	}
	return 0;
}

static ssize_t bl_triggerflags_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t val;
	int err;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	if ((err = beaglelogic_set_triggerflags(dev, val)))
		return err;

	return count;
}

static ssize_t bl_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	uint32_t state = bldev->state;
	struct logic_buffer *buffer = bldev->bufbeingread;

	if (state == STATE_BL_RUNNING) {
		/* State blocks and returns last buffer read */
		wait_event_interruptible(bldev->wait,
				buffer->state == STATE_BL_BUF_UNMAPPED);
		return scnprintf(buf, PAGE_SIZE, "%d\n", buffer->index);
	}

	/* Identify non-buffer debug states with a -ve value */
	return scnprintf(buf, PAGE_SIZE, "-%d\n", -bldev->state);
}

static ssize_t bl_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	/* State going to 1 starts the sampling operation, 0 aborts*/
	if (val > 1)
		return -EINVAL;

	if (val == 1)
		beaglelogic_start(dev);
	else
		beaglelogic_stop(dev);

	return count;
}

static ssize_t bl_buffers_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int i, c, cnt;

	for (i = 0, c = 0, cnt = 0; i < bldev->bufcount; i++) {
		c = scnprintf(buf, PAGE_SIZE, "%08x,%u\n",
				(uint32_t)bldev->buffers[i].phys_addr,
				bldev->buffers[i].size);
		cnt += c;
		buf += c;
	}

	return cnt;
}

static ssize_t bl_lasterror_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	wait_event_interruptible(bldev->wait,
			bldev->state != STATE_BL_RUNNING);


	return scnprintf(buf, PAGE_SIZE, "%d\n", bldev->lasterror);
}

static ssize_t bl_testpattern_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	/* Only if we get the magic number, trigger the test pattern */
	if (val == 12345678)
		beaglelogic_fill_buffer_testpattern(dev);

	return count;
}

static DEVICE_ATTR(bufunitsize, S_IWUSR | S_IRUGO,
		bl_bufunitsize_show, bl_bufunitsize_store);

static DEVICE_ATTR(memalloc, S_IWUSR | S_IRUGO,
		bl_memalloc_show, bl_memalloc_store);

static DEVICE_ATTR(samplerate, S_IWUSR | S_IRUGO,
		bl_samplerate_show, bl_samplerate_store);

static DEVICE_ATTR(sampleunit, S_IWUSR | S_IRUGO,
		bl_sampleunit_show, bl_sampleunit_store);

static DEVICE_ATTR(triggerflags, S_IWUSR | S_IRUGO,
		bl_triggerflags_show, bl_triggerflags_store);

static DEVICE_ATTR(state, S_IWUSR | S_IRUGO,
		bl_state_show, bl_state_store);

static DEVICE_ATTR(buffers, S_IRUGO,
		bl_buffers_show, NULL);

static DEVICE_ATTR(lasterror, S_IRUGO,
		bl_lasterror_show, NULL);

static DEVICE_ATTR(filltestpattern, S_IWUSR,
		NULL, bl_testpattern_store);

static struct attribute *beaglelogic_attributes[] = {
	&dev_attr_bufunitsize.attr,
	&dev_attr_memalloc.attr,
	&dev_attr_samplerate.attr,
	&dev_attr_sampleunit.attr,
	&dev_attr_triggerflags.attr,
	&dev_attr_state.attr,
	&dev_attr_buffers.attr,
	&dev_attr_lasterror.attr,
	&dev_attr_filltestpattern.attr,
	NULL
};

static struct attribute_group beaglelogic_attr_group = {
	.attrs = beaglelogic_attributes
};
/* end sysfs attrs */

static const struct of_device_id beaglelogic_dt_ids[];

static int beaglelogic_probe(struct platform_device *pdev)
{
	struct beaglelogicdev *bldev;
	struct device *dev;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;
	int ret;
	uint32_t val;


	// EDIT: Added new enum for handling changes to pru_rproc_get
	enum pruss_pru_id id0, id1;;

	match = of_match_device(beaglelogic_dt_ids, &pdev->dev);
	if (!match)
		return -ENODEV;

	/* Allocate memory for our private structure */
	bldev = kzalloc(sizeof(*bldev), GFP_KERNEL);

	if (!node)
		return -ENODEV; /* No support for non-DT platforms */

	if (!bldev) {
		ret = -1;
		goto fail;
	}

	bldev->fw_data = match->data;
	bldev->miscdev.fops = &pru_beaglelogic_fops;
	bldev->miscdev.minor = MISC_DYNAMIC_MINOR;
	bldev->miscdev.mode = S_IRUGO;
	bldev->miscdev.name = "beaglelogic";

	/* Link the platform device data to our private structure */
	bldev->p_dev = &pdev->dev;
	dev_set_drvdata(bldev->p_dev, bldev);

	/* Get a handle to the PRUSS structures */
	dev = &pdev->dev;


	/* Initialize DMA mask pointer if not already set
	 * Required for dma_set_mask_and_coherent() to work properly
	 * This prevents WARNING at dma/mapping.c:638 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;

	/* Set both DMA masks to avoid kernel warning
	 * Must be done before first dma_alloc_coherent() call */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "Failed to set DMA mask: %d\n", ret);
		return ret;
	}

	// EDIT: changed pru_rproc_get to coincide with new kernel requirements
	bldev->pru0 = pru_rproc_get(node, 0, &id0);

	if (IS_ERR(bldev->pru0)) {
		ret = PTR_ERR(bldev->pru0);
		//dev_err(dev, ret);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to get PRU0.\n");
		goto fail_free;
	}

	bldev->pruss = pruss_get(bldev->pru0);
	if (IS_ERR(bldev->pruss)) {
		ret = PTR_ERR(bldev->pruss);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to get pruss handle.\n");
		goto fail_pru0_put;
	}

	// EDIT: changed pru_rproc_get to coincide with new kernel requirements
	bldev->pru1 = pru_rproc_get(node, 1, &id1);
	if (IS_ERR(bldev->pru1)) {
		ret = PTR_ERR(bldev->pru1);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to get PRU0.\n");
		goto fail_pruss_put;
	}

	ret = pruss_request_mem_region(bldev->pruss, PRUSS_MEM_DRAM0,
		&bldev->pru0sram);
	if (ret) {
		dev_err(dev, "Unable to get PRUSS RAM.\n");
		goto fail_putmem;
	}

	/* Map PRU INTC memory for direct register access (kernel 6.x) */
	bldev->prussio_vaddr = ioremap(0x4A320000, 0x2000);
	if (!bldev->prussio_vaddr) {
		dev_err(dev, "Failed to map PRU INTC memory\n");
		ret = -ENOMEM;
		goto fail_putmem;
	}
	dev_info(dev, "Mapped PRU INTC memory at 0x4A320000\n");

	/*
	 * Manually configure Event 23 (ARM→PRU0 stop signal) routing.
	 * In kernel 6.x, remoteproc doesn't load .pru_irq_map from firmware,
	 * so we must configure INTC registers directly.
	 *
	 * Event 23 → Channel 1 → Host 1 (PRU0 R31.31)
	 *
	 * NOTE: INTC registers are protected - must disable GER to modify them.
	 */
	{
		void __iomem *ger   = bldev->prussio_vaddr + 0x010;  /* Global Enable Register */
		void __iomem *hieisr = bldev->prussio_vaddr + 0x034;  /* Host Interrupt Enable Indexed Set */
		void __iomem *cmr5  = bldev->prussio_vaddr + 0x404;  /* Channel Map 5 (events 20-23) */
		void __iomem *hmr0  = bldev->prussio_vaddr + 0x804;  /* Host Map 0 (channels 0-3) */
		uint32_t ger_saved, cmr5_val, hmr0_val;

		/* Save and disable INTC to allow register modifications */
		ger_saved = readl(ger);
		writel(0, ger);

		/* Read current values */
		cmr5_val = readl(cmr5);
		hmr0_val = readl(hmr0);

		/* Set Event 23 → Channel 1 (bits 12-15 of CMR5) */
		cmr5_val = (cmr5_val & ~(0xF << 12)) | (1 << 12);
		writel(cmr5_val, cmr5);

		/* Set Channel 1 → Host 1 (bits 4-7 of HMR0) */
		hmr0_val = (hmr0_val & ~(0xF << 4)) | (1 << 4);
		writel(hmr0_val, hmr0);

		/* Enable Host Interrupt 1 (PRU0) */
		writel(1, hieisr);  /* Index 1 = Host Interrupt 1 */

		/* Restore INTC enable state */
		writel(ger_saved, ger);

		dev_info(dev, "Configured Event 23 → Channel 1 → Host 1 (CMR5=0x%08x, HMR0=0x%08x, GER=0x%x)\n",
			 readl(cmr5), readl(hmr0), readl(ger));
	}

	/* Get interrupts and install interrupt handlers */
	bldev->from_bl_irq_1 = platform_get_irq_byname(pdev, "from_bl_1");
	if (bldev->from_bl_irq_1 <= 0) {
		ret = bldev->from_bl_irq_1;
		if (ret == -EPROBE_DEFER)
			goto fail_putmem;
	}
	bldev->from_bl_irq_2 = platform_get_irq_byname(pdev, "from_bl_2");
	if (bldev->from_bl_irq_2 <= 0) {
		ret = bldev->from_bl_irq_2;
		if (ret == -EPROBE_DEFER)
			goto fail_putmem;
	}
	/*
	 * Note: We don't get "to_bl" interrupt from device tree because
	 * Event 23 (ARM->PRU0) is configured in PRU firmware and triggered
	 * via direct INTC register write (see beaglelogic_request_stop).
	 */

	/* Set firmware and boot the PRUs */
	ret = rproc_set_firmware(bldev->pru0, bldev->fw_data->fw_names[0]);
	if (ret) {
		dev_err(dev, "Failed to set PRU0 firmware %s: %d\n",
			bldev->fw_data->fw_names[0], ret);
		goto fail_putmem;
	}

	ret = rproc_set_firmware(bldev->pru1, bldev->fw_data->fw_names[1]);
	if (ret) {
		dev_err(dev, "Failed to set PRU1 firmware %s: %d\n",
			bldev->fw_data->fw_names[1], ret);
		goto fail_putmem;
	}

	ret = rproc_boot(bldev->pru0);
	if (ret) {
		dev_err(dev, "Failed to boot PRU0: %d\n", ret);
		goto fail_putmem;
	}

	ret = rproc_boot(bldev->pru1);
	if (ret) {
		dev_err(dev, "Failed to boot PRU1: %d\n", ret);
		goto fail_shutdown_pru0;
	}

	ret = request_irq(bldev->from_bl_irq_1, beaglelogic_serve_irq,
		IRQF_ONESHOT, dev_name(dev), bldev);
	if (ret) goto fail_shutdown_prus;

	ret = request_irq(bldev->from_bl_irq_2, beaglelogic_serve_irq,
		IRQF_ONESHOT, dev_name(dev), bldev);
	if (ret) goto fail_free_irq1;

	printk("BeagleLogic loaded and initializing\n");

	/* Once done, register our misc device and link our private data */
	ret = misc_register(&bldev->miscdev);
	if (ret)
		goto fail_free_irqs;
	dev = bldev->miscdev.this_device;
	dev_set_drvdata(dev, bldev);

	/* Set up locks */
	mutex_init(&bldev->mutex);
	init_waitqueue_head(&bldev->wait);

	/* Core clock frequency is 200 MHz */
	bldev->coreclockfreq = 200000000;

	/* Power on in disabled state */
	bldev->state = STATE_BL_DISABLED;

	/* Capture context structure is at location 0000h in PRU0 SRAM */
	bldev->cxt_pru = bldev->pru0sram.va + 0;

	if (bldev->cxt_pru->magic == BL_FW_MAGIC)
		dev_info(dev, "Valid PRU capture context structure "\
				"found at offset %04X\n", 0);
	else {
		dev_err(dev, "Firmware error!\n");
		goto faildereg;
	}

	/* Get firmware properties */
	ret = beaglelogic_send_cmd(bldev, CMD_GET_VERSION);
	if (ret != 0) {
		dev_info(dev, "BeagleLogic PRU Firmware version: %d.%d\n",
				ret >> 8, ret & 0xFF);
	} else {
		dev_err(dev, "Firmware error!\n");
		goto faildereg;
	}

	ret = beaglelogic_send_cmd(bldev, CMD_GET_MAX_SG);
	if (ret > 0 && ret < 256) { /* Let's be reasonable here */
		dev_info(dev, "Device supports max %d vector transfers\n", ret);
		bldev->maxbufcount = ret;
	} else {
		dev_err(dev, "Firmware error!\n");
		goto faildereg;
	}

	/* Apply default configuration first */
	bldev->samplerate = 100 * 1000 * 1000;
	bldev->sampleunit = 1;
	bldev->bufunitsize = 4 * 1024 * 1024;
	bldev->triggerflags = 0;

	/* Override defaults with the device tree */
	if (!of_property_read_u32(node, "samplerate", &val))
		if (beaglelogic_set_samplerate(dev, val))
			dev_warn(dev, "Invalid default samplerate\n");

	if (!of_property_read_u32(node, "sampleunit", &val))
		if (beaglelogic_set_sampleunit(dev, val))
			dev_warn(dev, "Invalid default sampleunit\n");

	if (!of_property_read_u32(node, "triggerflags", &val))
		if (beaglelogic_set_triggerflags(dev, val))
			dev_warn(dev, "Invalid default triggerflags\n");

	/* We got configuration from PRUs, now mark device init'd */
	bldev->state = STATE_BL_INITIALIZED;

	/* Display our init'ed state */
	dev_info(dev, "Default sample rate=%d Hz, sampleunit=%d, "\
			"triggerflags=%d. Buffer in units of %d bytes each",
			bldev->samplerate,
			bldev->sampleunit,
			bldev->triggerflags,
			bldev->bufunitsize);

	/* Once done, create device files */
	ret = sysfs_create_group(&dev->kobj, &beaglelogic_attr_group);
	if (ret) {
		dev_err(dev, "Registration failed.\n");
		goto faildereg;
	}

	return 0;
faildereg:
	misc_deregister(&bldev->miscdev);
fail_free_irqs:
	free_irq(bldev->from_bl_irq_2, bldev);
fail_free_irq1:
	free_irq(bldev->from_bl_irq_1, bldev);
fail_shutdown_prus:
	rproc_shutdown(bldev->pru1);
fail_shutdown_pru0:
	rproc_shutdown(bldev->pru0);
fail_putmem:
	if (bldev->pru0sram.va)
		pruss_release_mem_region(bldev->pruss, &bldev->pru0sram);
	pru_rproc_put(bldev->pru1);
fail_pruss_put:
	pruss_put(bldev->pruss);
fail_pru0_put:
	pru_rproc_put(bldev->pru0);
fail_free:
	kfree(bldev);
fail:
	return ret;
}

static void beaglelogic_remove(struct platform_device *pdev)
{
	struct beaglelogicdev *bldev = platform_get_drvdata(pdev);
	struct device *dev = bldev->miscdev.this_device;

	/* Free all buffers */
	beaglelogic_memfree(dev);

	/* Remove the sysfs attributes */
	sysfs_remove_group(&dev->kobj, &beaglelogic_attr_group);

	/* Deregister the misc device */
	misc_deregister(&bldev->miscdev);

	/* Unmap PRU INTC memory */
	if (bldev->prussio_vaddr)
		iounmap(bldev->prussio_vaddr);

	/* Shutdown the PRUs */
	rproc_shutdown(bldev->pru1);
	rproc_shutdown(bldev->pru0);

	/* Free IRQs */
	free_irq(bldev->from_bl_irq_2, bldev);
	free_irq(bldev->from_bl_irq_1, bldev);

	/* Release handles to PRUSS memory regions */
	pruss_release_mem_region(bldev->pruss, &bldev->pru0sram);
	pru_rproc_put(bldev->pru1);
	pruss_put(bldev->pruss);
	pru_rproc_put(bldev->pru0);

	/* Free up memory */
	kfree(bldev);

	/* Print a log message to announce unloading */
	printk("BeagleLogic unloaded\n");
	// Removed return for kernel 6.x change
	//return 0;
}

static struct beaglelogic_private_data beaglelogic_pdata = {
	.fw_names[0] = "beaglelogic-pru0-fw",
	.fw_names[1] = "beaglelogic-pru1-fw",
};

static const struct of_device_id beaglelogic_dt_ids[] = {
	{ .compatible = "beaglelogic,beaglelogic", .data = &beaglelogic_pdata, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, beaglelogic_dt_ids);

static struct platform_driver beaglelogic_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = beaglelogic_dt_ids,
	},
	.probe = beaglelogic_probe,
	.remove = beaglelogic_remove,
};

module_platform_driver(beaglelogic_driver);

MODULE_AUTHOR("Kumar Abhishek <abhishek@theembeddedkitchen.net>, Bryan Rainwater");
MODULE_DESCRIPTION("Kernel Driver for BeagleLogic (updated for kernel 6.x)");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

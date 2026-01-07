/**
 * @file beaglelogic.h
 * @brief Userspace/Kernelspace common API for BeagleLogic Logic Analyzer
 *
 * This header defines the shared interface between kernel driver and userspace
 * applications, including ioctl commands, state machine definitions, and
 * configuration enumerations.
 *
 * Copyright (C) 2014-17 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef BEAGLELOGIC_H_
#define BEAGLELOGIC_H_

/**
 * enum beaglelogic_states - State machine states for BeagleLogic device
 *
 * The device transitions through these states during its lifecycle:
 * DISABLED -> INITIALIZED -> MEMALLOCD -> ARMED -> RUNNING -> INITIALIZED
 *
 * @STATE_BL_DISABLED: Initial state at module load, device powered off
 * @STATE_BL_INITIALIZED: Device powered on and ready for configuration
 * @STATE_BL_MEMALLOCD: DMA buffers allocated in memory
 * @STATE_BL_ARMED: Buffers DMA-mapped and PRU firmware configured
 * @STATE_BL_RUNNING: Active data capture in progress
 * @STATE_BL_REQUEST_STOP: Stop command issued, waiting for current buffer
 * @STATE_BL_ERROR: Error condition detected (e.g., buffer overrun)
 */
enum beaglelogic_states {
	STATE_BL_DISABLED,
	STATE_BL_INITIALIZED,
	STATE_BL_MEMALLOCD,
	STATE_BL_ARMED,
	STATE_BL_RUNNING,
	STATE_BL_REQUEST_STOP,
	STATE_BL_ERROR
};

/**
 * enum beaglelogic_triggerflags - Capture mode configuration
 *
 * @BL_TRIGGERFLAGS_ONESHOT: Capture stops after filling all buffers once
 * @BL_TRIGGERFLAGS_CONTINUOUS: Continuous circular buffer capture mode
 */
enum beaglelogic_triggerflags {
	BL_TRIGGERFLAGS_ONESHOT = 0,
	BL_TRIGGERFLAGS_CONTINUOUS
};

/**
 * enum beaglelogic_sampleunit - Sample data width configuration
 *
 * @BL_SAMPLEUNIT_16_BITS: Capture 16 channels (16-bit samples)
 * @BL_SAMPLEUNIT_8_BITS: Capture 8 channels (8-bit samples, higher rate)
 */
enum beaglelogic_sampleunit {
	BL_SAMPLEUNIT_16_BITS = 0,
	BL_SAMPLEUNIT_8_BITS
};

/**
 * IOCTL Commands for /dev/beaglelogic device file
 *
 * These commands provide the userspace API for configuring and controlling
 * the BeagleLogic logic analyzer. All commands use the 'k' magic number.
 */

/* Version Information */
#define IOCTL_BL_GET_VERSION        _IOR('k', 0x20, u32)  /* Get driver version */

/* Sample Rate Configuration (in Hz) */
#define IOCTL_BL_GET_SAMPLE_RATE    _IOR('k', 0x21, u32)  /* Get current sample rate */
#define IOCTL_BL_SET_SAMPLE_RATE    _IOW('k', 0x21, u32)  /* Set sample rate (Hz) */

/* Sample Unit Configuration (8-bit or 16-bit) */
#define IOCTL_BL_GET_SAMPLE_UNIT    _IOR('k', 0x22, u32)  /* Get sample width */
#define IOCTL_BL_SET_SAMPLE_UNIT    _IOW('k', 0x22, u32)  /* Set sample width */

/* Trigger Mode Configuration (one-shot or continuous) */
#define IOCTL_BL_GET_TRIGGER_FLAGS  _IOR('k', 0x23, u32)  /* Get trigger mode */
#define IOCTL_BL_SET_TRIGGER_FLAGS  _IOW('k', 0x23, u32)  /* Set trigger mode */

/* Buffer Status and Management */
#define IOCTL_BL_GET_CUR_INDEX      _IOR('k', 0x24, u32)  /* Get current buffer index */
#define IOCTL_BL_CACHE_INVALIDATE    _IO('k', 0x25)       /* Invalidate buffer cache */

/* Buffer Size Configuration (total and per-unit) */
#define IOCTL_BL_GET_BUFFER_SIZE    _IOR('k', 0x26, u32)  /* Get total buffer size */
#define IOCTL_BL_SET_BUFFER_SIZE    _IOW('k', 0x26, u32)  /* Set total buffer size */

#define IOCTL_BL_GET_BUFUNIT_SIZE   _IOR('k', 0x27, u32)  /* Get buffer unit size */
#define IOCTL_BL_SET_BUFUNIT_SIZE   _IOW('k', 0x27, u32)  /* Set buffer unit size */

/* Testing and Debugging */
#define IOCTL_BL_FILL_TEST_PATTERN   _IO('k', 0x28)       /* Fill buffers with test pattern */

/* Capture Control */
#define IOCTL_BL_START               _IO('k', 0x29)       /* Start data capture */
#define IOCTL_BL_STOP                _IO('k', 0x2A)       /* Stop data capture */

#endif /* BEAGLELOGIC_H_ */

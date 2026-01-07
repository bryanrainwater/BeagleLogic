/****************************************************************************/
/*  AM335x_PRU1.cmd - Linker Command File for PRU1 BeagleLogic Firmware    */
/*  Copyright (c) 2015-2021  Texas Instruments Incorporated                 */
/*                                                                          */
/*  Description:                                                            */
/*  This linker script defines the memory map and section placement for    */
/*  PRU1 firmware running on AM335x (BeagleBone) processors.               */
/*                                                                          */
/*  Key Differences from PRU0 linker script:                               */
/*  - No .pru_irq_map section (PRU1 doesn't configure interrupt routing)   */
/*  - PRU1 only sends interrupts, doesn't receive or route them            */
/*  - Memory map is otherwise identical to PRU0                            */
/*                                                                          */
/*  In BeagleLogic architecture:                                           */
/*  - PRU1 performs high-speed GPIO sampling                               */
/*  - PRU1 signals PRU0 when data is ready (via scratchpad + interrupt)    */
/*  - PRU0 handles all interrupt routing and ARM communication             */
/****************************************************************************/

-cr								/* Link using C conventions */

/*==========================================================================*/
/* Memory Map for AM335x PRU Subsystem (PRU1)                              */
/*                                                                          */
/* Note: PRU1 uses its own instruction RAM at 0x00000000 (physical         */
/* 0x34000 in PRU-ICSS address space) and its own data RAM. However, the   */
/* linker script uses local addressing (0x00000000) which is remapped by   */
/* the PRU subsystem hardware.                                             */
/*==========================================================================*/
MEMORY
{
      PAGE 0:  /* Instruction memory page */
	PRU_IMEM		: org = 0x00000000 len = 0x00002000  /* 8kB PRU1 Instruction RAM */

      PAGE 1:  /* Data memory page */

	/* ===== PRU Local and Shared RAM ===== */

	PRU_DMEM_0_1	: org = 0x00000000 len = 0x00002000 CREGISTER=24 /* 8kB PRU1 Data RAM (local) */
	PRU_DMEM_1_0	: org = 0x00002000 len = 0x00002000	CREGISTER=25 /* 8kB PRU0 Data RAM (peer access) */

	  PAGE 2:  /* Shared and external memory */
	PRU_SHAREDMEM	: org = 0x00010000 len = 0x00003000 CREGISTER=28 /* 12kB Shared RAM (PRU0/PRU1/ARM) */

	/* ===== System Memory (External to PRU Subsystem) ===== */

	DDR			    : org = 0x80000000 len = 0x00010000	CREGISTER=31 /* DDR RAM (main system memory) */
	L3OCMC			: org = 0x40000000 len = 0x00010000	CREGISTER=30 /* On-Chip Memory Controller (64kB SRAM) */


	/* ===== PRU Subsystem Peripherals ===== */

	PRU_CFG			: org = 0x00026000 len = 0x00000044	CREGISTER=4  /* PRU subsystem configuration */
	PRU_ECAP		: org = 0x00030000 len = 0x00000060	CREGISTER=3  /* Enhanced Capture module */
	PRU_IEP			: org = 0x0002E000 len = 0x0000031C	CREGISTER=26 /* Industrial Ethernet Peripheral timer */
	PRU_INTC		: org = 0x00020000 len = 0x00001504	CREGISTER=0  /* PRU interrupt controller (INTC) */
	PRU_UART		: org = 0x00028000 len = 0x00000038	CREGISTER=7  /* PRU UART */

	/* ===== AM335x System Peripherals (accessible via L3 interconnect) ===== */

	DCAN0			: org = 0x481CC000 len = 0x000001E8	CREGISTER=14 /* CAN controller 0 */
	DCAN1			: org = 0x481D0000 len = 0x000001E8	CREGISTER=15 /* CAN controller 1 */
	DMTIMER2		: org = 0x48040000 len = 0x0000005C	CREGISTER=1  /* Timer 2 */
	PWMSS0			: org = 0x48300000 len = 0x000002C4	CREGISTER=18 /* PWM subsystem 0 (eCAP, eQEP, ePWM) */
	PWMSS1			: org = 0x48302000 len = 0x000002C4	CREGISTER=19 /* PWM subsystem 1 */
	PWMSS2			: org = 0x48304000 len = 0x000002C4	CREGISTER=20 /* PWM subsystem 2 */
	GEMAC			: org = 0x4A100000 len = 0x0000128C	CREGISTER=9  /* Gigabit Ethernet MAC */
	I2C1			: org = 0x4802A000 len = 0x000000D8	CREGISTER=2  /* I2C controller 1 */
	I2C2			: org = 0x4819C000 len = 0x000000D8	CREGISTER=17 /* I2C controller 2 */
	MBX0			: org = 0x480C8000 len = 0x00000140	CREGISTER=22 /* Mailbox 0 */
	MCASP0_DMA		: org = 0x46000000 len = 0x00000100	CREGISTER=8  /* McASP DMA port */
	MCSPI0			: org = 0x48030000 len = 0x000001A4	CREGISTER=6  /* SPI controller 0 */
	MCSPI1			: org = 0x481A0000 len = 0x000001A4	CREGISTER=16 /* SPI controller 1 */
	MMCHS0			: org = 0x48060000 len = 0x00000300	CREGISTER=5  /* MMC/SD controller */
	SPINLOCK		: org = 0x480CA000 len = 0x00000880	CREGISTER=23 /* Hardware spinlock */
	TPCC			: org = 0x49000000 len = 0x00001098	CREGISTER=29 /* EDMA transfer controller */
	UART1			: org = 0x48022000 len = 0x00000088	CREGISTER=11 /* UART 1 */
	UART2			: org = 0x48024000 len = 0x00000088	CREGISTER=12 /* UART 2 */

	/* ===== Reserved Constant Table Entries ===== */

	RSVD10			: org = 0x48318000 len = 0x00000100	CREGISTER=10 /* Reserved */
	RSVD13			: org = 0x48310000 len = 0x00000100	CREGISTER=13 /* Reserved */
	RSVD21			: org = 0x00032400 len = 0x00000100	CREGISTER=21 /* Reserved */
	RSVD27			: org = 0x00032000 len = 0x00000100	CREGISTER=27 /* Reserved */

}

/*==========================================================================*/
/* Section Allocation (PRU1-specific)                                      */
/*                                                                          */
/* This section maps compiler-generated sections to physical memory:       */
/* - Code sections (.text) go to instruction RAM (PAGE 0)                  */
/* - Data sections (.data, .bss, etc.) go to data RAM (PAGE 1)             */
/*                                                                          */
/* Important: PRU1 does NOT include a .pru_irq_map section because it      */
/* does not configure interrupt routing. In the BeagleLogic architecture,  */
/* PRU1 only sends interrupts (to PRU0), while PRU0 handles all interrupt  */
/* routing and mapping to ARM host interrupts.                             */
/*==========================================================================*/
SECTIONS {
	/* Forces _c_int00 (C initialization) to the start of PRU IRAM.
	   Not necessary when loading an ELF file, but useful when loading
	   a raw binary via remoteproc firmware loader */
	.text:_c_int00*	>  0x0, PAGE 0

	/* ===== Code Sections ===== */
	.text		>  PRU_IMEM, PAGE 0        /* Executable code and assembly */

	/* ===== Data Sections (all in local PRU1 data RAM) ===== */
	.stack		>  PRU_DMEM_0_1, PAGE 1    /* Runtime stack */
	.bss		>  PRU_DMEM_0_1, PAGE 1    /* Uninitialized global/static variables */
	.cio		>  PRU_DMEM_0_1, PAGE 1    /* C I/O buffer (printf, etc.) */
	.data		>  PRU_DMEM_0_1, PAGE 1    /* Initialized global/static variables */
	.switch		>  PRU_DMEM_0_1, PAGE 1    /* Switch statement jump tables */
	.sysmem		>  PRU_DMEM_0_1, PAGE 1    /* Dynamic memory heap (malloc) */
	.cinit		>  PRU_DMEM_0_1, PAGE 1    /* Initialization tables for global data */
	.rodata		>  PRU_DMEM_0_1, PAGE 1    /* Read-only data (const variables) */
	.rofardata	>  PRU_DMEM_0_1, PAGE 1    /* Read-only far data */
	.farbss		>  PRU_DMEM_0_1, PAGE 1    /* Far BSS (uninitialized far data) */
	.fardata	>  PRU_DMEM_0_1, PAGE 1    /* Far data (initialized far data) */

	/* No .pru_irq_map section for PRU1 - it doesn't configure interrupts.
	   Only PRU0 needs .pru_irq_map to define interrupt routing tables. */
}
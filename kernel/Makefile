################################################################################
# Makefile for BeagleLogic Kernel Module and Device Tree Overlay
#
# This makefile builds:
# 1. beaglelogic.ko - Kernel module for the logic analyzer driver
# 2. beaglelogic-00A0.dtbo - Device tree overlay for hardware configuration
#
# Updated for Linux kernel 6.x+ with modern device tree compilation.
#
# Usage:
#   make              - Build kernel module
#   make overlay      - Build device tree overlay
#   make deploy_overlay - Install overlay to /lib/firmware
#   make clean        - Remove build artifacts
# 
# Original work:
# Copyright (C) 2014-20 Kumar Abhishek <abhishek@theembeddedkitchen.net>
# 
# Kernel 6.x+ port and updates:
# Copyright (C) 2024-2026 Bryan Rainwater
################################################################################

# Kernel source directory (defaults to current running kernel)
KSRC ?= /lib/modules/$(shell uname -r)/build

# Device tree binding includes (required for #include directives in overlay)
# Auto-detect from kernel source, or override with: make overlay DTBINDSRC=/path/to/includes
DTBINDSRC ?= $(shell [ -d "$(KSRC)/include" ] && echo "$(KSRC)/include" || echo "/usr/src/linux-headers-$(shell uname -r)/include")

# Kernel module object file
obj-m := beaglelogic.o

all:
	# Cross-compile option (uncomment for cross-compilation):
	# @make -C $(KSRC) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
	# Native compilation (for building on BeagleBone itself):
	@make -C $(KSRC) M=$(PWD) ARCH=arm modules

clean:
	# Cross-compile clean option (uncomment for cross-compilation):
	# @make -C $(KSRC) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- clean
	# Native clean (for building on BeagleBone itself):
	@make -C $(KSRC) M=$(PWD) ARCH=arm clean

################################################################################
# Device Tree Overlay Compilation
#
# The overlay is compiled in two steps for kernel 6.x+:
# 1. Preprocess with cpp to handle #include directives
# 2. Compile to binary blob with dtc
#
# Note: DTBINDSRC is auto-detected from kernel source. Override if needed:
#   make overlay DTBINDSRC=/path/to/kernel/include
################################################################################

overlay: beaglelogic-00A0.dtbo

beaglelogic-00A0.dtbo: beaglelogic-00A0.dts
	# Kernel 4.9 method (deprecated):
	# dtc -O dtb -o beaglelogic-00A0.dtbo -b 0 -@ beaglelogic-00A0.dts
	#
	# Kernel 6.x+ basic method (no preprocessor):
	# dtc -@ -I dts -O dtb -o beaglelogic-00A0.dtbo beaglelogic-00A0.dts
	#
	# Kernel 6.x+ preferred method (with C preprocessor for #include support):
	# Step 1: Preprocess to expand includes and macros
	cpp -nostdinc -I $(DTBINDSRC) -I arch  -undef -x assembler-with-cpp beaglelogic-00A0.dts > beaglelogic-00A0.dtso
	# Step 2: Compile preprocessed source to binary overlay
	dtc -@ -O dtb -o beaglelogic-00A0.dtbo beaglelogic-00A0.dtso

# Install device tree overlay to system firmware directory
deploy_overlay:
	cp -v beaglelogic-00A0.dtbo /lib/firmware

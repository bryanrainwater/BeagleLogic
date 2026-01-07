#!/bin/bash
#
# BeagleLogic Startup Script
# Waits for BeagleLogic device to appear and configures default settings
#

log='beaglelogic-startup:'

# Wait for BeagleLogic device to appear (with 30 second timeout)
if [ ! -d /sys/devices/virtual/misc/beaglelogic ] ; then
	echo "${log} Waiting for BeagleLogic to show up (timeout in 30 seconds)"
	wait_seconds=30
	until test $((wait_seconds--)) -eq 0 -o -d "/sys/devices/virtual/misc/beaglelogic" ; do sleep 1; done

	if [ ! -d /sys/devices/virtual/misc/beaglelogic ] ; then
		echo "${log} timeout. BeagleLogic couldn't load."
		echo "${log} Debug: checking if module loaded..."
		lsmod | grep beaglelogic
		echo "${log} Debug: checking dmesg for errors..."
		dmesg | tail -20
		exit 1
	fi
fi

# Configure default BeagleLogic settings
echo 1 > /sys/devices/virtual/misc/beaglelogic/triggerflags  # Continuous mode
echo 0 > /sys/devices/virtual/misc/beaglelogic/sampleunit    # 16-bit samples
echo 8388608 > /sys/devices/virtual/misc/beaglelogic/memalloc # 8MB buffer (tests will reallocate as needed)

echo "${log} Loaded"

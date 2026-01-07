################################################################################
# Makefile for BeagleLogic Test Applications
#
# This makefile builds example programs demonstrating the BeagleLogic API.
#
# Usage:
#   make              - Build all examples
#   make clean        - Remove build artifacts
#   make install      - Install examples to /usr/local/bin
#
# Copyright (C) 2014-2020 Kumar Abhishek <abhishek@theembeddedkitchen.net>
# Copyright (C) 2024-2026 Bryan Rainwater
################################################################################

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

# Primary unified test application (recommended for all users)
EXAMPLES = beaglelogic-testapp

# Default target
all: $(EXAMPLES)

# Pattern rules for building object files
%.o: %.c libbeaglelogic.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Link rule for beaglelogic-testapp (unified test application)
beaglelogic-testapp: beaglelogic-testapp.o beaglelogic.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Clean build artifacts
clean:
	rm -f *.o $(EXAMPLES)

# Install binaries to /usr/local/bin
install: all
	install -d /usr/local/bin
	install -m 755 $(EXAMPLES) /usr/local/bin/

# Uninstall binaries
uninstall:
	rm -f $(addprefix /usr/local/bin/,$(EXAMPLES))

# Help target
help:
	@echo "BeagleLogic Test Application Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all (default) - Build the unified test application"
	@echo "  clean         - Remove all build artifacts"
	@echo "  install       - Install beaglelogic-testapp to /usr/local/bin"
	@echo "  uninstall     - Remove installed application"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Application:"
	@echo "  beaglelogic-testapp - UNIFIED INTERACTIVE TEST APP (all-in-one tool)"
	@echo "                        Includes ALL functionality in a single application:"
	@echo "            • Simple capture"
	@echo "            • Continuous logging"
	@echo "            • PRUDAQ ADC capture"
	@echo "            • Performance benchmarking"
	@echo "            • ALL 13 diagnostic tests integrated"

.PHONY: all clean install uninstall help

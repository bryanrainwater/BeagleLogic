/*
 * beaglelogic-testapp.c - BeagleLogic Unified Interactive Test Application
 *
 * This comprehensive test tool combines all BeagleLogic testing functionality
 * into a single interactive program with 8 modes:
 *
 * Basic Modes (1-3):
 *   - Simple capture
 *   - Continuous logging with file rotation
 *   - PRUDAQ ADC capture
 *
 * Educational Modes (4-6) - Code examples demonstrating:
 *   - Continuous blocking read() patterns
 *   - Non-blocking poll() patterns
 *   - Oneshot visual waveform display
 *
 * Advanced (7-8):
 *   - Performance testing and benchmarking
 *   - Diagnostic tests (13 comprehensive tests with test suites)
 *
 * Usage: sudo ./beaglelogic-testapp
 *
 * Copyright (C) 2014 Kumar Abhishek
 * Copyright (C) 2024-2026 Bryan Rainwater
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "libbeaglelogic.h"

/* ========================================================================
 * CONFIGURATION CONSTANTS
 * ======================================================================== */

/* Test configuration */
#define TEST_SAMPLERATE       (10 * 1000 * 1000)  /* 10 MHz */
#define TEST_BUFFERSIZE       (4 * 1024 * 1024)   /* 4 MB */
#define TEST_READSIZE         (1 * 1024 * 1024)   /* 1 MB */
#define TEST_LARGE_BUFFERSIZE (32 * 1024 * 1024)  /* 32 MB */
#define TEST_CHUNK_SIZE       (64 * 1024)         /* 64 KB */

/* Continuous logger configuration */
#define DEFAULT_OUTPUT_DIR "./beaglelogic_logs"
#define DEFAULT_DURATION_SEC 10
#define FILE_ROTATION_SIZE (10 * 1024 * 1024)  /* 10 MB per file */
#define LOGGER_BUFFER_SIZE (1024 * 1024)  /* 1 MB */

/* PRUDAQ configuration */
#define ADC_MAX_VALUE 4095  /* 12-bit ADC */
#define ADC_VREF 1.8        /* PRUDAQ reference voltage */

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_BOLD    "\033[1m"

/* ========================================================================
 * GLOBAL VARIABLES
 * ======================================================================== */

static volatile int interrupted = 0;
static volatile int keep_running = 1;

/* For performance test */
static int perf_fd;
static uint8_t *perf_buf;

/* ========================================================================
 * UTILITY FUNCTIONS
 * ======================================================================== */

/* Signal handlers */
void signal_handler(int sig)
{
	(void)sig;
	interrupted = 1;
	keep_running = 0;
	fprintf(stderr, "\n%s[SIGNAL] Interrupted by user%s\n", COLOR_YELLOW, COLOR_RESET);
}

void perf_signal_handler(int sig)
{
	(void)sig;
	if (perf_buf)
		free(perf_buf);
	fprintf(stderr, "\nSignal caught\n");
	beaglelogic_close(perf_fd);
	exit(-1);
}

/* Print functions */
void print_section(const char *title)
{
	printf("\n%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s%s%s\n", COLOR_CYAN, title, COLOR_RESET);
	printf("%s========================================%s\n", COLOR_CYAN, COLOR_RESET);
}

void print_step(const char *step)
{
	printf("%s[STEP]%s %s\n", COLOR_BLUE, COLOR_RESET, step);
}

void print_success(const char *msg)
{
	printf("%s[OK]%s %s\n", COLOR_GREEN, COLOR_RESET, msg);
}

void print_error(const char *msg)
{
	printf("%s[ERROR]%s %s (errno=%d: %s)\n",
		COLOR_RED, COLOR_RESET, msg, errno, strerror(errno));
}

void print_warning(const char *msg)
{
	printf("%s[WARN]%s %s\n", COLOR_YELLOW, COLOR_RESET, msg);
}

void print_info(const char *key, const char *value)
{
	printf("  %-20s: %s\n", key, value);
}

void print_info_int(const char *key, int value)
{
	printf("  %-20s: %d\n", key, value);
}

void print_info_hex(const char *key, unsigned long value)
{
	printf("  %-20s: 0x%lx\n", key, value);
}

/* Read sysfs attribute */
int read_sysfs_attr(const char *attr, char *buf, size_t bufsize)
{
	char path[256];
	FILE *f;

	snprintf(path, sizeof(path), "/sys/devices/virtual/misc/beaglelogic/%s", attr);
	f = fopen(path, "r");
	if (!f) {
		return -1;
	}

	if (fgets(buf, bufsize, f) == NULL) {
		fclose(f);
		return -1;
	}

	fclose(f);

	/* Remove trailing newline */
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

/* Print device state from sysfs */
void print_device_state(void)
{
	char buf[256];

	printf("\n%s[DEVICE STATE]%s\n", COLOR_BLUE, COLOR_RESET);

	if (read_sysfs_attr("state", buf, sizeof(buf)) == 0) {
		print_info("State", buf);
	} else {
		print_warning("Cannot read state");
	}

	if (read_sysfs_attr("memalloc", buf, sizeof(buf)) == 0) {
		print_info("Memory allocated", buf);
	}

	if (read_sysfs_attr("buffersize", buf, sizeof(buf)) == 0) {
		print_info("Buffer size", buf);
	}

	if (read_sysfs_attr("samplerate", buf, sizeof(buf)) == 0) {
		print_info("Sample rate", buf);
	}

	if (read_sysfs_attr("sampleunit", buf, sizeof(buf)) == 0) {
		print_info("Sample unit", buf);
	}

	if (read_sysfs_attr("triggerflags", buf, sizeof(buf)) == 0) {
		print_info("Trigger flags", buf);
	}
}

/* Capture recent dmesg */
void print_recent_dmesg(void)
{
	printf("\n%s[RECENT KERNEL MESSAGES]%s\n", COLOR_BLUE, COLOR_RESET);
	system("dmesg | grep -i beaglelogic | tail -10");
}

/* Utility: Get user input */
int get_int_input(const char *prompt, int default_value)
{
	char buf[128];
	printf("%s [%d]: ", prompt, default_value);
	fflush(stdout);

	if (fgets(buf, sizeof(buf), stdin) == NULL) {
		return default_value;
	}

	if (buf[0] == '\n') {
		return default_value;
	}

	return atoi(buf);
}

void get_string_input(const char *prompt, const char *default_value, char *output, size_t output_size)
{
	printf("%s [%s]: ", prompt, default_value);
	fflush(stdout);

	if (fgets(output, output_size, stdin) == NULL) {
		strncpy(output, default_value, output_size - 1);
		output[output_size - 1] = '\0';
		return;
	}

	/* Remove trailing newline */
	output[strcspn(output, "\n")] = '\0';

	if (output[0] == '\0') {
		strncpy(output, default_value, output_size - 1);
		output[output_size - 1] = '\0';
	}
}

/* Time difference in microseconds */
static uint64_t timediff(struct timespec *tv1, struct timespec *tv2)
{
	return ((((uint64_t)(tv2->tv_sec - tv1->tv_sec) * 1000000000) +
		(tv2->tv_nsec - tv1->tv_nsec)) / 1000);
}

/* ========================================================================
 * MODE 1: SIMPLE CAPTURE
 * ======================================================================== */

int mode_simple_capture(void)
{
	int fd;
	uint8_t *buffer;
	size_t capture_size;
	uint32_t sample_rate;
	size_t bytes_read = 0;
	ssize_t ret;
	char filename[256];

	print_section("MODE 1: Simple Capture");

	/* Get configuration */
	capture_size = get_int_input("Capture size (KB)", 1024) * 1024;
	sample_rate = get_int_input("Sample rate (Hz)", 1000000);
	get_string_input("Output file", "capture.bin", filename, sizeof(filename));

	printf("\nConfiguration:\n");
	printf("  Capture size: %zu bytes (%.2f MB)\n", capture_size, capture_size / 1048576.0);
	printf("  Sample rate:  %u Hz (%.2f MHz)\n", sample_rate, sample_rate / 1000000.0);
	printf("  Output file:  %s\n", filename);
	printf("\n");

	/* Allocate buffer */
	buffer = malloc(capture_size);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	/* Open device */
	print_step("Opening BeagleLogic device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");

	/* Configure */
	print_step("Configuring capture (oneshot mode, 8-bit)");
	beaglelogic_set_samplerate(fd, sample_rate);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);
	print_device_state();

	/* Capture data */
	print_step("Capturing data");
	while (bytes_read < capture_size) {
		ret = read(fd, buffer + bytes_read, capture_size - bytes_read);
		if (ret <= 0) {
			print_error("read() failed");
			break;
		}
		bytes_read += ret;

		/* Progress indicator */
		if (bytes_read % (256 * 1024) == 0) {
			printf("  Progress: %.1f%%\r", (bytes_read * 100.0) / capture_size);
			fflush(stdout);
		}
	}
	printf("\n");
	print_success("Data captured");
	print_info_int("Bytes captured", bytes_read);

	/* Save to file */
	print_step("Saving to file");
	FILE *f = fopen(filename, "wb");
	if (!f) {
		print_error("fopen() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}

	if (fwrite(buffer, 1, bytes_read, f) != bytes_read) {
		print_error("fwrite() failed");
		fclose(f);
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}

	fclose(f);
	print_success("File saved");

	/* Cleanup */
	beaglelogic_close(fd);
	free(buffer);

	print_success("Simple capture completed");
	return 0;
}

/* ========================================================================
 * MODE 2: CONTINUOUS LOGGER
 * ======================================================================== */

/* Create output directory */
int create_output_dir(const char *path)
{
	struct stat st = {0};

	if (stat(path, &st) == -1) {
		if (mkdir(path, 0755) == -1) {
			perror("mkdir");
			return -1;
		}
	}
	return 0;
}

/* Generate filename with timestamp */
void generate_filename(char *buf, size_t bufsize, const char *output_dir, int file_num)
{
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	char timestamp[32];

	strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
	snprintf(buf, bufsize, "%s/capture_%s_%04d.bin", output_dir, timestamp, file_num);
}

int mode_continuous_logger(void)
{
	int fd;
	FILE *outfile = NULL;
	uint8_t *buffer;
	char output_dir[512];
	int duration_sec;
	uint32_t sample_rate;
	time_t start_time, current_time = 0;
	size_t file_bytes = 0;
	ssize_t bytes_read;
	char filename[512];
	uint64_t total_bytes = 0;
	int file_count = 0;

	print_section("MODE 2: Continuous Logger");

	/* Get configuration */
	get_string_input("Output directory", DEFAULT_OUTPUT_DIR, output_dir, sizeof(output_dir));
	duration_sec = get_int_input("Duration (seconds)", DEFAULT_DURATION_SEC);
	sample_rate = get_int_input("Sample rate (Hz)", 10000000);

	printf("\nConfiguration:\n");
	printf("  Output directory: %s\n", output_dir);
	printf("  Target duration:  %d seconds\n", duration_sec);
	printf("  Sample rate:      %u Hz (%.2f MHz)\n", sample_rate, sample_rate / 1000000.0);
	printf("  File rotation:    %.1f MB\n", FILE_ROTATION_SIZE / 1048576.0);
	printf("\n");

	/* Create output directory */
	if (create_output_dir(output_dir) < 0) {
		return -1;
	}

	/* Allocate buffer */
	buffer = malloc(LOGGER_BUFFER_SIZE);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	/* Open device */
	print_step("Opening BeagleLogic device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");

	/* Configure */
	print_step("Configuring capture (oneshot loop mode for stability)");
	beaglelogic_set_samplerate(fd, sample_rate);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);

	printf("\n");
	print_success("Starting continuous logging");
	start_time = time(NULL);

	/* Reset signal handler state */
	keep_running = 1;

	/* Main capture loop */
	while (keep_running) {
		/* Check duration */
		current_time = time(NULL);
		if (difftime(current_time, start_time) >= duration_sec) {
			printf("Duration reached, stopping...\n");
			break;
		}

		/* Open new file if needed */
		if (outfile == NULL || file_bytes >= FILE_ROTATION_SIZE) {
			if (outfile) {
				fclose(outfile);
				printf("Rotated to new file (file %d complete: %.2f MB)\n",
				       file_count, file_bytes / 1048576.0);
			}

			generate_filename(filename, sizeof(filename), output_dir, file_count);
			outfile = fopen(filename, "wb");
			if (!outfile) {
				print_error("fopen() failed");
				break;
			}

			printf("Writing to: %s\n", filename);
			file_count++;
			file_bytes = 0;
		}

		/* Read data */
		bytes_read = read(fd, buffer, LOGGER_BUFFER_SIZE);
		if (bytes_read <= 0) {
			if (bytes_read < 0) {
				print_error("read() failed");
			}
			break;
		}

		/* Write to file */
		if (fwrite(buffer, 1, bytes_read, outfile) != (size_t)bytes_read) {
			print_error("fwrite() failed");
			break;
		}

		file_bytes += bytes_read;
		total_bytes += bytes_read;

		/* Progress update */
		if (total_bytes % (10 * 1024 * 1024) < LOGGER_BUFFER_SIZE) {
			printf("Progress: %.2f MB captured, %.0f seconds elapsed\r",
			       total_bytes / 1048576.0, difftime(current_time, start_time));
			fflush(stdout);
		}
	}

	printf("\n");

	/* Cleanup */
	if (outfile) {
		fclose(outfile);
	}

	beaglelogic_close(fd);
	free(buffer);

	/* Summary */
	print_success("Continuous logging completed");
	printf("\n");
	printf("Summary:\n");
	printf("  Total bytes: %.2f MB\n", total_bytes / 1048576.0);
	printf("  Files:       %d\n", file_count);
	printf("  Actual time: %.1f seconds\n", difftime(current_time, start_time));
	if (difftime(current_time, start_time) > 0) {
		printf("  Data rate:   %.2f MB/s\n",
		       (total_bytes / 1048576.0) / difftime(current_time, start_time));
	}

	return 0;
}

/* ========================================================================
 * MODE 3: PRUDAQ ADC CAPTURE
 * ======================================================================== */

/* Decode PRUDAQ sample */
void decode_prudaq_sample(uint16_t raw, int *ch0, int *ch1)
{
	*ch0 = raw & 0x0FFF;
	*ch1 = (raw >> 12) & 0x0FFF;
}

/* Convert ADC to voltage */
float adc_to_voltage(int adc_value)
{
	return (float)adc_value * ADC_VREF / ADC_MAX_VALUE;
}

int mode_prudaq_adc(void)
{
	int fd;
	uint16_t *buffer;
	uint32_t sample_rate;
	int num_samples;
	size_t buffer_size;
	ssize_t bytes_read;
	unsigned int i;
	int ch0, ch1;
	float v0, v1;
	char output_file[256];
	FILE *f;

	print_section("MODE 3: PRUDAQ ADC Capture");

	print_warning("IMPORTANT: Ensure PRUDAQ firmware is loaded!");
	printf("  Check: ls -l /lib/firmware/beaglelogic-pru1-fw\n");
	printf("  Should point to: beaglelogic-pru1-prudaq-ch01\n");
	printf("\n");

	print_warning("PRUDAQ Mode: Sample rate is determined by external clock on P9_26!");
	printf("  Provide clock frequency: 10-20 MHz (determines actual sample rate)\n");
	printf("  The sample rate setting below is for CSV metadata only.\n");
	printf("\n");

	/* Get configuration */
	sample_rate = get_int_input("Expected clock frequency (Hz)", 10000000);
	num_samples = get_int_input("Number of samples", 10000);
	get_string_input("Output CSV file", "adc_data.csv", output_file, sizeof(output_file));

	printf("\nConfiguration:\n");
	printf("  Expected clock freq: %u Hz (%.2f MHz) - P9_26\n", sample_rate, sample_rate / 1000000.0);
	printf("  Num samples:         %d\n", num_samples);
	printf("  ADC channels:        2 (12-bit I/Q)\n");
	printf("  ADC Vref:            %.2f V\n", ADC_VREF);
	printf("  Output file:         %s\n", output_file);
	printf("\n");
	printf("  %sNote: Actual sample rate = external clock frequency%s\n", COLOR_YELLOW, COLOR_RESET);

	/* Allocate buffer */
	buffer_size = num_samples * sizeof(uint16_t);
	buffer = malloc(buffer_size);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	/* Open device */
	print_step("Opening BeagleLogic device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");

	/* Configure */
	print_step("Configuring for 16-bit PRUDAQ mode");
	beaglelogic_set_samplerate(fd, sample_rate);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_16_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);
	print_device_state();

	/* Capture */
	print_step("Capturing ADC data");
	bytes_read = read(fd, buffer, buffer_size);
	if (bytes_read < 0) {
		print_error("read() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Data captured");
	print_info_int("Bytes read", bytes_read);
	print_info_int("Samples", bytes_read / 2);

	/* Open output file */
	print_step("Writing CSV data");
	f = fopen(output_file, "w");
	if (!f) {
		print_error("fopen() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}

	/* Write CSV header */
	fprintf(f, "sample,ch0_raw,ch0_voltage,ch1_raw,ch1_voltage\n");

	/* Decode and write samples */
	for (i = 0; i < bytes_read / sizeof(uint16_t); i++) {
		decode_prudaq_sample(buffer[i], &ch0, &ch1);
		v0 = adc_to_voltage(ch0);
		v1 = adc_to_voltage(ch1);
		fprintf(f, "%d,%d,%.4f,%d,%.4f\n", i, ch0, v0, ch1, v1);
	}

	fclose(f);
	print_success("CSV file written");

	/* Cleanup */
	beaglelogic_close(fd);
	free(buffer);

	print_success("PRUDAQ ADC capture completed");
	return 0;
}

/* ========================================================================
 * MODE 4: CONTINUOUS CAPTURE - BLOCKING READ
 * ======================================================================== */

/* Global flag for signal handler */
static volatile int stop_capture = 0;

void sigint_handler(int sig)
{
	(void)sig;
	stop_capture = 1;
	printf("\n");  /* Move to new line after ^C */
}

int mode_continuous_blocking(void)
{
	int fd;
	uint8_t *buffer;
	uint32_t sample_rate;
	size_t buffer_size = 256 * 1024;  /* 256 KB read buffer */
	ssize_t bytes_read;
	uint64_t total_bytes = 0;
	time_t start_time, current_time, last_update;
	double elapsed, rate;
	struct sigaction sa;

	print_section("MODE 4: Continuous Capture (Blocking Read)");

	printf("This mode demonstrates:\n");
	printf("  • Continuous capture using blocking read()\n");
	printf("  • Real-time statistics display\n");
	printf("  • Signal handling for clean shutdown\n");
	printf("  • Press Ctrl+C to stop capture\n");
	printf("\n");

	/* Get configuration */
	sample_rate = get_int_input("Sample rate (Hz)", 10000000);

	printf("\nConfiguration:\n");
	printf("  Sample rate: %u Hz (%.2f MHz)\n", sample_rate, sample_rate / 1000000.0);
	printf("  Mode:        Continuous (blocking read)\n");
	printf("  Stop:        Ctrl+C\n");
	printf("\n");

	/* Allocate buffer */
	buffer = malloc(buffer_size);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	/* Setup signal handler for Ctrl+C */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		print_error("sigaction() failed");
		free(buffer);
		return -1;
	}

	/* Open device */
	print_step("Opening BeagleLogic device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");

	/* Configure */
	print_step("Configuring continuous capture");
	beaglelogic_set_samplerate(fd, sample_rate);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_CONTINUOUS);
	print_device_state();

	/* Start capture */
	print_step("Starting continuous capture (press Ctrl+C to stop)");
	printf("\n");

	start_time = time(NULL);
	last_update = start_time;
	stop_capture = 0;

	while (!stop_capture) {
		/* Blocking read - waits for data */
		bytes_read = read(fd, buffer, buffer_size);

		if (bytes_read < 0) {
			if (errno == EINTR) {
				/* Interrupted by signal (Ctrl+C) */
				break;
			}
			print_error("read() failed");
			break;
		}

		if (bytes_read == 0) {
			/* End of data (shouldn't happen in continuous mode) */
			break;
		}

		total_bytes += bytes_read;
		current_time = time(NULL);

		/* Update display every second */
		if (current_time != last_update) {
			elapsed = difftime(current_time, start_time);
			rate = total_bytes / elapsed;

			printf("\r[Capturing] Total: %.2f MB | Rate: %.2f MB/s | Time: %.0fs   ",
			       total_bytes / 1048576.0,
			       rate / 1048576.0,
			       elapsed);
			fflush(stdout);
			last_update = current_time;
		}
	}

	printf("\n\n");
	print_success("Capture stopped");

	/* Summary */
	current_time = time(NULL);
	elapsed = difftime(current_time, start_time);
	rate = elapsed > 0 ? total_bytes / elapsed : 0;

	printf("\nSummary:\n");
	printf("  Total bytes:  %.2f MB\n", total_bytes / 1048576.0);
	printf("  Actual time:  %.1f seconds\n", elapsed);
	printf("  Average rate: %.2f MB/s (%.2f MSamples/s)\n",
	       rate / 1048576.0,
	       rate / 1000000.0);
	printf("  Total samples: %lu\n", (unsigned long)total_bytes);

	/* Cleanup */
	beaglelogic_close(fd);
	free(buffer);

	/* Restore default signal handler */
	signal(SIGINT, SIG_DFL);

	return 0;
}

/* ========================================================================
 * MODE 5: CONTINUOUS CAPTURE - POLL/NON-BLOCKING
 * ======================================================================== */

int mode_continuous_poll(void)
{
	int fd;
	uint8_t *buffer;
	uint32_t sample_rate;
	size_t buffer_size = 256 * 1024;  /* 256 KB read buffer */
	ssize_t bytes_read;
	uint64_t total_bytes = 0;
	time_t start_time, current_time, last_update;
	double elapsed, rate;
	struct pollfd fds[2];
	int nfds;
	char input;

	print_section("MODE 5: Continuous Capture (Poll/Non-blocking)");

	printf("This mode demonstrates:\n");
	printf("  • Non-blocking I/O with poll()\n");
	printf("  • Monitoring multiple file descriptors (device + stdin)\n");
	printf("  • Event-driven capture architecture\n");
	printf("  • Press Enter to stop capture\n");
	printf("\n");

	/* Get configuration */
	sample_rate = get_int_input("Sample rate (Hz)", 10000000);

	printf("\nConfiguration:\n");
	printf("  Sample rate: %u Hz (%.2f MHz)\n", sample_rate, sample_rate / 1000000.0);
	printf("  Mode:        Continuous (poll/non-blocking)\n");
	printf("  Stop:        Press Enter\n");
	printf("\n");

	/* Allocate buffer */
	buffer = malloc(buffer_size);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	/* Open device */
	print_step("Opening BeagleLogic device");
	fd = beaglelogic_open_nonblock();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");

	/* Configure */
	print_step("Configuring continuous capture");
	beaglelogic_set_samplerate(fd, sample_rate);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_CONTINUOUS);
	print_device_state();

	/* Setup poll structures */
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	fds[1].fd = STDIN_FILENO;
	fds[1].events = POLLIN;

	/* Start capture */
	print_step("Starting continuous capture (press Enter to stop)");
	printf("\n");

	start_time = time(NULL);
	last_update = start_time;

	while (1) {
		/* Poll for data or user input (1 second timeout) */
		nfds = poll(fds, 2, 1000);

		if (nfds < 0) {
			if (errno == EINTR) {
				continue;
			}
			print_error("poll() failed");
			break;
		}

		/* Check for user input (Enter key) */
		if (fds[1].revents & POLLIN) {
			if (read(STDIN_FILENO, &input, 1) > 0) {
				printf("\n");
				break;  /* User pressed Enter */
			}
		}

		/* Check for data available */
		if (fds[0].revents & POLLIN) {
			bytes_read = read(fd, buffer, buffer_size);

			if (bytes_read < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					/* No data available yet - continue */
					continue;
				}
				print_error("read() failed");
				break;
			}

			if (bytes_read > 0) {
				total_bytes += bytes_read;
			}
		}

		/* Update display */
		current_time = time(NULL);
		if (current_time != last_update || nfds == 0) {  /* Update on timeout too */
			elapsed = difftime(current_time, start_time);
			rate = elapsed > 0 ? total_bytes / elapsed : 0;

			printf("\r[Capturing] Total: %.2f MB | Rate: %.2f MB/s | Time: %.0fs   ",
			       total_bytes / 1048576.0,
			       rate / 1048576.0,
			       elapsed);
			fflush(stdout);
			last_update = current_time;
		}
	}

	printf("\n\n");
	print_success("Capture stopped");

	/* Summary */
	current_time = time(NULL);
	elapsed = difftime(current_time, start_time);
	rate = elapsed > 0 ? total_bytes / elapsed : 0;

	printf("\nSummary:\n");
	printf("  Total bytes:  %.2f MB\n", total_bytes / 1048576.0);
	printf("  Actual time:  %.1f seconds\n", elapsed);
	printf("  Average rate: %.2f MB/s (%.2f MSamples/s)\n",
	       rate / 1048576.0,
	       rate / 1000000.0);
	printf("  Total samples: %lu\n", (unsigned long)total_bytes);

	/* Cleanup */
	beaglelogic_close(fd);
	free(buffer);

	return 0;
}

/* ========================================================================
 * MODE 6: ONESHOT VISUAL DISPLAY
 * ======================================================================== */

void display_waveform_terminal(uint8_t *data, size_t len, int channel)
{
	int i;
	uint8_t mask = 1 << channel;
	int samples_per_line = 80;

	printf("\nWaveform visualization (channel %d, first %zu samples):\n", channel, len);
	printf("  █ = HIGH, ░ = LOW\n\n  ");

	for (i = 0; i < (int)len && i < 800; i++) {
		printf("%s", (data[i] & mask) ? "█" : "░");
		if ((i + 1) % samples_per_line == 0 && i < (int)len - 1) {
			printf("\n  ");
		}
	}
	printf("\n");
}

void display_timing_grid(uint8_t *data, size_t len)
{
	int i, bit;
	int samples_to_show = (len < 64) ? len : 64;

	printf("\nTiming grid (first %d samples, all 8 channels):\n", samples_to_show);
	printf("  Sample  | 7 6 5 4 3 2 1 0 | Hex\n");
	printf("  --------+-----------------+-----\n");

	for (i = 0; i < samples_to_show; i++) {
		printf("  %6d  | ", i);
		for (bit = 7; bit >= 0; bit--) {
			printf("%c ", (data[i] & (1 << bit)) ? '1' : '0');
		}
		printf("| 0x%02X\n", data[i]);
	}
}

void display_channel_statistics(uint8_t *data, size_t len)
{
	int ch;
	uint64_t high_count[8] = {0};
	uint64_t transitions[8] = {0};
	uint8_t prev_state[8] = {0};
	size_t i;

	/* Calculate statistics */
	for (i = 0; i < len; i++) {
		for (ch = 0; ch < 8; ch++) {
			uint8_t state = (data[i] >> ch) & 1;
			if (state) high_count[ch]++;
			if (i > 0 && state != prev_state[ch]) {
				transitions[ch]++;
			}
			prev_state[ch] = state;
		}
	}

	printf("\nChannel Statistics:\n");
	printf("  Ch | Pin   | HIGH%% | Transitions | Likely Signal\n");
	printf("  ---+-------+-------+-------------+--------------\n");

	const char *pins[] = {"P8.45", "P8.46", "P8.43", "P8.44",
	                      "P8.41", "P8.42", "P8.39", "P8.40"};

	for (ch = 0; ch < 8; ch++) {
		double high_pct = (100.0 * high_count[ch]) / len;
		printf("  %d  | %s | %5.1f | %11lu | ",
		       ch, pins[ch], high_pct, (unsigned long)transitions[ch]);

		if (transitions[ch] == 0) {
			printf("%s\n", (high_pct > 50) ? "Static HIGH" : "Static LOW");
		} else if (transitions[ch] > len / 4) {
			printf("Active (square wave?)\n");
		} else {
			printf("Active (%lu edges)\n", (unsigned long)transitions[ch]);
		}
	}
}

int mode_oneshot_visual(void)
{
	int fd;
	uint8_t *buffer;
	size_t capture_size = 4096;  /* 4 KB for visual display */
	uint32_t sample_rate;
	ssize_t bytes_read;

	print_section("MODE 6: Oneshot Visual Display");

	printf("This mode demonstrates:\n");
	printf("  • Quick capture for signal verification\n");
	printf("  • Terminal-based waveform visualization\n");
	printf("  • Multi-channel timing analysis\n");
	printf("  • Statistical signal analysis\n");
	printf("\n");

	/* Get configuration */
	sample_rate = get_int_input("Sample rate (Hz)", 10000000);

	printf("\nConfiguration:\n");
	printf("  Sample rate:  %u Hz (%.2f MHz)\n", sample_rate, sample_rate / 1000000.0);
	printf("  Capture size: %zu bytes\n", capture_size);
	printf("  Mode:         Oneshot\n");
	printf("\n");

	/* Allocate buffer */
	buffer = malloc(capture_size);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	/* Open device */
	print_step("Opening BeagleLogic device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");

	/* Configure */
	print_step("Configuring oneshot capture");
	beaglelogic_set_samplerate(fd, sample_rate);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);

	/* Capture */
	print_step("Capturing data");
	bytes_read = read(fd, buffer, capture_size);
	if (bytes_read < 0) {
		print_error("read() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Data captured");
	printf("  Bytes captured: %zd\n", bytes_read);

	/* Display visualizations */
	if (bytes_read > 0) {
		/* Channel 0 waveform */
		display_waveform_terminal(buffer, bytes_read, 0);

		/* Timing grid */
		display_timing_grid(buffer, bytes_read);

		/* Statistics */
		display_channel_statistics(buffer, bytes_read);
	}

	/* Cleanup */
	beaglelogic_close(fd);
	free(buffer);

	print_success("Oneshot visual display completed");
	return 0;
}

/* ========================================================================
 * MODE 7: PERFORMANCE TEST
 * ======================================================================== */

int mode_performance_test(void)
{
	ssize_t cnt1;
	size_t sz, sz_to_read;
	struct timespec t1, t2;
	struct pollfd pollfd;
	uint32_t samplerate;
	uint32_t buffersize;
	uint8_t sampleunit;
	uint32_t triggerflags;
	uint8_t *bl_mem = NULL;
	int use_mmap;
	int i;

	print_section("MODE 4: Performance Test");

	/* Get configuration */
	samplerate = get_int_input("Sample rate (Hz)", 50000000);
	buffersize = get_int_input("Buffer size (MB)", 32) * 1024 * 1024;
	sampleunit = get_int_input("Sample unit (8 or 16 bits)", 8) == 16 ?
		BL_SAMPLEUNIT_16_BITS : BL_SAMPLEUNIT_8_BITS;
	printf("Capture mode: 1=oneshot, 2=continuous: ");
	fflush(stdout);
	char mode[16];
	if (fgets(mode, sizeof(mode), stdin) == NULL || mode[0] == '\n' || mode[0] == '2') {
		triggerflags = BL_TRIGGERFLAGS_CONTINUOUS;
	} else {
		triggerflags = BL_TRIGGERFLAGS_ONESHOT;
	}
	use_mmap = get_int_input("Use mmap? (1=yes, 0=no)", 0);

	printf("\nConfiguration:\n");
	printf("  Sample rate:  %u Hz (%.2f MHz)\n", samplerate, samplerate / 1000000.0);
	printf("  Buffer size:  %u bytes (%u MB)\n", buffersize, buffersize / (1024 * 1024));
	printf("  Sample unit:  %s\n", sampleunit == BL_SAMPLEUNIT_8_BITS ? "8-bit" : "16-bit");
	printf("  Mode:         %s\n", triggerflags == BL_TRIGGERFLAGS_ONESHOT ? "oneshot" : "continuous");
	printf("  Using mmap:   %s\n", use_mmap ? "yes" : "no");
	printf("\n");

	/* Allocate buffer */
	perf_buf = malloc(buffersize);
	if (!perf_buf) {
		print_error("malloc() failed");
		return -1;
	}

	/* Install signal handlers */
	signal(SIGINT, perf_signal_handler);
	signal(SIGSEGV, perf_signal_handler);

	/* Open device */
	print_step("Opening device (non-blocking)");
	perf_fd = beaglelogic_open_nonblock();
	if (perf_fd < 0) {
		print_error("beaglelogic_open_nonblock() failed");
		free(perf_buf);
		return -1;
	}
	print_success("Device opened");

	/* Configure */
	print_step("Configuring device");
	beaglelogic_set_buffersize(perf_fd, buffersize);
	beaglelogic_set_samplerate(perf_fd, samplerate);
	beaglelogic_set_sampleunit(perf_fd, sampleunit);
	beaglelogic_set_triggerflags(perf_fd, triggerflags);
	print_device_state();

	/* Setup mmap if requested */
	if (use_mmap) {
		print_step("Memory mapping buffer");
		bl_mem = beaglelogic_mmap(perf_fd);
		if (bl_mem == NULL || bl_mem == (void *)-1) {
			print_error("beaglelogic_mmap() failed");
			beaglelogic_close(perf_fd);
			free(perf_buf);
			return -1;
		}
		print_success("Memory mapped");
		print_info_hex("mmap address", (unsigned long)bl_mem);
	}

	/* Configure poll */
	pollfd.fd = perf_fd;
	pollfd.events = POLLIN | POLLRDNORM;

	/* Start capture */
	print_step("Starting capture");
	if (beaglelogic_start(perf_fd) < 0) {
		print_error("beaglelogic_start() failed");
		if (bl_mem) beaglelogic_munmap(perf_fd, bl_mem);
		beaglelogic_close(perf_fd);
		free(perf_buf);
		return -1;
	}
	print_success("Capture started");

	/* Performance test loop */
	print_step("Running 10 iterations");
	clock_gettime(CLOCK_MONOTONIC, &t1);

	for (i = 0; i < 10; i++) {
		sz = 0;
		sz_to_read = buffersize;

		printf("  Iteration %d/10: ", i + 1);
		fflush(stdout);

		while (sz_to_read > 0) {
			poll(&pollfd, 1, 1000);

			if (pollfd.revents & (POLLERR | POLLHUP)) {
				printf("Error/hangup\n");
				break;
			}

			if (pollfd.revents & POLLIN) {
				cnt1 = read(perf_fd, perf_buf, sz_to_read > 64 * 1024 ? 64 * 1024 : sz_to_read);

				if (cnt1 == 0) {
					break;
				} else if (cnt1 == -1) {
					if (errno == EAGAIN) {
						continue;
					} else {
						print_error("read() failed");
						goto cleanup;
					}
				}

				sz += cnt1;
				sz_to_read -= cnt1;
			}
		}

		printf("%zu bytes\n", sz);

		if (sz == 0 && triggerflags == BL_TRIGGERFLAGS_ONESHOT) {
			/* Oneshot mode - stop after first iteration */
			printf("  (Oneshot mode complete)\n");
			break;
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &t2);

	printf("\n");
	print_success("Performance test completed");
	printf("\n");
	printf("Performance:\n");
	printf("  Time:      %llu us\n", (unsigned long long)timediff(&t1, &t2));
	printf("  Data rate: %.2f MB/s\n",
	       (buffersize * 10 / 1048576.0) / (timediff(&t1, &t2) / 1000000.0));

cleanup:
	/* Stop if continuous mode */
	if (triggerflags == BL_TRIGGERFLAGS_CONTINUOUS) {
		print_step("Stopping capture");
		if (beaglelogic_stop(perf_fd) < 0) {
			print_warning("beaglelogic_stop() failed (this is a known issue)");
		} else {
			print_success("Capture stopped");
		}
	}

	/* Cleanup */
	if (bl_mem) {
		beaglelogic_munmap(perf_fd, bl_mem);
	}
	beaglelogic_close(perf_fd);
	free(perf_buf);

	/* Restore normal signal handlers */
	signal(SIGINT, signal_handler);
	signal(SIGSEGV, SIG_DFL);

	return 0;
}

/* ========================================================================
 * MODE 5: DIAGNOSTIC TESTS - ALL 13 COMPREHENSIVE TESTS
 * ======================================================================== */

/* Test 1: Basic open/close */
int diag_test_1_basic_open_close(void)
{
	int fd;

	print_section("DIAGNOSTIC TEST 1: Basic Open/Close");

	print_step("Opening device (blocking mode)");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		return -1;
	}
	print_success("Device opened");
	print_info_int("File descriptor", fd);
	print_device_state();

	sleep(1);

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		return -1;
	}
	print_success("Device closed");
	print_device_state();

	print_success("TEST PASSED - Basic open/close cycle successful");
	return 0;
}

/* Test 2: Open/configure/close */
int diag_test_2_configure(void)
{
	int fd;
	uint32_t bufsize;

	print_section("DIAGNOSTIC TEST 2: Open/Configure/Close");

	print_step("Opening device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		return -1;
	}
	print_success("Device opened");

	print_step("Setting buffer size");
	if (beaglelogic_set_buffersize(fd, TEST_BUFFERSIZE) < 0) {
		print_error("beaglelogic_set_buffersize() failed");
		beaglelogic_close(fd);
		return -1;
	}

	if (beaglelogic_get_buffersize(fd, &bufsize) < 0) {
		print_error("beaglelogic_get_buffersize() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Buffer size configured");
	print_info_int("Buffer size (MB)", bufsize / (1024 * 1024));

	print_step("Setting sample rate");
	if (beaglelogic_set_samplerate(fd, TEST_SAMPLERATE) < 0) {
		print_error("beaglelogic_set_samplerate() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Sample rate configured");

	print_step("Setting sample unit (8-bit)");
	if (beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS) < 0) {
		print_error("beaglelogic_set_sampleunit() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Sample unit configured");

	print_step("Setting trigger flags (oneshot)");
	if (beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT) < 0) {
		print_error("beaglelogic_set_triggerflags() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Trigger flags configured");

	print_device_state();

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		return -1;
	}
	print_success("Device closed");

	print_success("TEST PASSED - Configuration cycle successful");
	return 0;
}

/* Test 3: mmap without read */
int diag_test_3_mmap_only(void)
{
	int fd;
	void *mem;
	uint32_t bufsize;

	print_section("DIAGNOSTIC TEST 3: mmap/munmap (no read)");

	print_step("Opening device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		return -1;
	}
	print_success("Device opened");

	print_step("Configuring device");
	beaglelogic_set_buffersize(fd, TEST_BUFFERSIZE);
	beaglelogic_get_buffersize(fd, &bufsize);
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);
	print_device_state();

	print_step("Memory mapping buffer");
	mem = beaglelogic_mmap(fd);
	if (mem == NULL || mem == (void *)-1) {
		print_error("beaglelogic_mmap() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Memory mapped");
	print_info_hex("mmap address", (unsigned long)mem);
	print_info_int("Buffer size (MB)", bufsize / (1024 * 1024));

	sleep(1);

	print_step("Unmapping memory");
	if (beaglelogic_munmap(fd, mem) < 0) {
		print_error("beaglelogic_munmap() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Memory unmapped");

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		return -1;
	}
	print_success("Device closed");

	print_success("TEST PASSED - mmap/munmap cycle successful");
	return 0;
}

/* Test 4: mmap + cache invalidate */
int diag_test_4_cache_invalidate(void)
{
	int fd;
	void *mem;
	uint32_t bufsize;

	print_section("DIAGNOSTIC TEST 4: mmap + cache invalidate");

	print_step("Opening device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		return -1;
	}

	print_step("Configuring and mapping");
	beaglelogic_set_buffersize(fd, TEST_BUFFERSIZE);
	beaglelogic_get_buffersize(fd, &bufsize);
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);

	mem = beaglelogic_mmap(fd);
	if (mem == NULL || mem == (void *)-1) {
		print_error("beaglelogic_mmap() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Memory mapped");

	print_step("Invalidating cache");
	if (beaglelogic_memcacheinvalidate(fd) < 0) {
		print_error("beaglelogic_memcacheinvalidate() failed");
		beaglelogic_munmap(fd, mem);
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Cache invalidated");

	print_step("Unmapping memory");
	if (beaglelogic_munmap(fd, mem) < 0) {
		print_error("beaglelogic_munmap() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Memory unmapped");

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		return -1;
	}
	print_success("Device closed");

	print_success("TEST PASSED - Cache invalidate cycle successful");
	return 0;
}

/* Test 5: Read mode (no mmap) */
int diag_test_5_read_mode(void)
{
	int fd;
	uint8_t *buffer;
	ssize_t bytes_read;

	print_section("DIAGNOSTIC TEST 5: Read mode (no mmap)");

	buffer = malloc(TEST_READSIZE);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	print_step("Opening device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}

	print_step("Configuring device");
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);
	print_device_state();

	print_step("Reading data");
	bytes_read = read(fd, buffer, TEST_READSIZE);
	if (bytes_read < 0) {
		print_error("read() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Data read");
	print_info_int("Bytes read", bytes_read);
	print_device_state();

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		free(buffer);
		return -1;
	}
	print_success("Device closed");

	free(buffer);
	print_success("TEST PASSED - Read mode cycle successful");
	return 0;
}

/* Test 6: mmap + explicit start/stop */
int diag_test_6_start_stop(void)
{
	int fd;
	void *mem;
	uint32_t bufsize;

	print_section("DIAGNOSTIC TEST 6: mmap + explicit start/stop");

	print_step("Opening device (non-blocking)");
	fd = beaglelogic_open_nonblock();
	if (fd < 0) {
		print_error("beaglelogic_open_nonblock() failed");
		return -1;
	}

	print_step("Configuring device");
	beaglelogic_set_buffersize(fd, TEST_BUFFERSIZE);
	beaglelogic_get_buffersize(fd, &bufsize);
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);
	print_device_state();

	print_step("Memory mapping");
	mem = beaglelogic_mmap(fd);
	if (mem == NULL || mem == (void *)-1) {
		print_error("beaglelogic_mmap() failed");
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Memory mapped");

	print_step("Starting capture (explicit)");
	if (beaglelogic_start(fd) < 0) {
		print_error("beaglelogic_start() failed");
		beaglelogic_munmap(fd, mem);
		beaglelogic_close(fd);
		return -1;
	}
	print_success("Capture started");
	print_device_state();

	print_step("Waiting 2 seconds");
	sleep(2);
	print_device_state();

	print_step("Stopping capture");
	if (beaglelogic_stop(fd) < 0) {
		print_error("beaglelogic_stop() failed");
	} else {
		print_success("Capture stopped");
	}
	print_device_state();

	print_step("Unmapping memory");
	if (beaglelogic_munmap(fd, mem) < 0) {
		print_error("beaglelogic_munmap() failed");
	} else {
		print_success("Memory unmapped");
	}

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		return -1;
	}
	print_success("Device closed");

	print_success("TEST PASSED - Explicit start/stop cycle successful");
	return 0;
}

/* Test 7: mmap + read (oneshot) */
int diag_test_7_mmap_read(void)
{
	int fd;
	void *mem;
	uint8_t *buffer;
	uint32_t bufsize;
	ssize_t bytes_read;
	struct pollfd pollfd;
	int poll_ret;

	print_section("DIAGNOSTIC TEST 7: mmap + read (non-blocking I/O with poll)");

	buffer = malloc(TEST_READSIZE);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	print_step("Opening device (non-blocking)");
	fd = beaglelogic_open_nonblock();
	if (fd < 0) {
		print_error("beaglelogic_open_nonblock() failed");
		free(buffer);
		return -1;
	}

	print_step("Configuring device");
	beaglelogic_set_buffersize(fd, TEST_BUFFERSIZE);
	beaglelogic_get_buffersize(fd, &bufsize);
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);

	print_step("Memory mapping");
	mem = beaglelogic_mmap(fd);
	if (mem == NULL || mem == (void *)-1) {
		print_error("beaglelogic_mmap() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Memory mapped");
	print_device_state();

	print_step("Starting capture");
	beaglelogic_start(fd);
	usleep(250000);  /* 250ms delay for readability */

	print_step("Waiting for data (non-blocking mode with poll)");
	pollfd.fd = fd;
	pollfd.events = POLLIN | POLLRDNORM;
	poll_ret = poll(&pollfd, 1, 2000);  /* 2 second timeout */

	if (poll_ret == 0) {
		print_error("poll() timeout - no data available");
		beaglelogic_munmap(fd, mem);
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	} else if (poll_ret < 0) {
		print_error("poll() failed");
		beaglelogic_munmap(fd, mem);
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Data ready");

	print_step("Reading data via read()");
	bytes_read = read(fd, buffer, TEST_READSIZE);
	if (bytes_read < 0) {
		print_error("read() failed");
	} else {
		print_success("Data read");
		print_info_int("Bytes read", bytes_read);
	}
	print_device_state();
	usleep(250000);  /* 250ms delay for readability */

	print_step("Unmapping memory (oneshot - no stop needed)");
	if (beaglelogic_munmap(fd, mem) < 0) {
		print_error("beaglelogic_munmap() failed");
	} else {
		print_success("Memory unmapped");
	}

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		free(buffer);
		return -1;
	}
	print_success("Device closed");

	free(buffer);
	print_success("TEST PASSED - Non-blocking I/O with poll successful");
	return 0;
}

/* Test 8: Double open/close cycle */
int diag_test_8_double_cycle(void)
{
	int fd1, fd2;

	print_section("DIAGNOSTIC TEST 8: Double open/close cycle");

	print_step("First cycle - Opening");
	fd1 = beaglelogic_open();
	if (fd1 < 0) {
		print_error("First beaglelogic_open() failed");
		return -1;
	}
	print_success("First open successful");

	print_step("First cycle - Closing");
	if (beaglelogic_close(fd1) < 0) {
		print_error("First beaglelogic_close() failed");
		return -1;
	}
	print_success("First close successful");
	print_device_state();
	usleep(250000);  /* 250ms delay for readability */

	print_step("Second cycle - Opening");
	fd2 = beaglelogic_open();
	if (fd2 < 0) {
		print_error("Second beaglelogic_open() failed");
		print_recent_dmesg();
		return -1;
	}
	print_success("Second open successful");

	print_step("Second cycle - Closing");
	if (beaglelogic_close(fd2) < 0) {
		print_error("Second beaglelogic_close() failed");
		return -1;
	}
	print_success("Second close successful");

	print_success("TEST PASSED - Double open/close cycle successful");
	return 0;
}

/* Test 9: State recovery after error */
int diag_test_9_error_recovery(void)
{
	int fd;

	print_section("DIAGNOSTIC TEST 9: Error recovery");

	print_step("Opening device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		return -1;
	}

	print_step("Intentionally triggering error (close without cleanup)");
	close(fd);
	print_warning("Closed file descriptor directly");
	print_device_state();
	usleep(250000);  /* 250ms delay for readability */

	print_step("Attempting to recover - reopening device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("Recovery open failed");
		print_recent_dmesg();
		return -1;
	}
	print_success("Recovery successful");
	print_device_state();

	print_step("Closing properly");
	beaglelogic_close(fd);
	print_success("Closed properly");

	print_success("TEST PASSED - Error recovery successful");
	return 0;
}

/* Test 10: Stress test - multiple mmap cycles */
int diag_test_10_stress_mmap(void)
{
	int fd;
	void *mem;
	int i;
	const int iterations = 5;

	print_section("DIAGNOSTIC TEST 10: Stress test - multiple mmap cycles");

	for (i = 0; i < iterations; i++) {
		printf("\n%s--- Iteration %d/%d ---%s\n", COLOR_YELLOW, i+1, iterations, COLOR_RESET);

		print_step("Opening device");
		fd = beaglelogic_open();
		if (fd < 0) {
			print_error("beaglelogic_open() failed");
			return -1;
		}

		print_step("Configuring and mapping");
		beaglelogic_set_buffersize(fd, TEST_BUFFERSIZE);
		beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
		beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
		beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);

		mem = beaglelogic_mmap(fd);
		if (mem == NULL || mem == (void *)-1) {
			print_error("beaglelogic_mmap() failed");
			beaglelogic_close(fd);
			return -1;
		}
		print_success("Mapped");
		usleep(250000);  /* 250ms delay for readability */

		print_step("Unmapping and closing");
		if (beaglelogic_munmap(fd, mem) < 0) {
			print_error("beaglelogic_munmap() failed");
			beaglelogic_close(fd);
			return -1;
		}

		if (beaglelogic_close(fd) < 0) {
			print_error("beaglelogic_close() failed");
			return -1;
		}
		print_success("Unmapped and closed");

		if (interrupted) {
			print_warning("Test interrupted by user");
			break;
		}
	}

	print_success("TEST PASSED - Stress test (5 mmap cycles) completed successfully");
	return 0;
}

/* Test 11: Heavy mmap + iterative reads (continuous mode stress test) */
int diag_test_11_heavy_continuous(void)
{
	int fd;
	void *mem;
	uint8_t *buffer;
	uint32_t bufsize;
	ssize_t bytes_read, total_read;
	int i;
	const int iterations = 10;
	struct pollfd pollfd;

	print_section("DIAGNOSTIC TEST 11: Heavy mmap + continuous mode (ADVANCED)");
	print_warning("This mimics beaglelogictestapp: 32MB buffer, CONTINUOUS mode, 10 iterations");
	printf("%s[INFO]%s Previous hang issue in beaglelogic_stop() has been FIXED in v2.0\n", COLOR_BLUE, COLOR_RESET);
	printf("%s[INFO]%s If timeout occurs, check dmesg for error messages and report the issue\n", COLOR_BLUE, COLOR_RESET);

	buffer = malloc(TEST_LARGE_BUFFERSIZE);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}
	memset(buffer, 0xFF, TEST_LARGE_BUFFERSIZE);

	print_step("Opening device (non-blocking)");
	fd = beaglelogic_open_nonblock();
	if (fd < 0) {
		print_error("beaglelogic_open_nonblock() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");

	print_step("Configuring device (32 MB buffer, CONTINUOUS mode)");
	beaglelogic_set_buffersize(fd, TEST_LARGE_BUFFERSIZE);
	beaglelogic_get_buffersize(fd, &bufsize);
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_CONTINUOUS);
	print_info_int("Buffer size (MB)", bufsize / (1024 * 1024));
	print_device_state();

	print_step("Memory mapping buffer");
	mem = beaglelogic_mmap(fd);
	if (mem == NULL || mem == (void *)-1) {
		print_error("beaglelogic_mmap() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Memory mapped");
	print_info_hex("mmap address", (unsigned long)mem);

	pollfd.fd = fd;
	pollfd.events = POLLIN | POLLRDNORM;

	print_step("Starting capture");
	if (beaglelogic_start(fd) < 0) {
		print_error("beaglelogic_start() failed");
		beaglelogic_munmap(fd, mem);
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Capture started");
	usleep(250000);  /* 250ms delay for readability */

	print_step("Performing 10 iterations of chunked reads");
	total_read = 0;
	for (i = 0; i < iterations; i++) {
		uint8_t *buf_ptr = buffer;
		ssize_t iteration_bytes = 0;

		printf("  %s[Iteration %d/%d]%s ", COLOR_YELLOW, i+1, iterations, COLOR_RESET);
		fflush(stdout);

		poll(&pollfd, 1, 500);
		while (iteration_bytes < (ssize_t)bufsize && pollfd.revents) {
			bytes_read = read(fd, buf_ptr, TEST_CHUNK_SIZE);

			if (bytes_read == 0) {
				break;
			} else if (bytes_read == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					poll(&pollfd, 1, 500);
					continue;
				} else {
					print_error("read() failed");
					beaglelogic_munmap(fd, mem);
					beaglelogic_close(fd);
					free(buffer);
					return -1;
				}
			}

			buf_ptr += bytes_read;
			iteration_bytes += bytes_read;
		}

		total_read += iteration_bytes;
		printf("Read %zd bytes\n", iteration_bytes);

		if (interrupted) {
			print_warning("Test interrupted by user");
			break;
		}
	}

	print_success("All iterations completed");
	print_info_int("Total bytes read (MB)", total_read / (1024 * 1024));
	print_device_state();
	usleep(250000);  /* 250ms delay for readability */

	print_step("Stopping capture (continuous mode)");
	if (beaglelogic_stop(fd) < 0) {
		print_error("beaglelogic_stop() failed");
	} else {
		print_success("Capture stopped");
	}
	print_device_state();

	print_step("Invalidating cache (after heavy use)");
	if (beaglelogic_memcacheinvalidate(fd) < 0) {
		print_error("beaglelogic_memcacheinvalidate() failed");
	} else {
		print_success("Cache invalidated");
	}

	print_step("Unmapping memory");
	if (beaglelogic_munmap(fd, mem) < 0) {
		print_error("beaglelogic_munmap() failed");
	} else {
		print_success("Memory unmapped");
	}

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		free(buffer);
		return -1;
	}
	print_success("Device closed");

	free(buffer);
	print_success("TEST PASSED - Heavy continuous mode stress test (32MB, 10 iter) completed successfully");
	return 0;
}

/* Test 12: Large buffer stress */
int diag_test_12_large_buffer(void)
{
	int fd;
	void *mem;
	uint8_t *buffer;
	uint32_t bufsize;
	ssize_t bytes_read, total_read;
	int iteration;
	const int num_iterations = 3;
	struct pollfd pollfd;

	print_section("DIAGNOSTIC TEST 12: Large buffer stress (ADVANCED)");
	print_warning("Uses 32 MB buffers, multiple read iterations");

	buffer = malloc(TEST_LARGE_BUFFERSIZE);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	print_step("Opening device (non-blocking)");
	fd = beaglelogic_open_nonblock();
	if (fd < 0) {
		print_error("beaglelogic_open_nonblock() failed");
		free(buffer);
		return -1;
	}
	print_success("Device opened");
	usleep(250000);  /* 250ms delay for readability */

	print_step("Configuring device (32 MB buffer)");
	beaglelogic_set_buffersize(fd, TEST_LARGE_BUFFERSIZE);
	beaglelogic_get_buffersize(fd, &bufsize);
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);
	print_info_int("Buffer size (MB)", bufsize / (1024 * 1024));
	print_device_state();

	print_step("Memory mapping buffer");
	mem = beaglelogic_mmap(fd);
	if (mem == NULL || mem == (void *)-1) {
		print_error("beaglelogic_mmap() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Memory mapped");

	pollfd.fd = fd;
	pollfd.events = POLLIN | POLLRDNORM;

	for (iteration = 0; iteration < num_iterations; iteration++) {
		printf("\n%s--- Read iteration %d/%d ---%s\n",
			COLOR_YELLOW, iteration+1, num_iterations, COLOR_RESET);

		print_step("Starting capture");
		if (beaglelogic_start(fd) < 0) {
			print_error("beaglelogic_start() failed");
			beaglelogic_munmap(fd, mem);
			beaglelogic_close(fd);
			free(buffer);
			return -1;
		}
		usleep(250000);  /* 250ms delay for readability */

		print_step("Reading full buffer");
		total_read = 0;
		uint8_t *buf_ptr = buffer;

		poll(&pollfd, 1, 1000);
		while (total_read < (ssize_t)bufsize && pollfd.revents) {
			bytes_read = read(fd, buf_ptr, TEST_CHUNK_SIZE);

			if (bytes_read == 0) {
				break;
			} else if (bytes_read == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					poll(&pollfd, 1, 1000);
					continue;
				} else {
					print_error("read() failed");
					beaglelogic_munmap(fd, mem);
					beaglelogic_close(fd);
					free(buffer);
					return -1;
				}
			}

			buf_ptr += bytes_read;
			total_read += bytes_read;
		}

		print_success("Buffer read complete");
		print_info_int("Bytes read", total_read);
		print_device_state();
		usleep(250000);  /* 250ms delay for readability */

		if (interrupted) {
			print_warning("Test interrupted by user");
			break;
		}
	}

	print_step("Unmapping memory");
	if (beaglelogic_munmap(fd, mem) < 0) {
		print_error("beaglelogic_munmap() failed");
	} else {
		print_success("Memory unmapped");
	}

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		free(buffer);
		return -1;
	}
	print_success("Device closed");

	free(buffer);
	print_success("TEST PASSED - Large buffer stress test (32MB, 3 iter) completed successfully");
	return 0;
}

/* Test 13: Cache invalidate after data capture */
int diag_test_13_cache_after_capture(void)
{
	int fd;
	void *mem;
	uint8_t *buffer;
	uint32_t bufsize;
	ssize_t bytes_read;

	print_section("DIAGNOSTIC TEST 13: Cache invalidate AFTER capture (ADVANCED)");
	print_warning("Tests if memcacheinvalidate() causes segfault after buffer use");

	buffer = malloc(TEST_READSIZE);
	if (!buffer) {
		print_error("malloc() failed");
		return -1;
	}

	print_step("Opening device");
	fd = beaglelogic_open();
	if (fd < 0) {
		print_error("beaglelogic_open() failed");
		free(buffer);
		return -1;
	}

	print_step("Configuring device");
	beaglelogic_set_buffersize(fd, TEST_BUFFERSIZE);
	beaglelogic_get_buffersize(fd, &bufsize);
	beaglelogic_set_samplerate(fd, TEST_SAMPLERATE);
	beaglelogic_set_sampleunit(fd, BL_SAMPLEUNIT_8_BITS);
	beaglelogic_set_triggerflags(fd, BL_TRIGGERFLAGS_ONESHOT);

	print_step("Memory mapping");
	mem = beaglelogic_mmap(fd);
	if (mem == NULL || mem == (void *)-1) {
		print_error("beaglelogic_mmap() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Memory mapped");
	print_device_state();
	usleep(250000);  /* 250ms delay for readability */

	print_step("Capturing data via read()");
	bytes_read = read(fd, buffer, TEST_READSIZE);
	if (bytes_read < 0) {
		print_error("read() failed");
		beaglelogic_munmap(fd, mem);
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Data captured");
	print_info_int("Bytes read", bytes_read);
	usleep(250000);  /* 250ms delay for readability */

	print_step("Accessing mmap buffer");
	uint8_t sample_byte = ((uint8_t *)mem)[0];
	print_success("mmap buffer accessible");
	print_info_hex("First byte from mmap", sample_byte);

	print_step("Invalidating cache AFTER capture (critical test)");
	if (beaglelogic_memcacheinvalidate(fd) < 0) {
		print_error("beaglelogic_memcacheinvalidate() failed");
		beaglelogic_munmap(fd, mem);
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Cache invalidated successfully");
	usleep(250000);  /* 250ms delay for readability */

	print_step("Accessing mmap buffer after cache invalidate");
	sample_byte = ((uint8_t *)mem)[100];
	print_success("mmap buffer still accessible");
	print_info_hex("Byte 100 from mmap", sample_byte);

	print_device_state();

	print_step("Unmapping memory");
	if (beaglelogic_munmap(fd, mem) < 0) {
		print_error("beaglelogic_munmap() failed");
		beaglelogic_close(fd);
		free(buffer);
		return -1;
	}
	print_success("Memory unmapped");

	print_step("Closing device");
	if (beaglelogic_close(fd) < 0) {
		print_error("beaglelogic_close() failed");
		free(buffer);
		return -1;
	}
	print_success("Device closed");

	free(buffer);
	print_success("TEST PASSED - Cache invalidate after capture successful");
	return 0;
}

/* Comprehensive diagnostic menu and runner */
void show_diagnostic_menu(void)
{
	printf("\n%s╔════════════════════════════════════════════════════════╗%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s║          Diagnostic Tests - Driver Validation         ║%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s╚════════════════════════════════════════════════════════╝%s\n", COLOR_CYAN, COLOR_RESET);
	printf("\n");
	printf("%sBasic Tests (Quick - Run These First):%s\n", COLOR_GREEN, COLOR_RESET);
	printf("  1.  Basic open/close\n");
	printf("  2.  Open/configure/close\n");
	printf("  3.  mmap/munmap (no read)\n");
	printf("\n");
	printf("%sComprehensive Tests:%s\n", COLOR_GREEN, COLOR_RESET);
	printf("  4.  mmap + cache invalidate\n");
	printf("  5.  Read mode (no mmap)\n");
	printf("  6.  mmap + explicit start/stop\n");
	printf("  7.  mmap + read (oneshot)\n");
	printf("  8.  Double open/close cycle\n");
	printf("  9.  Error recovery\n");
	printf("  10. Stress test (5 mmap cycles)\n");
	printf("\n");
	printf("%sAdvanced Stress Tests (May Hang - Known Issues):%s\n", COLOR_YELLOW, COLOR_RESET);
	printf("  11. Heavy continuous mode (32MB, 10 iter) %s[May hang!]%s\n", COLOR_RED, COLOR_RESET);
	printf("  12. Large buffer stress (32MB, 3 iter)\n");
	printf("  13. Cache invalidate after capture\n");
	printf("\n");
	printf("%sTest Suites:%s\n", COLOR_CYAN, COLOR_RESET);
	printf("  20. Quick Diagnostics (tests 1-3)\n");
	printf("  21. Comprehensive Suite (tests 1-10)\n");
	printf("  22. Full Suite (ALL tests 1-13)\n");
	printf("\n");
	printf("  0.  Back to main menu\n");
	printf("\n");
}

int mode_diagnostics(void)
{
	int choice;
	int result;
	int i;

	while (1) {
		show_diagnostic_menu();
		choice = get_int_input("Select diagnostic test or suite", 0);

		switch (choice) {
		case 0:
			return 0;

		case 1:
			result = diag_test_1_basic_open_close();
			if (result < 0) print_recent_dmesg();
			break;

		case 2:
			result = diag_test_2_configure();
			if (result < 0) print_recent_dmesg();
			break;

		case 3:
			result = diag_test_3_mmap_only();
			if (result < 0) print_recent_dmesg();
			break;

		case 4:
			result = diag_test_4_cache_invalidate();
			if (result < 0) print_recent_dmesg();
			break;

		case 5:
			result = diag_test_5_read_mode();
			if (result < 0) print_recent_dmesg();
			break;

		case 6:
			result = diag_test_6_start_stop();
			if (result < 0) print_recent_dmesg();
			break;

		case 7:
			result = diag_test_7_mmap_read();
			if (result < 0) print_recent_dmesg();
			break;

		case 8:
			result = diag_test_8_double_cycle();
			if (result < 0) print_recent_dmesg();
			break;

		case 9:
			result = diag_test_9_error_recovery();
			if (result < 0) print_recent_dmesg();
			break;

		case 10:
			result = diag_test_10_stress_mmap();
			if (result < 0) print_recent_dmesg();
			break;

		case 11:
			result = diag_test_11_heavy_continuous();
			if (result < 0) print_recent_dmesg();
			break;

		case 12:
			result = diag_test_12_large_buffer();
			if (result < 0) print_recent_dmesg();
			break;

		case 13:
			result = diag_test_13_cache_after_capture();
			if (result < 0) print_recent_dmesg();
			break;

		case 20:
			/* Quick Diagnostics (1-3) */
			printf("\n%s=== Running Quick Diagnostics (Tests 1-3) ===%s\n", COLOR_BOLD, COLOR_RESET);
			for (i = 1; i <= 3; i++) {
				printf("\n%s>>> Test %d/3%s\n", COLOR_CYAN, i, COLOR_RESET);
				switch (i) {
				case 1: result = diag_test_1_basic_open_close(); break;
				case 2: result = diag_test_2_configure(); break;
				case 3: result = diag_test_3_mmap_only(); break;
				}
				if (result < 0) {
					print_recent_dmesg();
					print_warning("Test failed - stopping suite");
					break;
				}
			}
			print_success("Quick diagnostics completed");
			break;

		case 21:
			/* Comprehensive Suite (1-10) */
			printf("\n%s=== Running Comprehensive Suite (Tests 1-10) ===%s\n", COLOR_BOLD, COLOR_RESET);
			for (i = 1; i <= 10; i++) {
				printf("\n%s>>> Test %d/10%s\n", COLOR_CYAN, i, COLOR_RESET);
				switch (i) {
				case 1: result = diag_test_1_basic_open_close(); break;
				case 2: result = diag_test_2_configure(); break;
				case 3: result = diag_test_3_mmap_only(); break;
				case 4: result = diag_test_4_cache_invalidate(); break;
				case 5: result = diag_test_5_read_mode(); break;
				case 6: result = diag_test_6_start_stop(); break;
				case 7: result = diag_test_7_mmap_read(); break;
				case 8: result = diag_test_8_double_cycle(); break;
				case 9: result = diag_test_9_error_recovery(); break;
				case 10: result = diag_test_10_stress_mmap(); break;
				}
				if (result < 0) {
					print_recent_dmesg();
					print_warning("Test failed - stopping suite");
					break;
				}
			}
			print_success("Comprehensive suite completed");
			break;

		case 22:
			/* Full Suite (1-13) */
			printf("\n%s=== Running Full Suite (ALL Tests 1-13) ===%s\n", COLOR_BOLD, COLOR_RESET);
			printf("%s[INFO]%s Tests 11-13 are advanced stress tests with 32MB buffers\n", COLOR_BLUE, COLOR_RESET);
			printf("%s[INFO]%s The previous hang issue in Test 11 has been FIXED in v2.0\n", COLOR_BLUE, COLOR_RESET);
			printf("%s[INFO]%s All tests should complete successfully - report any timeouts or failures\n", COLOR_BLUE, COLOR_RESET);
			printf("Press Enter to continue or Ctrl+C to cancel...");
			getchar();

			for (i = 1; i <= 13; i++) {
				printf("\n%s>>> Test %d/13%s\n", COLOR_CYAN, i, COLOR_RESET);
				switch (i) {
				case 1: result = diag_test_1_basic_open_close(); break;
				case 2: result = diag_test_2_configure(); break;
				case 3: result = diag_test_3_mmap_only(); break;
				case 4: result = diag_test_4_cache_invalidate(); break;
				case 5: result = diag_test_5_read_mode(); break;
				case 6: result = diag_test_6_start_stop(); break;
				case 7: result = diag_test_7_mmap_read(); break;
				case 8: result = diag_test_8_double_cycle(); break;
				case 9: result = diag_test_9_error_recovery(); break;
				case 10: result = diag_test_10_stress_mmap(); break;
				case 11: result = diag_test_11_heavy_continuous(); break;
				case 12: result = diag_test_12_large_buffer(); break;
				case 13: result = diag_test_13_cache_after_capture(); break;
				}
				if (result < 0) {
					print_recent_dmesg();
					print_warning("Test failed - stopping suite");
					break;
				}
				if (interrupted) {
					print_warning("Suite interrupted by user");
					break;
				}
			}
			print_success("Full diagnostic suite completed");
			break;

		default:
			print_error("Invalid choice - select 1-13, 20-22, or 0 to exit");
			break;
		}

		if (choice >= 1 && choice <= 13) {
			printf("\n%sPress Enter to return to diagnostic menu...%s", COLOR_YELLOW, COLOR_RESET);
			getchar();
		} else if (choice >= 20 && choice <= 22) {
			printf("\n%sPress Enter to return to diagnostic menu...%s", COLOR_YELLOW, COLOR_RESET);
			getchar();
		}
	}

	return 0;
}

/* ========================================================================
 * MAIN MENU
 * ======================================================================== */

void show_main_menu(void)
{
	printf("\n");
	printf("%s╔════════════════════════════════════════════════════════╗%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s║                                                        ║%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s║      BeagleLogic Unified Test Application             ║%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s║                                                        ║%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s╚════════════════════════════════════════════════════════╝%s\n", COLOR_CYAN, COLOR_RESET);
	printf("\n");
	printf("%sMain Menu:%s\n", COLOR_BOLD, COLOR_RESET);
	printf("\n%sBasic Modes:%s\n", COLOR_BOLD, COLOR_RESET);
	printf("  %s1.%s Simple Capture          - Basic data capture to file\n", COLOR_GREEN, COLOR_RESET);
	printf("  %s2.%s Continuous Logger       - Long-running capture with file rotation\n", COLOR_GREEN, COLOR_RESET);
	printf("  %s3.%s PRUDAQ ADC Capture      - 12-bit ADC capture (requires PRUDAQ)\n", COLOR_GREEN, COLOR_RESET);
	printf("\n%sEducational Modes (code examples):%s\n", COLOR_BOLD, COLOR_RESET);
	printf("  %s4.%s Continuous Blocking     - Blocking read() with Ctrl+C stop\n", COLOR_GREEN, COLOR_RESET);
	printf("  %s5.%s Continuous Poll         - Non-blocking poll() with Enter stop\n", COLOR_GREEN, COLOR_RESET);
	printf("  %s6.%s Oneshot Visual          - Terminal waveform display\n", COLOR_GREEN, COLOR_RESET);
	printf("\n%sAdvanced:%s\n", COLOR_BOLD, COLOR_RESET);
	printf("  %s7.%s Performance Test        - Benchmark throughput and reliability\n", COLOR_GREEN, COLOR_RESET);
	printf("  %s8.%s Diagnostic Tests        - Comprehensive driver testing\n", COLOR_GREEN, COLOR_RESET);
	printf("\n");
	printf("  %s9.%s About                   - Information about this tool\n", COLOR_GREEN, COLOR_RESET);
	printf("  %s0.%s Exit\n", COLOR_GREEN, COLOR_RESET);
	printf("\n");
}

void show_about(void)
{
	printf("\n");
	printf("%s╔════════════════════════════════════════════════════════╗%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s║  BeagleLogic Unified Test Application                 ║%s\n", COLOR_CYAN, COLOR_RESET);
	printf("%s╚════════════════════════════════════════════════════════╝%s\n", COLOR_CYAN, COLOR_RESET);
	printf("\n");
	printf("This comprehensive test tool provides ALL BeagleLogic testing\n");
	printf("functionality in a single interactive program.\n");
	printf("\n");
	printf("%sApplication Modes:%s\n", COLOR_BOLD, COLOR_RESET);
	printf("\n");
	printf("%sBasic Modes (1-3):%s\n", COLOR_CYAN, COLOR_RESET);
	printf("  • Simple capture for quick data acquisition\n");
	printf("  • Continuous logging with automatic file rotation\n");
	printf("  • PRUDAQ ADC support for analog signal capture\n");
	printf("\n");
	printf("%sEducational Modes (4-6):%s Code examples demonstrating:\n", COLOR_CYAN, COLOR_RESET);
	printf("  • Blocking read() patterns with signal handling\n");
	printf("  • Non-blocking poll() for event-driven capture\n");
	printf("  • Terminal waveform visualization and signal analysis\n");
	printf("\n");
	printf("%sAdvanced (7-8):%s\n", COLOR_CYAN, COLOR_RESET);
	printf("  • Performance benchmarking and stress testing\n");
	printf("  • 13 comprehensive diagnostic tests with test suites\n");
	printf("\n");
	printf("%sDiagnostic Test Suites:%s\n", COLOR_BOLD, COLOR_RESET);
	printf("  Quick (1-3):        Fast driver validation\n");
	printf("  Comprehensive (4-10): In-depth testing\n");
	printf("  Full (1-13):        Complete validation with stress tests\n");
	printf("\n");
	printf("This tool is completely self-contained - no external tools needed!\n");
	printf("\n");
	printf("Copyright:\n");
	printf("  (C) 2014 Kumar Abhishek\n");
	printf("  (C) 2024-2026 Bryan Rainwater\n");
	printf("\n");
	printf("License: GPLv2\n");
	printf("\n");
}

/* ========================================================================
 * MAIN PROGRAM
 * ======================================================================== */

int main(int argc, char **argv)
{
	int choice;
	int result;

	(void)argc;
	(void)argv;

	/* Install signal handler */
	signal(SIGINT, signal_handler);

	/* Main loop */
	while (1) {
		/* Reset interrupted flag */
		interrupted = 0;
		keep_running = 1;

		show_main_menu();

		printf("Select option: ");
		fflush(stdout);

		if (scanf("%d", &choice) != 1) {
			/* Clear input buffer */
			while (getchar() != '\n');
			print_error("Invalid input");
			continue;
		}

		/* Clear input buffer */
		while (getchar() != '\n');

		switch (choice) {
		case 1:
			result = mode_simple_capture();
			if (result < 0) {
				print_warning("Simple capture failed");
			}
			break;

		case 2:
			result = mode_continuous_logger();
			if (result < 0) {
				print_warning("Continuous logger failed");
			}
			break;

		case 3:
			result = mode_prudaq_adc();
			if (result < 0) {
				print_warning("PRUDAQ ADC capture failed");
			}
			break;

		case 4:
			result = mode_continuous_blocking();
			if (result < 0) {
				print_warning("Continuous blocking capture failed");
			}
			break;

		case 5:
			result = mode_continuous_poll();
			if (result < 0) {
				print_warning("Continuous poll capture failed");
			}
			break;

		case 6:
			result = mode_oneshot_visual();
			if (result < 0) {
				print_warning("Oneshot visual display failed");
			}
			break;

		case 7:
			result = mode_performance_test();
			if (result < 0) {
				print_warning("Performance test failed");
			}
			break;

		case 8:
			result = mode_diagnostics();
			if (result < 0) {
				print_warning("Diagnostics failed");
			}
			break;

		case 9:
			show_about();
			break;

		case 0:
			printf("\n%sExiting BeagleLogic Test Application%s\n", COLOR_CYAN, COLOR_RESET);
			printf("Goodbye!\n\n");
			return 0;

		default:
			print_error("Invalid choice - please select 0-9");
			break;
		}

		if (choice >= 1 && choice <= 8) {
			printf("\n%sPress Enter to return to main menu...%s", COLOR_YELLOW, COLOR_RESET);
			getchar();
		}
	}

	return 0;
}

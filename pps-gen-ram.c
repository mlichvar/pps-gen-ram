/*
 * Memory PPS signal generator
 *
 * Copyright (C) 2020  Miroslav Lichvar <mlichvar@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Buffer of words larger than the CPU cache */
#define BUFFER_LENGTH (uint32_t)((1U << 27) / 4)

/* Shift to get number of words in 64-byte cache line */
#define LINE_SHIFT 4

static void init_buffer(uint32_t *buffer) {
	int i;

	for (i = 0; i < BUFFER_LENGTH; i++)
		buffer[i] = (random() << 16) ^ random();
}

static int get_precision(uint32_t *prec) {
	struct timespec ts1, ts2;
	uint32_t diff, best_diff = 1000000000;
	int i;

	if (clock_gettime(CLOCK_REALTIME, &ts1) < 0)
		return 0;

	for (i = 0; i < 10000; i++) {
		if (clock_gettime(CLOCK_REALTIME, &ts2) < 0)
			return 0;
		diff = (uint32_t)ts2.tv_nsec - (uint32_t)ts1.tv_nsec;
		if (diff > 0 && best_diff > diff)
			best_diff = diff;
		ts1 = ts2;
	}

	*prec = best_diff;

	return 1;
}

static int compare_uint32(const void *a, const void *b) {
	uint32_t x = *(uint32_t *)a;
	uint32_t y = *(uint32_t *)b;

	if (x < y)
		return -1;
	else if (x > y)
		return 1;
	else
		return 0;
}

static inline uint32_t read_ram(volatile uint32_t *buffer, uint32_t rnd) {
	uint32_t index, x;

	index = (rnd << LINE_SHIFT) % BUFFER_LENGTH;
	x = buffer[index];

	__asm__ __volatile__("":::"memory");

	return x;
}

static int get_read_latency(uint32_t *buffer, uint32_t *latency) {
	uint32_t nsec, sum = 0, diffs[100000];
	struct timespec ts1, ts2;
	int i;

	if (clock_gettime(CLOCK_REALTIME, &ts1) < 0)
		return 0;

	for (i = 0; i < sizeof diffs / sizeof diffs[0]; i++) {
		if (clock_gettime(CLOCK_REALTIME, &ts2) < 0)
			return 0;

		nsec = ts2.tv_nsec;
		sum += read_ram(buffer, nsec ^ sum);
		diffs[i] = nsec - (uint32_t)ts1.tv_nsec;

		ts1 = ts2;
	}

	if (sum == -1)
		printf("%"PRIu32"\n", sum);

	qsort(diffs, i, sizeof diffs[0], compare_uint32);

	*latency = diffs[i / 2];

	return 1;
}

static int generate_pps(uint32_t *buffer, uint32_t interval, uint32_t precision) {
	uint32_t sum = 0, nsec, pulse = 0, prev_nsec = 0;
	struct timespec ts;

	while (1) {
		if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
			return 0;

		nsec = ts.tv_nsec;

		if (nsec < pulse && nsec >= prev_nsec)
		       continue;

		if (nsec <= pulse + precision)
			sum += read_ram(buffer, nsec ^ sum);

		prev_nsec = nsec;
		pulse = (nsec / interval + 1) * interval;
		if (pulse >= 1000000000U)
			pulse = 0;
		if (prev_nsec > pulse)
			prev_nsec = pulse;

		if (sum == -1)
			printf("%"PRIu32"\n", sum);
	}

	return 1;
}

static void print_help(void) {
	fprintf(stderr, "Usage: pps-ram-gen INTERVAL\n");
}

int main(int argc, char **argv) {
	uint32_t *buffer, interval, precision, latency;

	setlinebuf(stdout);

	if (argc < 2) {
		print_help();
		return 2;
	}

	interval = atoi(argv[1]);
	if (interval < 100 || 1000000000U % interval != 0) {
		fprintf(stderr, "Invalid interval\n");
		return 2;
	}

	buffer = malloc(BUFFER_LENGTH * sizeof buffer[0]);
	if (!buffer) {
		perror("malloc()");
		return 3;
	}

	init_buffer(buffer);

	if (!get_precision(&precision) || !get_read_latency(buffer, &latency)) {
		perror("clock_gettime():");
		return 4;
	}

	latency -= precision;

	printf("Buffer size     : %"PRIu32" MB\n",
			BUFFER_LENGTH * (uint32_t)sizeof buffer[0] / (1 << 20));
	printf("Pulse interval  : %"PRIu32" ns\n", interval);
	printf("Clock precision : %"PRIu32" ns\n", precision);
	printf("Read latency    : %"PRIu32" ns\n", latency);

	return !generate_pps(buffer, interval, precision / 1);
}

// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;

#define likely(condition) (condition)
#define unlikely(condition) (condition)
#define PXMLT_CHRDEV_MAX_NUM 5
#define TS_SIZE 188
#define INPUT_PACKETS 12

static u32 min(u32 left, u32 right)
{
	return left < right ? left : right;
}

struct ptx_chrdev {
	unsigned int index;
};

struct output_packet {
	unsigned int tuner_index;
	u8 data[TS_SIZE];
};

static struct output_packet output[INPUT_PACKETS];
static size_t output_count;

static int ptx_chrdev_put_stream(struct ptx_chrdev *chrdev, void *buf,
				size_t len)
{
	assert(chrdev);
	assert(len == TS_SIZE);
	assert(output_count < INPUT_PACKETS);

	output[output_count].tuner_index = chrdev->index;
	memcpy(output[output_count].data, buf, len);
	output_count++;

	return 0;
}

#include "../pxmlt_stream.h"

static void run_case(int corrupt_packet, size_t split)
{
	struct pxmlt_stream_context stream = {0};
	struct ptx_chrdev tuners[PXMLT_CHRDEV_MAX_NUM];
	u8 input[INPUT_PACKETS][TS_SIZE] = {0};
	u8 expected[TS_SIZE] = {0};
	u8 *bytes = (u8 *)input;
	size_t packet, output_index;

	output_count = 0;
	for (packet = 0; packet < PXMLT_CHRDEV_MAX_NUM; packet++) {
		tuners[packet].index = packet;
		stream.chrdev[packet] = &tuners[packet];
	}

	for (packet = 0; packet < INPUT_PACKETS; packet++) {
		if ((int)packet == corrupt_packet)
			continue;

		input[packet][0] = ((packet % PXMLT_CHRDEV_MAX_NUM + 1) << 4) | 7;
		input[packet][65] = packet + 1;
		input[packet][TS_SIZE - 1] = 0xA0 + packet;
	}

	if (split <= sizeof(input)) {
		pxmlt_device_stream_handler(&stream, bytes, split);
		pxmlt_device_stream_handler(&stream, bytes + split,
					   sizeof(input) - split);
	} else {
		for (packet = 0; packet < sizeof(input); packet++)
			pxmlt_device_stream_handler(&stream, bytes + packet, 1);
	}

	if (stream.remain_len % TS_SIZE ||
	    output_count + stream.remain_len / TS_SIZE != INPUT_PACKETS - (corrupt_packet >= 0)) {
		fprintf(stderr, "Corrupt packet %d, split %zu: %zu packets, %zu pending bytes\n",
			corrupt_packet, split, output_count, stream.remain_len);
		exit(EXIT_FAILURE);
	}

	for (packet = 0, output_index = 0; packet < INPUT_PACKETS; packet++) {
		const u8 *actual;
		unsigned int tuner_index;

		if ((int)packet == corrupt_packet)
			continue;

		tuner_index = packet % PXMLT_CHRDEV_MAX_NUM;
		expected[65] = packet + 1;
		expected[TS_SIZE - 1] = 0xA0 + packet;
		if (output_index < output_count) {
			expected[0] = 0x47;
			actual = output[output_index].data;
			assert(output[output_index].tuner_index == tuner_index);
		} else {
			/* The parser can keep complete packets until it confirms sync. */
			expected[0] = ((tuner_index + 1) << 4) | 7;
			actual = stream.remain_buf + (output_index - output_count) * TS_SIZE;
		}
		assert(memcmp(actual, expected, TS_SIZE) == 0);
		output_index++;
	}
}

int main(void)
{
	const int corrupt_packets[] = {-1, 0, 6};
	size_t scenario, split;

	for (scenario = 0; scenario < sizeof(corrupt_packets) / sizeof(corrupt_packets[0]); scenario++) {
		for (split = 0; split <= TS_SIZE * INPUT_PACKETS; split++)
			run_case(corrupt_packets[scenario], split);
		run_case(corrupt_packets[scenario], TS_SIZE * INPUT_PACKETS + 1);
	}

	puts("PX-MLT stream boundary tests passed");
	return 0;
}

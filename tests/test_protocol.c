/* SPDX-License-Identifier: MIT */
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct recorded_op {
	uint8_t slave;
	uint8_t reg;
	uint8_t value;
};

struct recorder {
	struct recorded_op operations[64];
	unsigned int delays[64];
	size_t operation_count;
	size_t delay_count;
	size_t fail_at_operation;
};

static int record_write(void *context, uint8_t slave, uint8_t reg, uint8_t value)
{
	struct recorder *recorder = context;
	struct recorded_op *operation = &recorder->operations[recorder->operation_count++];

	operation->slave = slave;
	operation->reg = reg;
	operation->value = value;
	if (recorder->operation_count == recorder->fail_at_operation)
		return -EIO;
	return 0;
}

static int record_sleep(void *context, unsigned int milliseconds)
{
	struct recorder *recorder = context;

	recorder->delays[recorder->delay_count++] = milliseconds;
	return 0;
}

#define ASSERT(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

static int test_single_static(void)
{
	struct recorder recorder = { 0 };
	struct omen_fury_target_set targets;
	struct omen_fury_protocol protocol = {
		.write_context = &recorder, .write = record_write,
		.sleep_context = &recorder, .sleep = record_sleep,
	};
	const uint8_t rgb[] = { 0x12, 0x34, 0x56 };
	const struct recorded_op expected[] = {
		{ 0xc2, 0x08, 0x53 }, { 0xc2, 0x09, 0x00 },
		{ 0xc2, 0x20, 0x20 }, { 0xc2, 0x30, 0x01 },
		{ 0xc2, 0x31, 0x12 }, { 0xc2, 0x32, 0x34 },
		{ 0xc2, 0x33, 0x56 }, { 0xc2, 0x08, 0x44 },
	};

	ASSERT(!omen_fury_target_one(&targets, 0xc2));
	ASSERT(!omen_fury_protocol_static(&protocol, &targets, rgb, 0x20));
	ASSERT(recorder.operation_count == 8);
	ASSERT(!memcmp(recorder.operations, expected, sizeof(expected)));
	ASSERT(recorder.delay_count == 8);
	ASSERT(recorder.delays[0] == 50 && recorder.delays[7] == 50);
	ASSERT(recorder.delays[1] == 5 && recorder.delays[6] == 5);
	return 0;
}

static int test_all_off_staging(void)
{
	struct recorder recorder = { 0 };
	struct omen_fury_target_set targets;
	struct omen_fury_protocol protocol = {
		.write_context = &recorder, .write = record_write,
		.sleep_context = &recorder, .sleep = record_sleep,
	};
	const uint8_t reverse[] = { 0xc6, 0xc4, 0xc2, 0xc0 };
	const uint8_t forward[] = { 0xc0, 0xc2, 0xc4, 0xc6 };
	size_t i;

	omen_fury_targets_all(&targets);
	ASSERT(!omen_fury_protocol_off(&protocol, &targets));
	ASSERT(recorder.operation_count == 12);
	for (i = 0; i < 4; ++i) {
		ASSERT(recorder.operations[i].slave == reverse[i]);
		ASSERT(recorder.operations[i].reg == 0x08);
		ASSERT(recorder.operations[i].value == 0x53);
		ASSERT(recorder.operations[4 + i].slave == forward[i]);
		ASSERT(recorder.operations[4 + i].reg == 0x20);
		ASSERT(recorder.operations[4 + i].value == 0x00);
		ASSERT(recorder.operations[8 + i].slave == forward[i]);
		ASSERT(recorder.operations[8 + i].reg == 0x08);
		ASSERT(recorder.operations[8 + i].value == 0x44);
	}
	return 0;
}

static int test_parsers(void)
{
	uint8_t rgb[3];
	uint8_t slave;

	ASSERT(!omen_fury_parse_rgb("#aBcD01", rgb));
	ASSERT(rgb[0] == 0xab && rgb[1] == 0xcd && rgb[2] == 0x01);
	ASSERT(omen_fury_parse_rgb("red", rgb));
	ASSERT(!omen_fury_parse_slave("C6", &slave) && slave == 0xc6);
	ASSERT(!omen_fury_parse_slave("1", &slave) && slave == 0xc0);
	ASSERT(!omen_fury_parse_slave("4", &slave) && slave == 0xc6);
	ASSERT(!omen_fury_parse_slave("A1", &slave) && slave == 0xc0);
	ASSERT(!omen_fury_parse_slave("b2", &slave) && slave == 0xc6);
	ASSERT(omen_fury_parse_slave("5", &slave));
	ASSERT(omen_fury_parse_slave("C1", &slave));
	return 0;
}

static int test_partial_failure_cleanup(void)
{
	struct recorder recorder = { .fail_at_operation = 6 };
	struct omen_fury_target_set targets;
	struct omen_fury_protocol protocol = {
		.write_context = &recorder, .write = record_write,
		.sleep_context = &recorder, .sleep = record_sleep,
	};
	const uint8_t forward[] = { 0xc0, 0xc2, 0xc4, 0xc6 };
	size_t i;

	omen_fury_targets_all(&targets);
	ASSERT(omen_fury_protocol_off(&protocol, &targets) == -EIO);
	ASSERT(recorder.operation_count == 10);
	for (i = 0; i < 4; ++i) {
		ASSERT(recorder.operations[6 + i].slave == forward[i]);
		ASSERT(recorder.operations[6 + i].reg == 0x08);
		ASSERT(recorder.operations[6 + i].value == 0x44);
	}
	return 0;
}

static int test_begin_failure_cleanup(void)
{
	struct recorder recorder = { .fail_at_operation = 3 };
	struct omen_fury_target_set targets;
	struct omen_fury_protocol protocol = {
		.write_context = &recorder, .write = record_write,
		.sleep_context = &recorder, .sleep = record_sleep,
	};

	omen_fury_targets_all(&targets);
	ASSERT(omen_fury_protocol_off(&protocol, &targets) == -EIO);
	ASSERT(recorder.operation_count == 5);
	ASSERT(recorder.operations[3].slave == 0xc4);
	ASSERT(recorder.operations[3].reg == 0x08);
	ASSERT(recorder.operations[3].value == 0x44);
	ASSERT(recorder.operations[4].slave == 0xc6);
	ASSERT(recorder.operations[4].reg == 0x08);
	ASSERT(recorder.operations[4].value == 0x44);
	return 0;
}

int main(void)
{
	if (test_single_static() || test_all_off_staging() || test_parsers() ||
	    test_partial_failure_cleanup() || test_begin_failure_cleanup())
		return 1;
	puts("protocol tests: PASS");
	return 0;
}

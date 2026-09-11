/* SPDX-License-Identifier: MIT */
#include "protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint8_t all_targets[] = { 0xc0, 0xc2, 0xc4, 0xc6 };

static int send(struct omen_fury_protocol *protocol, uint8_t slave,
		uint8_t reg, uint8_t value, unsigned int delay_ms)
{
	int result = protocol->write(protocol->write_context, slave, reg, value);

	if (result)
		return result;
	return delay_ms ? protocol->sleep(protocol->sleep_context, delay_ms) : 0;
}

int omen_fury_targets_all(struct omen_fury_target_set *targets)
{
	memcpy(targets->values, all_targets, sizeof(all_targets));
	targets->count = sizeof(all_targets);
	return 0;
}

int omen_fury_target_one(struct omen_fury_target_set *targets, uint8_t slave)
{
	size_t i;

	for (i = 0; i < sizeof(all_targets); ++i)
		if (all_targets[i] == slave) {
			targets->values[0] = slave;
			targets->count = 1;
			return 0;
		}
	return -EINVAL;
}

int omen_fury_parse_slave(const char *text, uint8_t *slave)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 16);
	if (errno || *text == '\0' || *end != '\0' || value > 0xff)
		return -EINVAL;
	if (omen_fury_target_one(&(struct omen_fury_target_set){ 0 },
				 (uint8_t)value))
		return -EINVAL;
	*slave = (uint8_t)value;
	return 0;
}

int omen_fury_parse_rgb(const char *text, uint8_t rgb[3])
{
	char value[7];
	char *end;
	unsigned long parsed;

	if (text[0] == '#')
		++text;
	if (strlen(text) != 6)
		return -EINVAL;
	memcpy(value, text, 7);
	errno = 0;
	parsed = strtoul(value, &end, 16);
	if (errno || *end != '\0' || parsed > 0xffffff)
		return -EINVAL;
	rgb[0] = (parsed >> 16) & 0xff;
	rgb[1] = (parsed >> 8) & 0xff;
	rgb[2] = parsed & 0xff;
	return 0;
}

int omen_fury_default_sleep(void *context, unsigned int milliseconds)
{
	struct timespec requested = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = (long)(milliseconds % 1000) * 1000000L,
	};
	struct timespec remaining;

	(void)context;
	while (nanosleep(&requested, &remaining) < 0) {
		if (errno != EINTR)
			return -errno;
		requested = remaining;
	}
	return 0;
}

int omen_fury_protocol_off(struct omen_fury_protocol *protocol,
			   const struct omen_fury_target_set *targets)
{
	size_t i;
	int result;

	/* Windows begins transfers in reverse DIMM order. */
	for (i = targets->count; i > 0; --i) {
		result = send(protocol, targets->values[i - 1], 0x08, 0x53, 50);
		if (result)
			return result;
	}
	for (i = 0; i < targets->count; ++i) {
		result = send(protocol, targets->values[i], 0x20, 0x00, 5);
		if (result)
			return result;
	}
	/* Windows completes transfers in forward DIMM order. */
	for (i = 0; i < targets->count; ++i) {
		result = send(protocol, targets->values[i], 0x08, 0x44, 50);
		if (result)
			return result;
	}
	return 0;
}

int omen_fury_protocol_static(struct omen_fury_protocol *protocol,
			      const struct omen_fury_target_set *targets,
			      const uint8_t rgb[3], uint8_t brightness)
{
	size_t i;
	int result;

	for (i = targets->count; i > 0; --i) {
		result = send(protocol, targets->values[i - 1], 0x08, 0x53, 50);
		if (result)
			return result;
	}
	for (i = 0; i < targets->count; ++i) {
		const uint8_t slave = targets->values[i];
		const uint8_t writes[][2] = {
			{ 0x09, 0x00 }, { 0x20, brightness }, { 0x30, 0x01 },
			{ 0x31, rgb[0] }, { 0x32, rgb[1] }, { 0x33, rgb[2] },
		};
		size_t step;

		for (step = 0; step < sizeof(writes) / sizeof(writes[0]); ++step) {
			result = send(protocol, slave, writes[step][0], writes[step][1], 5);
			if (result)
				return result;
		}
	}
	for (i = 0; i < targets->count; ++i) {
		result = send(protocol, targets->values[i], 0x08, 0x44, 50);
		if (result)
			return result;
	}
	return 0;
}

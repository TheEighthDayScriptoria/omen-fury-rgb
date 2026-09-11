/* SPDX-License-Identifier: MIT */
#ifndef OMEN_FURY_PROTOCOL_H
#define OMEN_FURY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define OMEN_FURY_MAX_TARGETS 4

struct omen_fury_target_set {
	uint8_t values[OMEN_FURY_MAX_TARGETS];
	size_t count;
};

typedef int (*omen_fury_write_fn)(void *context, uint8_t slave, uint8_t reg,
				  uint8_t value);
typedef int (*omen_fury_sleep_fn)(void *context, unsigned int milliseconds);

struct omen_fury_protocol {
	void *write_context;
	omen_fury_write_fn write;
	void *sleep_context;
	omen_fury_sleep_fn sleep;
};

int omen_fury_targets_all(struct omen_fury_target_set *targets);
int omen_fury_target_one(struct omen_fury_target_set *targets, uint8_t slave);
int omen_fury_parse_slave(const char *text, uint8_t *slave);
int omen_fury_parse_rgb(const char *text, uint8_t rgb[3]);
int omen_fury_default_sleep(void *context, unsigned int milliseconds);
int omen_fury_protocol_off(struct omen_fury_protocol *protocol,
			   const struct omen_fury_target_set *targets);
int omen_fury_protocol_static(struct omen_fury_protocol *protocol,
			      const struct omen_fury_target_set *targets,
			      const uint8_t rgb[3], uint8_t brightness);

#endif

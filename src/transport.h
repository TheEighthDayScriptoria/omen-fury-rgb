/* SPDX-License-Identifier: MIT */
#ifndef OMEN_FURY_TRANSPORT_H
#define OMEN_FURY_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include <linux/omen_fury_wmi.h>

struct omen_fury_transport {
	int fd;
	bool verbose;
	bool dry_run;
	bool session_active;
	const char *device_path;
};

int omen_fury_transport_open(struct omen_fury_transport *transport,
			     const char *device_path, bool verbose, bool dry_run);
void omen_fury_transport_close(struct omen_fury_transport *transport);
int omen_fury_transport_get_caps(struct omen_fury_transport *transport,
				 struct omen_fury_wmi_caps *caps);
int omen_fury_transport_begin(struct omen_fury_transport *transport);
int omen_fury_transport_end(struct omen_fury_transport *transport);
int omen_fury_transport_write(void *context, uint8_t slave, uint8_t reg,
			      uint8_t value);

#endif

/* SPDX-License-Identifier: MIT */
#include "transport.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const uint8_t dry_slaves[] = { 0xc0, 0xc2, 0xc4, 0xc6 };
static const uint8_t dry_registers[] = {
	0x08, 0x09, 0x20, 0x30, 0x31, 0x32, 0x33,
};

int omen_fury_transport_open(struct omen_fury_transport *transport,
			     const char *device_path, bool verbose, bool dry_run)
{
	memset(transport, 0, sizeof(*transport));
	transport->fd = -1;
	transport->verbose = verbose;
	transport->dry_run = dry_run;
	transport->device_path = device_path;
	if (dry_run)
		return 0;

	transport->fd = open(device_path, O_RDWR | O_CLOEXEC);
	return transport->fd < 0 ? -errno : 0;
}

void omen_fury_transport_close(struct omen_fury_transport *transport)
{
	if (transport->fd >= 0)
		close(transport->fd);
	transport->fd = -1;
}

int omen_fury_transport_get_caps(struct omen_fury_transport *transport,
				 struct omen_fury_wmi_caps *caps)
{
	memset(caps, 0, sizeof(*caps));
	if (transport->dry_run) {
		caps->abi_version = OMEN_FURY_WMI_ABI_VERSION;
		caps->struct_size = sizeof(*caps);
		caps->flags = OMEN_FURY_CAP_MLED_WRITE |
			OMEN_FURY_CAP_KERNEL_VALIDATION |
			OMEN_FURY_CAP_SERIALIZED;
		memcpy(caps->slaves, dry_slaves, sizeof(dry_slaves));
		memcpy(caps->registers, dry_registers, sizeof(dry_registers));
		return 0;
	}
	if (ioctl(transport->fd, OMEN_FURY_WMI_GET_CAPS, caps) < 0)
		return -errno;
	if (caps->abi_version != OMEN_FURY_WMI_ABI_VERSION)
		return -EPROTONOSUPPORT;
	return 0;
}

int omen_fury_transport_write(void *context, uint8_t slave, uint8_t reg,
			      uint8_t value)
{
	struct omen_fury_transport *transport = context;
	struct omen_fury_mled_write operation = {
		.slave = slave,
		.reg = reg,
		.value = value,
		.reserved = 0,
	};

	if (transport->verbose || transport->dry_run)
		fprintf(stderr, "%s%02X reg=%02X value=%02X",
			transport->dry_run ? "DRY-RUN " : "",
			slave, reg, value);
	if (transport->dry_run) {
		fputs(" OK\n", stderr);
		return 0;
	}
	if (ioctl(transport->fd, OMEN_FURY_WMI_MLED_WRITE, &operation) < 0) {
		int error = errno;
		if (transport->verbose)
			fprintf(stderr, " FAILED: %s\n", strerror(error));
		return -error;
	}
	if (transport->verbose)
		fputs(" OK\n", stderr);
	return 0;
}

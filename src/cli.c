/* SPDX-License-Identifier: MIT */
#include "protocol.h"
#include "transport.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_DEVICE "/dev/omen-fury-wmi"
#define DEFAULT_BRIGHTNESS 0x20

struct options {
	const char *command;
	const char *positionals[4];
	size_t positional_count;
	const char *device;
	bool verbose;
	bool dry_run;
	bool experimental;
	bool brightness_set;
	uint8_t brightness;
	bool dimm_set;
	uint8_t dimm;
};

static void usage(FILE *stream)
{
	fprintf(stream,
		"Usage:\n"
		"  omen-furyctl [options] info\n"
		"  omen-furyctl [options] off\n"
		"  omen-furyctl [options] static RRGGBB [--brightness 0..255]\n"
		"  omen-furyctl [options] --experimental raw SLAVE REG VALUE\n\n"
		"Options:\n"
		"  --dimm 1..4|A1..B2|HEX target one DIMM (default: all)\n"
		"  --device PATH         character device (default: %s)\n"
		"  --verbose, -v         print every operation and result\n"
		"  --dry-run              print the sequence without opening hardware\n"
		"  --experimental         acknowledge experimental raw operation\n"
		"  --help, -h             show this help\n",
		DEFAULT_DEVICE);
}

static int parse_byte(const char *text, int base, uint8_t *value)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, base);
	if (errno || *text == '\0' || *end != '\0' || parsed > 0xff)
		return -EINVAL;
	*value = (uint8_t)parsed;
	return 0;
}

static int parse_options(int argc, char **argv, struct options *options)
{
	int i;

	memset(options, 0, sizeof(*options));
	options->device = DEFAULT_DEVICE;
	options->brightness = DEFAULT_BRIGHTNESS;
	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) {
			options->verbose = true;
		} else if (!strcmp(argv[i], "--dry-run")) {
			options->dry_run = true;
		} else if (!strcmp(argv[i], "--experimental")) {
			options->experimental = true;
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			usage(stdout);
			exit(0);
		} else if (!strcmp(argv[i], "--device")) {
			if (++i == argc)
				return -EINVAL;
			options->device = argv[i];
		} else if (!strcmp(argv[i], "--brightness")) {
			if (++i == argc || parse_byte(argv[i], 10, &options->brightness))
				return -EINVAL;
			options->brightness_set = true;
		} else if (!strcmp(argv[i], "--dimm")) {
			if (++i == argc || omen_fury_parse_slave(argv[i], &options->dimm))
				return -EINVAL;
			options->dimm_set = true;
		} else if (argv[i][0] == '-') {
			return -EINVAL;
		} else if (!options->command) {
			options->command = argv[i];
		} else if (options->positional_count < 4) {
			options->positionals[options->positional_count++] = argv[i];
		} else {
			return -EINVAL;
		}
	}
	return options->command ? 0 : -EINVAL;
}

static bool byte_in_list(uint8_t value, const uint8_t *list, size_t count)
{
	size_t i;

	for (i = 0; i < count; ++i)
		if (list[i] == value)
			return true;
	return false;
}

static void print_caps(const struct omen_fury_wmi_caps *caps,
		       const char *device)
{
	size_t i;

	printf("HP OMEN / Kingston FURY WMI bridge\n");
	printf("Device: %s\nABI version: %u\nCapabilities:", device,
	       caps->abi_version);
	if (caps->flags & OMEN_FURY_CAP_MLED_WRITE)
		printf(" mled-write");
	if (caps->flags & OMEN_FURY_CAP_KERNEL_VALIDATION)
		printf(" validated");
	if (caps->flags & OMEN_FURY_CAP_SERIALIZED)
		printf(" serialized");
	printf("\nSlaves:");
	for (i = 0; i < OMEN_FURY_SLAVE_COUNT; ++i)
		printf(" %02X", caps->slaves[i]);
	printf("\nRegisters:");
	for (i = 0; i < OMEN_FURY_REGISTER_COUNT; ++i)
		printf(" %02X", caps->registers[i]);
	putchar('\n');
}

int main(int argc, char **argv)
{
	struct omen_fury_transport transport;
	struct omen_fury_wmi_caps caps;
	struct omen_fury_target_set targets;
	struct omen_fury_protocol protocol;
	struct options options;
	uint8_t rgb[3];
	int result;

	if (parse_options(argc, argv, &options)) {
		usage(stderr);
		return 2;
	}
	if (options.dimm_set)
		omen_fury_target_one(&targets, options.dimm);
	else
		omen_fury_targets_all(&targets);

	result = omen_fury_transport_open(&transport, options.device,
					  options.verbose, options.dry_run);
	if (result) {
		fprintf(stderr, "Cannot open %s: %s\n", options.device,
			strerror(-result));
		if (result == -EACCES && getuid() != 0)
			fprintf(stderr, "Hint: try running with sudo or check udev rules.\n");
		else if (result == -ENOENT) {
			fprintf(stderr, "Hint: the omen_fury_wmi kernel module is not loaded.\n");
			if (access("/sys/bus/wmi/devices/5FB7F034-2C63-45E9-BE91-3D44E2C707E4-0", F_OK) != 0 &&
			    access("/sys/bus/wmi/devices/5FB7F034-2C63-45E9-BE91-3D44E2C707E4-1", F_OK) != 0) {
				fprintf(stderr, "Error: this system does not expose the HP OMEN WMI BIOS interface.\n"
						"This software requires an HP OMEN desktop to function.\n");
			}
		}
		return 1;
	}
	result = omen_fury_transport_get_caps(&transport, &caps);
	if (result) {
		fprintf(stderr, "Cannot query bridge capabilities: %s\n",
			strerror(-result));
		goto out;
	}

	if (!strcmp(options.command, "info")) {
		if (options.positional_count || options.brightness_set) {
			result = -EINVAL;
			goto bad_usage;
		}
		print_caps(&caps, options.device);
		result = 0;
		goto out;
	}

	protocol.write_context = &transport;
	protocol.write = omen_fury_transport_write;
	protocol.sleep_context = NULL;
	protocol.sleep = omen_fury_default_sleep;

	if (!strcmp(options.command, "off")) {
		if (options.positional_count || options.brightness_set) {
			result = -EINVAL;
			goto bad_usage;
		}
		result = omen_fury_protocol_off(&protocol, &targets);
	} else if (!strcmp(options.command, "static")) {
		if (options.positional_count != 1 ||
		    omen_fury_parse_rgb(options.positionals[0], rgb)) {
			result = -EINVAL;
			goto bad_usage;
		}
		result = omen_fury_protocol_static(&protocol, &targets, rgb,
						 options.brightness);
	} else if (!strcmp(options.command, "raw")) {
		uint8_t slave, reg, value;

		if (!options.experimental || options.positional_count != 3 ||
		    omen_fury_parse_slave(options.positionals[0], &slave) ||
		    parse_byte(options.positionals[1], 16, &reg) ||
		    parse_byte(options.positionals[2], 16, &value)) {
			result = -EINVAL;
			goto bad_usage;
		}
		if (!byte_in_list(reg, caps.registers, OMEN_FURY_REGISTER_COUNT)) {
			fprintf(stderr, "Register %02X is not in the bridge whitelist\n", reg);
			result = -EINVAL;
			goto out;
		}
		result = omen_fury_transport_write(&transport, slave, reg, value);
	} else {
		result = -EINVAL;
		goto bad_usage;
	}

	if (result)
		fprintf(stderr, "Operation failed: %s\n", strerror(-result));
	goto out;

bad_usage:
	usage(stderr);
out:
	omen_fury_transport_close(&transport);
	return result ? (result == -EINVAL ? 2 : 1) : 0;
}

/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_OMEN_FURY_WMI_H
#define _UAPI_LINUX_OMEN_FURY_WMI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define OMEN_FURY_WMI_ABI_VERSION 1

#define OMEN_FURY_CAP_MLED_WRITE       (1U << 0)
#define OMEN_FURY_CAP_KERNEL_VALIDATION (1U << 1)
#define OMEN_FURY_CAP_SERIALIZED       (1U << 2)

#define OMEN_FURY_SLAVE_COUNT 4
#define OMEN_FURY_REGISTER_COUNT 7

struct omen_fury_wmi_caps {
	__u16 abi_version;
	__u16 struct_size;
	__u32 flags;
	__u8 slaves[OMEN_FURY_SLAVE_COUNT];
	__u8 registers[OMEN_FURY_REGISTER_COUNT];
	__u8 reserved[5];
};

struct omen_fury_mled_write {
	__u8 slave;
	__u8 reg;
	__u8 value;
	__u8 reserved;
};

#define OMEN_FURY_WMI_IOC_MAGIC 0xF5
#define OMEN_FURY_WMI_GET_CAPS \
	_IOR(OMEN_FURY_WMI_IOC_MAGIC, 0x00, struct omen_fury_wmi_caps)
#define OMEN_FURY_WMI_MLED_WRITE \
	_IOW(OMEN_FURY_WMI_IOC_MAGIC, 0x01, struct omen_fury_mled_write)

#endif /* _UAPI_LINUX_OMEN_FURY_WMI_H */

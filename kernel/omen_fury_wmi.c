// SPDX-License-Identifier: GPL-2.0-only
/*
 * Experimental, constrained HP OMEN / Kingston FURY MLED WMI bridge.
 *
 * This deliberately is not a general HP WMI call interface. Userspace can
 * only write one byte to one validated MLED register on one validated DIMM.
 */
#include <linux/acpi.h>
#include <linux/compat.h>
#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wmi.h>

#include <linux/omen_fury_wmi.h>

#define DRIVER_NAME "omen-fury-wmi"
#define HP_WMI_BIOS_GUID "5FB7F034-2C63-45E9-BE91-3D44E2C707E4"
#define HP_SIGNATURE 0x55434553U
#define HP_MLED_COMMAND 0x00020009U
#define HP_MLED_COMMAND_TYPE 0x0000000aU
#define HP_WMI_METHOD_ID 2
#define HP_DATA_CAPACITY 128
#define HP_PASS_SIGNATURE 0x53534150U

static const struct dmi_system_id omen_fury_dmi_table[] = {
	{
		.ident = "HP OMEN Desktop",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "HP"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, omen_fury_dmi_table);

struct hp_bios_args {
	u32 signature;
	u32 command;
	u32 command_type;
	u32 data_size;
	u8 data[HP_DATA_CAPACITY];
} __packed;

struct hp_bios_return {
	u32 signature;
	u32 return_code;
} __packed;

static DEFINE_MUTEX(omen_fury_wmi_lock);

static const u8 allowed_slaves[OMEN_FURY_SLAVE_COUNT] = {
	0xc0, 0xc2, 0xc4, 0xc6,
};

static const u8 allowed_registers[OMEN_FURY_REGISTER_COUNT] = {
	0x08, 0x09, 0x20, 0x30, 0x31, 0x32, 0x33,
};

static bool byte_in_set(u8 value, const u8 *set, size_t count)
{
	size_t i;

	for (i = 0; i < count; ++i)
		if (set[i] == value)
			return true;
	return false;
}

static int omen_fury_mled_write(const struct omen_fury_mled_write *op)
{
	struct hp_bios_args *args;
	struct acpi_buffer input;
	struct acpi_buffer output = {
		.length = ACPI_ALLOCATE_BUFFER,
		.pointer = NULL,
	};
	union acpi_object *object;
	struct hp_bios_return *result;
	acpi_status status;
	int ret = 0;

	if (op->reserved)
		return -EINVAL;
	if (!byte_in_set(op->slave, allowed_slaves, ARRAY_SIZE(allowed_slaves)))
		return -EINVAL;
	if (!byte_in_set(op->reg, allowed_registers,
			 ARRAY_SIZE(allowed_registers)))
		return -EINVAL;

	args = kzalloc_obj(*args, GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	args->signature = HP_SIGNATURE;
	args->command = HP_MLED_COMMAND;
	args->command_type = HP_MLED_COMMAND_TYPE;
	/* HP's logical three-byte operation must be padded and declared as four. */
	args->data_size = 4;
	args->data[0] = op->reg;
	args->data[1] = op->value;
	args->data[2] = op->slave;
	args->data[3] = 0;
	input.length = sizeof(*args);
	input.pointer = args;

	status = wmi_evaluate_method(HP_WMI_BIOS_GUID, 0, HP_WMI_METHOD_ID,
				     &input, &output);
	if (ACPI_FAILURE(status)) {
		pr_err_ratelimited(DRIVER_NAME ": WMI evaluation failed: %s\n",
				   acpi_format_exception(status));
		ret = -EIO;
		goto out_args;
	}

	object = output.pointer;
	if (!object) {
		ret = -ENODATA;
		goto out_args;
	}
	if (object->type != ACPI_TYPE_BUFFER ||
	    object->buffer.length < sizeof(*result)) {
		ret = -EPROTO;
		goto out_object;
	}

	result = (struct hp_bios_return *)object->buffer.pointer;
	if (result->signature != HP_PASS_SIGNATURE) {
		pr_err_ratelimited(DRIVER_NAME ": invalid firmware response signature\n");
		ret = -EPROTO;
	} else if (result->return_code) {
		pr_err_ratelimited(DRIVER_NAME ": firmware returned error %#x\n",
				   result->return_code);
		ret = -EREMOTEIO;
	}

out_object:
	kfree(object);
out_args:
	kfree(args);
	return ret;
}

static long omen_fury_wmi_ioctl(struct file *file, unsigned int command,
				unsigned long argument)
{
	void __user *user_arg = (void __user *)argument;
	struct omen_fury_wmi_caps caps = {
		.abi_version = OMEN_FURY_WMI_ABI_VERSION,
		.struct_size = sizeof(caps),
		.flags = OMEN_FURY_CAP_MLED_WRITE |
			 OMEN_FURY_CAP_KERNEL_VALIDATION |
			 OMEN_FURY_CAP_SERIALIZED,
	};
	struct omen_fury_mled_write op;
	int ret;

	switch (command) {
	case OMEN_FURY_WMI_GET_CAPS:
		memcpy(caps.slaves, allowed_slaves, sizeof(caps.slaves));
		memcpy(caps.registers, allowed_registers, sizeof(caps.registers));
		return copy_to_user(user_arg, &caps, sizeof(caps)) ? -EFAULT : 0;
	case OMEN_FURY_WMI_MLED_WRITE:
		if (copy_from_user(&op, user_arg, sizeof(op)))
			return -EFAULT;
		ret = mutex_lock_interruptible(&omen_fury_wmi_lock);
		if (ret)
			return ret;
		ret = omen_fury_mled_write(&op);
		mutex_unlock(&omen_fury_wmi_lock);
		return ret;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations omen_fury_wmi_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = omen_fury_wmi_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
};

static struct miscdevice omen_fury_wmi_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRIVER_NAME,
	.fops = &omen_fury_wmi_fops,
	.mode = 0600,
};

static int __init omen_fury_wmi_init(void)
{
	if (!dmi_check_system(omen_fury_dmi_table))
		return -ENODEV;

	if (!wmi_has_guid(HP_WMI_BIOS_GUID))
		return -ENODEV;

	return misc_register(&omen_fury_wmi_device);
}

static void __exit omen_fury_wmi_exit(void)
{
	misc_deregister(&omen_fury_wmi_device);
}

module_init(omen_fury_wmi_init);
module_exit(omen_fury_wmi_exit);

MODULE_AUTHOR("omen-fury-linux contributors");
MODULE_DESCRIPTION("Experimental constrained HP OMEN FURY MLED WMI bridge");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");

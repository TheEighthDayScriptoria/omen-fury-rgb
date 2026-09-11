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
#include <linux/uaccess.h>
#include <linux/wait.h>
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
		.ident = "HP OMEN 45L GT22 (current naming)",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME,
				  "OMEN by HP 45L Gaming Desktop GT22-"),
		},
	},
	{
		.ident = "HP OMEN 45L GT22 (legacy naming)",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME,
				  "OMEN 45L Gaming Desktop GT22-"),
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

static bool allow_unsupported;
module_param(allow_unsupported, bool, 0444);
MODULE_PARM_DESC(allow_unsupported,
		 "Allow loading on an unlisted DMI product (dangerous)");

/* Serializes each firmware call and session ownership transitions. */
static DEFINE_MUTEX(omen_fury_call_lock);
static DEFINE_MUTEX(omen_fury_session_lock);
static DECLARE_WAIT_QUEUE_HEAD(omen_fury_session_wait);
static struct file *omen_fury_session_owner;

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
	struct hp_bios_args args = { };
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

	args.signature = HP_SIGNATURE;
	args.command = HP_MLED_COMMAND;
	args.command_type = HP_MLED_COMMAND_TYPE;
	/* HP's logical three-byte operation must be padded and declared as four. */
	args.data_size = 4;
	args.data[0] = op->reg;
	args.data[1] = op->value;
	args.data[2] = op->slave;
	args.data[3] = 0;
	input.length = sizeof(args);
	input.pointer = &args;

	status = wmi_evaluate_method(HP_WMI_BIOS_GUID, 0, HP_WMI_METHOD_ID,
				     &input, &output);
	if (ACPI_FAILURE(status)) {
		pr_err_ratelimited(DRIVER_NAME ": WMI evaluation failed: %s\n",
				   acpi_format_exception(status));
		return -EIO;
	}

	object = output.pointer;
	if (!object)
		return -ENODATA;
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
	ACPI_FREE(object);
	return ret;
}

static int omen_fury_begin_session(struct file *file)
{
	int ret;

	for (;;) {
		ret = mutex_lock_interruptible(&omen_fury_call_lock);
		if (ret)
			return ret;

		mutex_lock(&omen_fury_session_lock);
		if (!omen_fury_session_owner) {
			omen_fury_session_owner = file;
			mutex_unlock(&omen_fury_session_lock);
			mutex_unlock(&omen_fury_call_lock);
			return 0;
		}
		if (omen_fury_session_owner == file) {
			mutex_unlock(&omen_fury_session_lock);
			mutex_unlock(&omen_fury_call_lock);
			return -EALREADY;
		}
		mutex_unlock(&omen_fury_session_lock);
		mutex_unlock(&omen_fury_call_lock);

		ret = wait_event_interruptible(omen_fury_session_wait,
					       !READ_ONCE(omen_fury_session_owner));
		if (ret)
			return ret;
	}
}

static int omen_fury_end_session(struct file *file)
{
	int ret = 0;

	mutex_lock(&omen_fury_call_lock);
	mutex_lock(&omen_fury_session_lock);
	if (omen_fury_session_owner != file)
		ret = -EPERM;
	else
		omen_fury_session_owner = NULL;
	mutex_unlock(&omen_fury_session_lock);
	mutex_unlock(&omen_fury_call_lock);
	if (!ret)
		wake_up_all(&omen_fury_session_wait);
	return ret;
}

static int omen_fury_release(struct inode *inode, struct file *file)
{
	(void)inode;
	mutex_lock(&omen_fury_session_lock);
	if (omen_fury_session_owner == file) {
		omen_fury_session_owner = NULL;
		mutex_unlock(&omen_fury_session_lock);
		wake_up_all(&omen_fury_session_wait);
	} else {
		mutex_unlock(&omen_fury_session_lock);
	}
	return 0;
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
			 OMEN_FURY_CAP_SERIALIZED |
			 OMEN_FURY_CAP_EXCLUSIVE_SESSION,
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
		ret = mutex_lock_interruptible(&omen_fury_call_lock);
		if (ret)
			return ret;
		mutex_lock(&omen_fury_session_lock);
		if (omen_fury_session_owner && omen_fury_session_owner != file) {
			mutex_unlock(&omen_fury_session_lock);
			mutex_unlock(&omen_fury_call_lock);
			return -EBUSY;
		}
		mutex_unlock(&omen_fury_session_lock);
		ret = omen_fury_mled_write(&op);
		mutex_unlock(&omen_fury_call_lock);
		return ret;
	case OMEN_FURY_WMI_BEGIN_SESSION:
		return omen_fury_begin_session(file);
	case OMEN_FURY_WMI_END_SESSION:
		return omen_fury_end_session(file);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations omen_fury_wmi_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = omen_fury_wmi_ioctl,
	.release = omen_fury_release,
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
	if (!dmi_check_system(omen_fury_dmi_table)) {
		if (!allow_unsupported) {
			pr_err(DRIVER_NAME ": unsupported DMI product; refusing to load\n");
			return -ENODEV;
		}
		pr_warn(DRIVER_NAME ": DMI safety check overridden\n");
	}

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

MODULE_AUTHOR("omen-fury-rgb contributors");
MODULE_DESCRIPTION("Experimental constrained HP OMEN FURY MLED WMI bridge");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2.0");

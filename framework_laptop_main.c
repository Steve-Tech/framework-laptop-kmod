// SPDX-License-Identifier: GPL-2.0+
/*
 * Framework Laptop Platform Driver
 *
 * Copyright (C) 2022 Dustin L. Howett
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/power_supply.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/version.h>

#include <acpi/battery.h>

#include "framework_laptop.h"

static struct platform_device *fwdevice;
static struct device *ec_device;

static DEVICE_ATTR_RO(framework_privacy);

static struct attribute *framework_laptop_attrs[] = {
	&dev_attr_framework_privacy.attr,
	NULL,
};

ATTRIBUTE_GROUPS(framework_laptop);

static const struct acpi_device_id device_ids[] = {
	{ "FRMW0001", 0 },
	{ "FRMW0004", 0 },
	{ "", 0 },
};
MODULE_DEVICE_TABLE(acpi, device_ids);

static const struct dmi_system_id framework_laptop_dmi_table[] __initconst = {
	{
		/* the Framework Laptop */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Framework"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Laptop"),
		},
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(dmi, framework_laptop_dmi_table);

static int device_match_cros_ec(struct device *dev, const void *foo)
{
	const char *name = dev_name(dev);
	if (strncmp(name, "cros-ec-dev", 11))
		return 0;
	return 1;
}

static int framework_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct framework_data *data;
	int ret = 0;

	dev = &pdev->dev;

	ec_device = bus_find_device(&platform_bus_type, NULL, NULL,
				    device_match_cros_ec);
	if (!ec_device) {
		dev_err(dev, DRV_NAME ": failed to find EC %s.\n",
			FRAMEWORK_LAPTOP_EC_DEVICE_NAME);
		return -EINVAL;
	}
	ec_device = ec_device->parent;
	data->ec_device = ec_device;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);
	data->pdev = pdev;

#if 0
	/* Register the driver */
	ret = platform_driver_register(&cros_ec_lpc_driver);
	if (ret) {
		pr_err(DRV_NAME ": can't register driver: %d\n", ret);
		return ret;
	}

	/* Register the device, and it'll get hooked up automatically */
	ret = platform_device_register(&cros_ec_lpc_device);
	if (ret) {
		pr_err(DRV_NAME ": can't register device: %d\n", ret);
		platform_driver_unregister(&cros_ec_lpc_driver);
	}
#endif

	fw_battery_register(data);
	fw_leds_register(data);
	fw_color_leds_register(data);
	fw_hwmon_register(data);

	return ret;
}

static int framework_remove(struct platform_device *pdev)
{
	struct framework_data *data;

	data = (struct framework_data *)platform_get_drvdata(pdev);

	// Make sure they're not null before we try to unregister it
	if (data) {
		fw_hwmon_unregister(data);
		fw_color_leds_unregister(data);
		fw_leds_unregister(data);
		fw_battery_unregister(data);
	}

	put_device(ec_device);

	return 0;
}

static struct platform_driver framework_driver = {
	.driver = {
		.name = DRV_NAME,
		.acpi_match_table = device_ids,
		.dev_groups = framework_laptop_groups,
	},
	.probe = framework_probe,
	.remove = framework_remove,
};

static int __init framework_laptop_init(void)
{
	int ret;

	if (!dmi_check_system(framework_laptop_dmi_table)) {
		pr_err(DRV_NAME ": unsupported system.\n");
		return -ENODEV;
	}

	ret = platform_driver_register(&framework_driver);
	if (ret)
		goto fail;

	fwdevice = platform_device_alloc(DRV_NAME, PLATFORM_DEVID_NONE);
	if (!fwdevice) {
		ret = -ENOMEM;
		goto fail_platform_driver;
	}

	ret = platform_device_add(fwdevice);
	if (ret)
		goto fail_device_add;

	return 0;

fail_device_add:
	platform_device_del(fwdevice);
	fwdevice = NULL;

fail_platform_driver:
	platform_driver_unregister(&framework_driver);

fail:
	return ret;
}

static void __exit framework_laptop_exit(void)
{
	if (fwdevice) {
		platform_device_unregister(fwdevice);
		platform_driver_unregister(&framework_driver);
	}
}

module_init(framework_laptop_init);
module_exit(framework_laptop_exit);

MODULE_DESCRIPTION("Framework Laptop Platform Driver");
MODULE_AUTHOR("Dustin L. Howett <dustin@howett.net>");
MODULE_AUTHOR("Stephen Horvath <stephen@horvath.au>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_SOFTDEP("pre: cros_ec_lpcs");

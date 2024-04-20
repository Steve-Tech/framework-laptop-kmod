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

#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/version.h>

#include <acpi/battery.h>

#include "framework_laptop.h"

static struct device *ec_device;

#define EC_CMD_CHARGE_LIMIT_CONTROL 0x3E03

enum ec_chg_limit_control_modes {
	/* Disable all setting, charge control by charge_manage */
	CHG_LIMIT_DISABLE	= BIT(0),
	/* Set maximum and minimum percentage */
	CHG_LIMIT_SET_LIMIT	= BIT(1),
	/* Host read current setting */
	CHG_LIMIT_GET_LIMIT	= BIT(3),
	/* Enable override mode, allow charge to full this time */
	CHG_LIMIT_OVERRIDE	= BIT(7),
};

struct ec_params_ec_chg_limit_control {
	/* See enum ec_chg_limit_control_modes */
	uint8_t modes;
	uint8_t max_percentage;
	uint8_t min_percentage;
} __ec_align1;

struct ec_response_chg_limit_control {
	uint8_t max_percentage;
	uint8_t min_percentage;
} __ec_align1;

static int charge_limit_control(enum ec_chg_limit_control_modes modes, uint8_t max_percentage) {
	struct {
		struct cros_ec_command msg;
		union {
			struct ec_params_ec_chg_limit_control params;
			struct ec_response_chg_limit_control resp;
		};
	} __packed buf;
	struct ec_params_ec_chg_limit_control *params = &buf.params;
	struct ec_response_chg_limit_control *resp = &buf.resp;
	struct cros_ec_command *msg = &buf.msg;
	struct cros_ec_device *ec;
	int ret;

	if (!ec_device)
		return -ENODEV;

	ec = dev_get_drvdata(ec_device);

	memset(&buf, 0, sizeof(buf));

	msg->version = 0;
	msg->command = EC_CMD_CHARGE_LIMIT_CONTROL;
	msg->outsize = sizeof(*params);
	msg->insize = sizeof(*resp);

	params->modes = modes;
	params->max_percentage = max_percentage;

	ret = cros_ec_cmd_xfer_status(ec, msg);
	if (ret < 0) {
		return -EIO;
	}

	return resp->max_percentage;
}


static ssize_t battery_get_threshold(char *buf)
{
	int ret;

	ret = charge_limit_control(CHG_LIMIT_GET_LIMIT, 0);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", (int)ret);
}

static ssize_t battery_set_threshold(const char *buf, size_t count)
{
	int ret;
	int value;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	if (value > 100)
		return -EINVAL;

	ret = charge_limit_control(CHG_LIMIT_SET_LIMIT, (uint8_t)value);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t charge_control_end_threshold_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return battery_get_threshold(buf);
}

static ssize_t charge_control_end_threshold_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	return battery_set_threshold(buf, count);
}

static DEVICE_ATTR_RW(charge_control_end_threshold);

static struct attribute *framework_laptop_battery_attrs[] = {
	&dev_attr_charge_control_end_threshold.attr,
	NULL,
};

ATTRIBUTE_GROUPS(framework_laptop_battery);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static int framework_laptop_battery_add(struct power_supply *battery, struct acpi_battery_hook *hook)
#else
static int framework_laptop_battery_add(struct power_supply *battery)
#endif
{
	// Framework EC only supports 1 battery
	if (strcmp(battery->desc->name, "BAT1") != 0)
		return -ENODEV;

	if (device_add_groups(&battery->dev, framework_laptop_battery_groups))
		return -ENODEV;

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static int framework_laptop_battery_remove(struct power_supply *battery, struct acpi_battery_hook *hook)
#else
static int framework_laptop_battery_remove(struct power_supply *battery)
#endif
{
	device_remove_groups(&battery->dev, framework_laptop_battery_groups);
	return 0;
}

static struct acpi_battery_hook framework_laptop_battery_hook = {
	.add_battery = framework_laptop_battery_add,
	.remove_battery = framework_laptop_battery_remove,
	.name = "Framework Laptop Battery Extension",
};


int fw_battery_register(struct framework_data *data)
{
	ec_device = data->ec_device;
	
	battery_hook_register(&framework_laptop_battery_hook);
	
	return 0;
}

void fw_battery_unregister(struct framework_data *data)
{
	battery_hook_unregister(&framework_laptop_battery_hook);
}
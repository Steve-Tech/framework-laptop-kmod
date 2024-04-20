// SPDX-License-Identifier: GPL-2.0+
/*
 * Framework Laptop Platform Driver
 *
 * Copyright (C) 2022 Dustin L. Howett
 * Copyright (C) 2024 Stephen Horvath
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
#include <linux/sysfs.h>
#include <linux/types.h>

#include "framework_laptop.h"

static struct device *ec_device;

#define EC_CMD_PRIVACY_SWITCHES_CHECK_MODE 0x3E14

struct ec_response_privacy_switches_check {
	uint8_t microphone;
	uint8_t camera;
} __ec_align1;

// --- framework_privacy ---
ssize_t framework_privacy_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int ret;
	if (!ec_device)
		return -ENODEV;

	struct cros_ec_device *ec = dev_get_drvdata(ec_device);

	struct ec_response_privacy_switches_check resp;

	ret = cros_ec_cmd(ec, 0, EC_CMD_PRIVACY_SWITCHES_CHECK_MODE, NULL, 0,
			  &resp, sizeof(resp));
	if (ret < 0)
		return -EIO;

	// Output following dell-privacy's format
	return sysfs_emit(buf, "[Microphone] [%s]\n[Camera] [%s]\n",
			  resp.microphone ? "unmuted" : "muted",
			  resp.camera ? "unmuted" : "muted");
}


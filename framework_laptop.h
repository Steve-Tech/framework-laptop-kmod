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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#define DRV_NAME "framework_laptop"
#define FRAMEWORK_LAPTOP_EC_DEVICE_NAME "cros-ec-dev"

struct framework_led {
	enum ec_led_id id;
	enum ec_led_colors color;
	struct led_classdev led;
	struct framework_led *others;
};

struct framework_data {
	struct platform_device *pdev;
	struct device *ec_device;
	struct led_classdev kb_led;
	struct led_classdev fp_led;
	struct framework_led batt_led[EC_LED_COLOR_COUNT];
	struct device *hwmon_dev;
};

int fw_hwmon_register(struct framework_data *data);
void fw_hwmon_unregister(struct framework_data *data);

int fw_color_leds_register(struct framework_data *data);
void fw_color_leds_unregister(struct framework_data *data);

int fw_leds_register(struct framework_data *data);
void fw_leds_unregister(struct framework_data *data);

int fw_battery_register(struct framework_data *data);
void fw_battery_unregister(struct framework_data *data);

/* SysFS attributes */
ssize_t framework_privacy_show(struct device *dev, struct device_attribute *attr, char *buf);
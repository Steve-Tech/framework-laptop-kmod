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

// Set the LED brightness
static int ec_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct cros_ec_device *ec;
	int ret;

	struct framework_led *fw_led =
		container_of(led, struct framework_led, led);


	struct ec_params_led_control params = {
		.led_id = fw_led->id,
		.flags = 0
	};

	memset(params.brightness, 0, sizeof(params.brightness));
	params.brightness[fw_led->color] = value;

	struct ec_response_led_control resp;

	if (!ec_device)
		return -EIO;

	if (led->trigger) {
		led_trigger_set(led, NULL);
	}

	ec = dev_get_drvdata(ec_device);

	ret = cros_ec_cmd(ec, 1, EC_CMD_LED_CONTROL, &params, sizeof(params),
			  &resp, sizeof(resp));
	if (ret < 0) {
		return -EIO;
	}

	return 0;
}

// Query the max LED brightness
static int ec_led_max(struct led_classdev *led)
{
	struct cros_ec_device *ec;
	int ret;

	struct framework_led *fw_led =
		container_of(led, struct framework_led, led);

	struct ec_params_led_control params = { .led_id = fw_led->id,
						.flags = EC_LED_FLAGS_QUERY };

	struct ec_response_led_control resp;

	if (!ec_device)
		return -EIO;

	ec = dev_get_drvdata(ec_device);

	ret = cros_ec_cmd(ec, 1, EC_CMD_LED_CONTROL, &params, sizeof(params),
			  &resp, sizeof(resp));
	if (ret < 0) {
		return -EIO;
	}

	return resp.brightness_range[fw_led->color];

	return 0;
}

static struct led_hw_trigger_type framework_hw_trigger_type;

static int ec_trig_activate(struct led_classdev *led);
static void ec_trig_deactivate(struct led_classdev *led);
static struct led_trigger framework_led_trigger = {
	.name = DRV_NAME,
	.activate = ec_trig_activate,
	.deactivate = ec_trig_deactivate,
	.trigger_type = &framework_hw_trigger_type
};

static int ec_trig_activate(struct led_classdev *led)
{
	struct cros_ec_device *ec;
	int ret;

	struct framework_led *fw_led =
		container_of(led, struct framework_led, led);

	struct ec_params_led_control params = { .led_id = fw_led->id,
						.flags = EC_LED_FLAGS_AUTO };

	struct ec_response_led_control resp;

	if (!ec_device)
		return -EIO;

	ec = dev_get_drvdata(ec_device);

	ret = cros_ec_cmd(ec, 1, EC_CMD_LED_CONTROL, &params, sizeof(params),
			  &resp, sizeof(resp));
	if (ret < 0) {
		return -EIO;
	}

	// Unset the trigger functions, so we don't get a loop
	framework_led_trigger.activate = NULL;
	framework_led_trigger.deactivate = NULL;

	for (int i = 0; i < EC_LED_COLOR_COUNT; i++) {
		struct framework_led *other = &fw_led->others[i];
		struct led_classdev *other_led = &other->led;

		if (other_led != led)
			led_trigger_set(other_led, &framework_led_trigger);
	}

	// Reset the trigger functions
	framework_led_trigger.activate = ec_trig_activate;
	framework_led_trigger.deactivate = ec_trig_deactivate;

	return 0;
}

static void ec_trig_deactivate(struct led_classdev *led)
{
	struct framework_led *fw_led =
		container_of(led, struct framework_led, led);

	// Unset the trigger functions, so we don't get a loop
	framework_led_trigger.activate = NULL;
	framework_led_trigger.deactivate = NULL;

	for (int i = 0; i < EC_LED_COLOR_COUNT; i++) {
		struct framework_led *other = &fw_led->others[i];
		struct led_classdev *other_led = &other->led;

		if (other_led != led)
			led_trigger_set(other_led, NULL);
	}

	// Reset the trigger functions
	framework_led_trigger.activate = ec_trig_activate;
	framework_led_trigger.deactivate = ec_trig_deactivate;
}

int fw_color_leds_register(struct framework_data *data)
{
	int ret;
	
	struct device *dev = &data->pdev->dev;

	ec_device = data->ec_device;

	ret = devm_led_trigger_register(dev, &framework_led_trigger);
	if (ret)
		return ret;

	const char *batt_led_names[EC_LED_COLOR_COUNT] = {
		DRV_NAME ":red:" LED_FUNCTION_INDICATOR,   DRV_NAME ":green:" LED_FUNCTION_INDICATOR,
		DRV_NAME ":blue:" LED_FUNCTION_INDICATOR,  DRV_NAME ":yellow:" LED_FUNCTION_INDICATOR,
		DRV_NAME ":white:" LED_FUNCTION_INDICATOR, DRV_NAME ":amber:" LED_FUNCTION_INDICATOR,
	};

	for (uint i = 0; i < EC_LED_COLOR_COUNT; i++) {
		data->batt_led[i].others = data->batt_led;
		data->batt_led[i].id = EC_LED_ID_BATTERY_LED;
		data->batt_led[i].color = i;
		data->batt_led[i].led.name = batt_led_names[i];
		data->fp_led.brightness_get = NULL;
		data->batt_led[i].led.brightness_set_blocking = ec_led_set;
		data->batt_led[i].led.max_brightness =
			ec_led_max(&data->batt_led[i].led);

		if (data->batt_led[i].led.max_brightness <= 0)
			break;

		data->batt_led[i].led.trigger_type = &framework_hw_trigger_type;

		ret = devm_led_classdev_register(
			dev, &data->batt_led[i].led);
		if (ret)
			return ret;
	}

	// Set trigger
	// Why aren't I using default_trigger? Because that will run for every color, instead of once for all of them
	ret = led_trigger_set(&data->batt_led[0].led, &framework_led_trigger);
	if (ret)
		return ret;

	return 0;
}

void fw_color_leds_unregister(struct framework_data *data)
{
	for (uint i = 0; i < EC_LED_COLOR_COUNT; i++) {
		led_classdev_unregister(&data->batt_led[i].led);
	}

	led_trigger_unregister(&framework_led_trigger);
}
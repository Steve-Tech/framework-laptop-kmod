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
#include <linux/types.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include "framework_laptop.h"

static struct device *ec_device;

/* Get the current keyboard LED brightness */
static enum led_brightness kb_led_get(struct led_classdev *led)
{
	struct cros_ec_device *ec;

	int ret;

	if (!ec_device)
		return -EIO;

	ec = dev_get_drvdata(ec_device);

	struct ec_response_pwm_get_keyboard_backlight resp;

	ret = cros_ec_cmd(ec, 0, EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT, NULL, 0,
			  &resp, sizeof(resp));
	if (ret < 0) {
		return -EIO;
	}

	return resp.percent;
}

/* Set the keyboard LED brightness */
static int kb_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct cros_ec_device *ec;

	int ret;

	if (!ec_device)
		return -EIO;

	ec = dev_get_drvdata(ec_device);

	struct ec_params_pwm_set_keyboard_backlight params = {
		.percent = value,
	};

	ret = cros_ec_cmd(ec, 0, EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT, &params,
			  sizeof(params), NULL, 0);
	if (ret < 0) {
		return -EIO;
	}

	return 0;
}

#define EC_CMD_FP_LED_LEVEL_CONTROL 0x3E0E

struct ec_params_fp_led_control {
	uint8_t set_led_level;
	uint8_t get_led_level;
} __ec_align1;

enum fp_led_brightness_level {
	FP_LED_BRIGHTNESS_HIGH = 0,
	FP_LED_BRIGHTNESS_MEDIUM = 1,
	FP_LED_BRIGHTNESS_LOW = 2,
};

struct ec_response_fp_led_level {
	uint8_t level;
} __ec_align1;

/* Get the fingerprint LED brightness */
static enum led_brightness fp_led_get(struct led_classdev *led)
{
	struct cros_ec_device *ec;
	int ret;

	struct ec_params_fp_led_control params = {
		.set_led_level = 0,
		.get_led_level = 1,
	};

	struct ec_response_fp_led_level resp;

	if (!ec_device)
		goto out;

	ec = dev_get_drvdata(ec_device);

	ret = cros_ec_cmd(ec, 0, EC_CMD_FP_LED_LEVEL_CONTROL, &params,
			  sizeof(params), &resp, sizeof(resp));

	if (ret < 0) {
		goto out;
	}

	return resp.level;

out:
	return 0;
}

/* Set the fingerprint LED brightness */
static int fp_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct cros_ec_device *ec;
	int ret;

	struct ec_params_fp_led_control params = {
		.set_led_level = FP_LED_BRIGHTNESS_LOW - value,
		.get_led_level = 0,
	};

	struct ec_response_fp_led_level resp;

	if (!ec_device)
		return -EIO;

	ec = dev_get_drvdata(ec_device);

	ret = cros_ec_cmd(ec, 0, EC_CMD_FP_LED_LEVEL_CONTROL, &params,
			  sizeof(params), &resp, sizeof(resp));
	if (ret < 0) {
		return -EIO;
	}

	return 0;
}

int fw_leds_register(struct framework_data *data)
{
	int ret;

	struct device *dev = &data->pdev->dev;
	
	ec_device = data->ec_device;

	data->kb_led.name = DRV_NAME "::kbd_backlight";
	data->kb_led.brightness_get = kb_led_get;
	data->kb_led.brightness_set_blocking = kb_led_set;
	data->kb_led.max_brightness = 100;

	ret = devm_led_classdev_register(dev, &data->kb_led);
	if (ret)
		goto kb_error;

	/* "fingerprint" is a non-standard name, but this behaves weird anyway */
	data->fp_led.name = DRV_NAME "::fingerprint";
	data->fp_led.brightness_get = fp_led_get;
	data->fp_led.brightness_set_blocking = fp_led_set;
	data->fp_led.max_brightness = 2;

	ret = devm_led_classdev_register(dev, &data->fp_led);
	if (ret)
		goto fp_error;

	return 0;

fp_error:
	devm_led_classdev_unregister(dev, &data->fp_led);
kb_error:
	devm_led_classdev_unregister(dev, &data->kb_led);

	return ret;
}

void fw_leds_unregister(struct framework_data *data)
{
	struct device *dev = &data->pdev->dev;
	devm_led_classdev_unregister(dev, &data->fp_led);
	devm_led_classdev_unregister(dev, &data->kb_led);
}
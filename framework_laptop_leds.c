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

// Get the last set keyboard LED brightness
static enum led_brightness kb_led_get(struct led_classdev *led)
{
	struct {
		struct cros_ec_command msg;
		union {
			struct ec_params_pwm_get_duty p;
			struct ec_response_pwm_get_duty resp;
		};
	} __packed buf;

	struct ec_params_pwm_get_duty *p = &buf.p;
	struct ec_response_pwm_get_duty *resp = &buf.resp;
	struct cros_ec_command *msg = &buf.msg;
	struct cros_ec_device *ec;
	int ret;
	if (!ec_device)
		goto out;

	ec = dev_get_drvdata(ec_device);

	memset(&buf, 0, sizeof(buf));

	p->pwm_type = EC_PWM_TYPE_KB_LIGHT;

	msg->version = 0;
	msg->command = EC_CMD_PWM_GET_DUTY;
	msg->insize = sizeof(*resp);
	msg->outsize = sizeof(*p);

	ret = cros_ec_cmd_xfer_status(ec, msg);
	if (ret < 0) {
		goto out;
	}

	return resp->duty * 100 / EC_PWM_MAX_DUTY;

out:
	return 0;
}

// Set the keyboard LED brightness
static int kb_led_set(struct led_classdev *led, enum led_brightness value)
{
	struct {
		struct cros_ec_command msg;
		union {
			struct ec_params_pwm_set_keyboard_backlight params;
		};
	} __packed buf;

	struct ec_params_pwm_set_keyboard_backlight *params = &buf.params;
	struct cros_ec_command *msg = &buf.msg;
	struct cros_ec_device *ec;
	int ret;

	if (!ec_device)
		return -EIO;

	ec = dev_get_drvdata(ec_device);

	memset(&buf, 0, sizeof(buf));

	msg->version = 0;
	msg->command = EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT;
	msg->insize = 0;
	msg->outsize = sizeof(*params);

	params->percent = value;

	ret = cros_ec_cmd_xfer_status(ec, msg);
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

// Get the fingerprint LED brightness
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

// Set the fingerprint LED brightness
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

	data->kb_led.name = DRV_NAME "::kbd_backlight";
	data->kb_led.brightness_get = kb_led_get;
	data->kb_led.brightness_set_blocking = kb_led_set;
	data->kb_led.max_brightness = 100;

	ret = devm_led_classdev_register(dev, &data->kb_led);
	if (ret)
		return ret;

	/* "fingerprint" is a non-standard name, but this behaves weird anyway */
	data->fp_led.name = DRV_NAME "::fingerprint";
	data->fp_led.brightness_get = fp_led_get;
	data->fp_led.brightness_set_blocking = fp_led_set;
	data->fp_led.max_brightness = 2;

	ret = devm_led_classdev_register(dev, &data->fp_led);
	if (ret)
		return ret;

	return 0;
}

void fw_leds_unregister(struct framework_data *data)
{
	led_classdev_unregister(&data->kb_led);
	led_classdev_unregister(&data->fp_led);
}
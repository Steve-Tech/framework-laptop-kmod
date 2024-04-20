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

// --- fanN_input ---
// Read the current fan speed from the EC's memory
static ssize_t ec_get_fan_speed(u8 idx, u16 *val)
{
	if (!ec_device)
		return -ENODEV;

	struct cros_ec_device *ec = dev_get_drvdata(ec_device);

	const u8 offset = EC_MEMMAP_FAN + 2 * idx;

	return ec->cmd_readmem(ec, offset, sizeof(*val), val);
}

static ssize_t fw_fan_speed_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);

	u16 val;
	if (ec_get_fan_speed(sen_attr->index, &val) < 0) {
		return -EIO;
	}

	if (val == EC_FAN_SPEED_NOT_PRESENT || val == EC_FAN_SPEED_STALLED) {
		return sysfs_emit(buf, "%u\n", 0);
	}

	// Format as string for sysfs
	return sysfs_emit(buf, "%u\n", val);
}

// --- fanN_target ---
static ssize_t ec_set_target_rpm(u8 idx, u32 *val)
{
	int ret;
	if (!ec_device)
		return -ENODEV;

	struct cros_ec_device *ec = dev_get_drvdata(ec_device);

	struct ec_params_pwm_set_fan_target_rpm_v1 params = {
		.rpm = *val,
		.fan_idx = idx,
	};

	ret = cros_ec_cmd(ec, 1, EC_CMD_PWM_SET_FAN_TARGET_RPM, &params,
			  sizeof(params), NULL, 0);
	if (ret < 0)
		return -EIO;

	return 0;
}

static ssize_t ec_get_target_rpm(u8 idx, u32 *val)
{
	int ret;
	if (!ec_device)
		return -ENODEV;

	struct cros_ec_device *ec = dev_get_drvdata(ec_device);

	struct ec_response_pwm_get_fan_rpm resp;

	// index isn't supported, it should only return fan 0's target

	ret = cros_ec_cmd(ec, 0, EC_CMD_PWM_GET_FAN_TARGET_RPM, NULL, 0, &resp,
			  sizeof(resp));
	if (ret < 0)
		return -EIO;

	*val = resp.rpm;

	return 0;
}

static ssize_t fw_fan_target_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);
	u32 val;

	int err;
	err = kstrtou32(buf, 10, &val);
	if (err < 0)
		return err;

	if (ec_set_target_rpm(sen_attr->index, &val) < 0) {
		return -EIO;
	}

	return count;
}

static ssize_t fw_fan_target_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);

	// Only fan 0 is supported
	if (sen_attr->index != 0) {
		return -EINVAL;
	}

	u32 val;
	if (ec_get_target_rpm(sen_attr->index, &val) < 0) {
		return -EIO;
	}

	// Format as string for sysfs
	return sysfs_emit(buf, "%u\n", val);
}

// --- fanN_fault ---
static ssize_t fw_fan_fault_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);

	u16 val;
	if (ec_get_fan_speed(sen_attr->index, &val) < 0) {
		return -EIO;
	}

	// Format as string for sysfs
	return sysfs_emit(buf, "%u\n", val == EC_FAN_SPEED_NOT_PRESENT);
}

// --- fanN_alarm ---
static ssize_t fw_fan_alarm_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);

	u16 val;
	if (ec_get_fan_speed(sen_attr->index, &val) < 0) {
		return -EIO;
	}

	// Format as string for sysfs
	return sysfs_emit(buf, "%u\n", val == EC_FAN_SPEED_STALLED);
}

// --- pwmN_enable ---
static ssize_t ec_set_auto_fan_ctrl(u8 idx)
{
	int ret;
	if (!ec_device)
		return -ENODEV;

	struct cros_ec_device *ec = dev_get_drvdata(ec_device);

	struct ec_params_auto_fan_ctrl_v1 params = {
		.fan_idx = idx,
	};

	ret = cros_ec_cmd(ec, 1, EC_CMD_THERMAL_AUTO_FAN_CTRL, &params,
			  sizeof(params), NULL, 0);
	if (ret < 0)
		return -EIO;

	return 0;
}

static ssize_t fw_pwm_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);

	// The EC doesn't take any arguments for this command,
	// so we don't need to parse the buffer
	// u32 val;

	// int err;
	// err = kstrtou32(buf, 10, &val);
	// if (err < 0)
	// 	return err;

	if (ec_set_auto_fan_ctrl(sen_attr->index) < 0) {
		return -EIO;
	}

	return count;
}

// --- pwmN ---
static ssize_t ec_set_fan_duty(u8 idx, u32 *val)
{
	int ret;
	if (!ec_device)
		return -ENODEV;

	struct cros_ec_device *ec = dev_get_drvdata(ec_device);

	struct ec_params_pwm_set_fan_duty_v1 params = {
		.percent = *val,
		.fan_idx = idx,
	};

	ret = cros_ec_cmd(ec, 1, EC_CMD_PWM_SET_FAN_DUTY, &params,
			  sizeof(params), NULL, 0);
	if (ret < 0)
		return -EIO;

	return 0;
}

static ssize_t fw_pwm_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct sensor_device_attribute *sen_attr = to_sensor_dev_attr(attr);
	u32 val;

	int err;
	err = kstrtou32(buf, 10, &val);
	if (err < 0)
		return err;

	if (ec_set_fan_duty(sen_attr->index, &val) < 0) {
		return -EIO;
	}

	return count;
}

static ssize_t fw_pwm_min_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%i\n", 0);
}

static ssize_t fw_pwm_max_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%i\n", 100);
}

static ssize_t ec_count_fans(size_t *val)
{
	if (!ec_device)
		return -ENODEV;

	struct cros_ec_device *ec = dev_get_drvdata(ec_device);

	u16 fans[EC_FAN_SPEED_ENTRIES];

	int ret = ec->cmd_readmem(ec, EC_MEMMAP_FAN, sizeof(fans), fans);
	if (ret < 0)
		return -EIO;

	for (size_t i = 0; i < EC_FAN_SPEED_ENTRIES; i++) {
		if (fans[i] == EC_FAN_SPEED_NOT_PRESENT) {
			*val = i;
			return 0;
		}
	}

	*val = EC_FAN_SPEED_ENTRIES;
	return 0;
}

#define FW_ATTRS_PER_FAN 8

// --- hwmon sysfs attributes ---
// clang-format off
static SENSOR_DEVICE_ATTR_RO(fan1_input, fw_fan_speed, 0); // Fan Reading
static SENSOR_DEVICE_ATTR_RW(fan1_target, fw_fan_target, 0); // Target RPM (RW on fan 0 only)
static SENSOR_DEVICE_ATTR_RO(fan1_fault, fw_fan_fault, 0); // Fan Not Present
static SENSOR_DEVICE_ATTR_RO(fan1_alarm, fw_fan_alarm, 0); // Fan Stalled
static SENSOR_DEVICE_ATTR_WO(pwm1_enable, fw_pwm_enable, 0); // Set Fan Control Mode
static SENSOR_DEVICE_ATTR_WO(pwm1, fw_pwm, 0); // Set Fan Speed
static SENSOR_DEVICE_ATTR_RO(pwm1_min, fw_pwm_min, 0); // Min Fan Speed
static SENSOR_DEVICE_ATTR_RO(pwm1_max, fw_pwm_max, 0); // Max Fan Speed
// clang-format on

static SENSOR_DEVICE_ATTR_RO(fan2_input, fw_fan_speed, 1);
static SENSOR_DEVICE_ATTR_WO(fan2_target, fw_fan_target, 1);
static SENSOR_DEVICE_ATTR_RO(fan2_fault, fw_fan_fault, 1);
static SENSOR_DEVICE_ATTR_RO(fan2_alarm, fw_fan_alarm, 1);
static SENSOR_DEVICE_ATTR_WO(pwm2_enable, fw_pwm_enable, 1);
static SENSOR_DEVICE_ATTR_WO(pwm2, fw_pwm, 1);
static SENSOR_DEVICE_ATTR_RO(pwm2_min, fw_pwm_min, 1);
static SENSOR_DEVICE_ATTR_RO(pwm2_max, fw_pwm_max, 1);

static SENSOR_DEVICE_ATTR_RO(fan3_input, fw_fan_speed, 2);
static SENSOR_DEVICE_ATTR_WO(fan3_target, fw_fan_target, 2);
static SENSOR_DEVICE_ATTR_RO(fan3_fault, fw_fan_fault, 2);
static SENSOR_DEVICE_ATTR_RO(fan3_alarm, fw_fan_alarm, 2);
static SENSOR_DEVICE_ATTR_WO(pwm3_enable, fw_pwm_enable, 2);
static SENSOR_DEVICE_ATTR_WO(pwm3, fw_pwm, 2);
static SENSOR_DEVICE_ATTR_RO(pwm3_min, fw_pwm_min, 2);
static SENSOR_DEVICE_ATTR_RO(pwm3_max, fw_pwm_max, 2);

static SENSOR_DEVICE_ATTR_RO(fan4_input, fw_fan_speed, 3);
static SENSOR_DEVICE_ATTR_WO(fan4_target, fw_fan_target, 3);
static SENSOR_DEVICE_ATTR_RO(fan4_fault, fw_fan_fault, 3);
static SENSOR_DEVICE_ATTR_RO(fan4_alarm, fw_fan_alarm, 3);
static SENSOR_DEVICE_ATTR_WO(pwm4_enable, fw_pwm_enable, 3);
static SENSOR_DEVICE_ATTR_WO(pwm4, fw_pwm, 3);
static SENSOR_DEVICE_ATTR_RO(pwm4_min, fw_pwm_min, 3);
static SENSOR_DEVICE_ATTR_RO(pwm4_max, fw_pwm_max, 3);

static struct attribute
	*fw_hwmon_attrs[(EC_FAN_SPEED_ENTRIES * FW_ATTRS_PER_FAN) + 1] = {
		&sensor_dev_attr_fan1_input.dev_attr.attr,
		&sensor_dev_attr_fan1_target.dev_attr.attr,
		&sensor_dev_attr_fan1_fault.dev_attr.attr,
		&sensor_dev_attr_fan1_alarm.dev_attr.attr,
		&sensor_dev_attr_pwm1_enable.dev_attr.attr,
		&sensor_dev_attr_pwm1.dev_attr.attr,
		&sensor_dev_attr_pwm1_min.dev_attr.attr,
		&sensor_dev_attr_pwm1_max.dev_attr.attr,

		&sensor_dev_attr_fan2_input.dev_attr.attr,
		&sensor_dev_attr_fan2_target.dev_attr.attr,
		&sensor_dev_attr_fan2_fault.dev_attr.attr,
		&sensor_dev_attr_fan2_alarm.dev_attr.attr,
		&sensor_dev_attr_pwm2_enable.dev_attr.attr,
		&sensor_dev_attr_pwm2.dev_attr.attr,
		&sensor_dev_attr_pwm2_min.dev_attr.attr,
		&sensor_dev_attr_pwm2_max.dev_attr.attr,

		&sensor_dev_attr_fan3_input.dev_attr.attr,
		&sensor_dev_attr_fan3_target.dev_attr.attr,
		&sensor_dev_attr_fan3_fault.dev_attr.attr,
		&sensor_dev_attr_fan3_alarm.dev_attr.attr,
		&sensor_dev_attr_pwm3_enable.dev_attr.attr,
		&sensor_dev_attr_pwm3.dev_attr.attr,
		&sensor_dev_attr_pwm3_min.dev_attr.attr,
		&sensor_dev_attr_pwm3_max.dev_attr.attr,

		&sensor_dev_attr_fan4_input.dev_attr.attr,
		&sensor_dev_attr_fan4_target.dev_attr.attr,
		&sensor_dev_attr_fan4_fault.dev_attr.attr,
		&sensor_dev_attr_fan4_alarm.dev_attr.attr,
		&sensor_dev_attr_pwm4_enable.dev_attr.attr,
		&sensor_dev_attr_pwm4.dev_attr.attr,
		&sensor_dev_attr_pwm4_min.dev_attr.attr,
		&sensor_dev_attr_pwm4_max.dev_attr.attr,

		NULL,
	};

ATTRIBUTE_GROUPS(fw_hwmon);

int fw_hwmon_register(struct framework_data *data)
{
	struct device *dev = &data->pdev->dev;
	struct cros_ec_device *ec = dev_get_drvdata(data->ec_device);

	ec_device = data->ec_device;

	if (ec->cmd_readmem) {
		// Count the number of fans
		size_t fan_count;
		if (ec_count_fans(&fan_count) < 0) {
			dev_err(dev, DRV_NAME ": failed to count fans.\n");
			return -EINVAL;
		}
		// NULL terminates the list after the last detected fan
		fw_hwmon_attrs[fan_count * FW_ATTRS_PER_FAN] = NULL;

		data->hwmon_dev = hwmon_device_register_with_groups(
			dev, DRV_NAME, NULL, fw_hwmon_groups);
		if (IS_ERR(data->hwmon_dev))
			return PTR_ERR(data->hwmon_dev);

	} else {
		dev_err(dev,
			DRV_NAME
			": fan readings could not be enabled for this EC %s.\n",
			FRAMEWORK_LAPTOP_EC_DEVICE_NAME);
	}

	return 0;
}

void fw_hwmon_unregister(struct framework_data *data)
{
	if (!data->hwmon_dev)
		return;

	hwmon_device_unregister(data->hwmon_dev);
}

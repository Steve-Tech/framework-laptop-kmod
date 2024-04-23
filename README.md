A kernel module that exposes the Framework Laptop (13, 16)'s battery charge limit and LEDs to userspace.

## Install

### Packages

On Arch Linux, this module is packaged in the Arch User Repository as
[`framework-laptop-kmod-dkms-git`](https://aur.archlinux.org/packages/framework-laptop-kmod-dkms-git).
You can install it for all your locally installed kernels with your favorite
AUR helper:

```console
$ yay -S framework-laptop-kmod-dkms-git
```

### Building

By default, this project will try to build a module for your running kernel.

```console
$ make
```

If you need to target a different kernel, set `KDIR`:

```console
$ make KDIR=/usr/src/linux-6.5
```

You can install the module systemwide with `make modules_install`.

## Usage

If the module is installed systemwide, you can load it with 
`modprobe framework_laptop`. If you built it manually, you can also use
`insmod ./framework_laptop.ko`.

This module requires `cros_ec` and `cros_ec_lpcs` to be loaded and functional.

> **Note**
> For the Framework Laptop 13 AMD Ryzen 7040 series and the Framework Laptop 16,
> you will need to apply [this patch series](https://lore.kernel.org/chrome-platform/20231005160701.19987-1-dustin@howett.net/) to your kernel sources.

### Battery Charge Limit

- Exposed via `charge_control_end_threshold`, available on `BAT1`
  - `/sys/class/power_supply/BAT1/charge_control_end_threshold`

### LEDs

The keyboard backlight, fingerprint light, and side LEDs are exposed in SysFS as LEDs.

- `/sys/class/leds/framework_laptop::kbd_backlight` - Keyboard backlight (0-100)
- `/sys/class/leds/framework_laptop::fingerprint` - Fingerprint light (0-2)
- `/sys/class/leds/framework_laptop:<color>:indicator` - Side LEDs (0-100)
  - `<color>` is one of `red`, `green`, `blue`, `yellow`, `white`, `amber`.
  - It's a good idea to echo `none` to `/sys/class/leds/framework_laptop:<color>:indicator/trigger` to disable the default trigger.
  - If you want the EC to take over control again, echo `framework-laptop` to the same file.

### HWMON

#### Fan Control

This driver supports up to 4 fans, and creates a HWMON interface with the name `framework_laptop`.

- `fan[1-4]_input` - Read fan speed in RPM (read-only)
- `fan[1-4]_target` - Set target fan speed in RPM
  - read-write on the first fan, write-only on the others
- `fan[1-4]_fault` - Fan removed indicator (read-only)
- `fan[1-4]_alarm` - Fan stall indicator (read-only)
- `pwm[1-4]` - Fan speed control in percent 0-100 (write-only)
- `pwm[1-4]_enable` - Enable automatic fan control (write-only)
  - Currently you can write anything to enable, but writing `2` is recommended in case the driver is updated to support disabling automatic fan control.
  - Writing to the other interfaces will disable automatic fan control.
- `pwm[1-4]_min` - returns 0 (read-only)
- `pwm[1-4]_max` - returns 100 (read-only)

#### Intrusion Detection

- `intrusion0_alarm` - Chassis intrusion indicator (read-write)
  - Writing `0` will clear the alarm
  - Reading will return the alarm status (0 or 1)
- `intrusion1_alarm` - Chassis open indicator (read-only)
  - Reading will return the alarm status (0 or 1)

### Privacy Switches

This driver exposes the privacy switches as a custom SysFS interface under `/sys/devices/platform/framework_laptop/framework_privacy`.
It follows the [existing format of the `dell-privacy` driver](https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-platform-dell-privacy-wmi).

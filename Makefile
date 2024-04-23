ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := framework_laptop.o
framework_laptop-objs := framework_laptop_main.o framework_laptop_hwmon.o framework_laptop_leds.o framework_laptop_color_leds.o framework_laptop_battery.o framework_laptop_sysfs.o

else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

modules:

%:
	$(MAKE) -C $(KDIR) M=$$PWD $@

endif


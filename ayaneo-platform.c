// SPDX-License-Identifier: GPL-3.0+
/*
 * Platform driver for AYANEO x86 Handhelds that exposes RGB LED
 * control via a sysfs led_class_multicolor interface.
 *
 * Copyright (C) 2023 Derek J. Clark <derekjohn.clark@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/processor.h>
#include <linux/delay.h>

/* Handle ACPI lock mechanism */
static u32 ayaneo_mutex;

#define ACPI_LOCK_DELAY_MS	500

static bool lock_global_acpi_lock(void)
{
	return ACPI_SUCCESS(acpi_acquire_global_lock(ACPI_LOCK_DELAY_MS, &ayaneo_mutex));
}

static bool unlock_global_acpi_lock(void)
{
	return ACPI_SUCCESS(acpi_release_global_lock(ayaneo_mutex));
}

/* Common ec ram port data */
#define AYANEO_ADDR_PORT 	0x4e
#define AYANEO_DATA_PORT 	0x4f
#define AYANEO_HIGH_BYTE 	0xd1
#define AYANEO_LED_MC_OFF	0x31
#define AYANEO_LED_MC_ON	0x37


/* RGB LED EC Ram Registers */
#define AYANEO_LED_MC_L_Q1_R	0xb3
#define AYANEO_LED_MC_L_Q1_G	0xb4
#define AYANEO_LED_MC_L_Q1_B	0xb5
#define AYANEO_LED_MC_L_Q2_R	0xb6
#define AYANEO_LED_MC_L_Q2_G	0xb7
#define AYANEO_LED_MC_L_Q2_B	0xb8
#define AYANEO_LED_MC_L_Q3_R	0xb9
#define AYANEO_LED_MC_L_Q3_G	0xba
#define AYANEO_LED_MC_L_Q3_B	0xbb
#define AYANEO_LED_MC_L_Q4_R	0xbc
#define AYANEO_LED_MC_L_Q4_G	0xbd
#define AYANEO_LED_MC_L_Q4_B	0xbe
#define AYANEO_LED_MC_R_Q1_R	0x73
#define AYANEO_LED_MC_R_Q1_G	0x74
#define AYANEO_LED_MC_R_Q1_B	0x75
#define AYANEO_LED_MC_R_Q2_R	0x76
#define AYANEO_LED_MC_R_Q2_G	0x77
#define AYANEO_LED_MC_R_Q2_B	0x78
#define AYANEO_LED_MC_R_Q3_R	0x79
#define AYANEO_LED_MC_R_Q3_G	0x7a
#define AYANEO_LED_MC_R_Q3_B	0x7b
#define AYANEO_LED_MC_R_Q4_R	0x7c
#define AYANEO_LED_MC_R_Q4_G	0x7d
#define AYANEO_LED_MC_R_Q4_B	0x7e

#define UNCLEAR_CMD_1		0x86
#define UNCLEAR_CMD_2		0xc6
/* Schema:
#
# 0x6d - LED PWM control (0x03)
#
# 0xb1 - Support for 4 zones and RGB color
#
#   RGB colors:
#
#   1 - Red
#   2 - Green
#   3 - Blue
#
#   Zones:
#
#   Right (2), Down (5), Left (8) , Up (11)
#
#   Note: Set 0xb1 to 02 for off.
#
# 0xb2 - Sets brightness, requires b1 to be set at the same time.
#
#   00-ff - brightness from 0-255.  Not noticeable to me above 128.
#
# 0xbf - Set expected mode
#
#   0x10 - Enable
#   0xe2 - Tint (+ Red for Purple, + Green for Teal)
#   0xe3-0e5 - Tint + blink (unused)
#
#   0xff - Close channel
*/
/* EC Controlled RGB registers */
#define AYANEO_LED_PWM_CONTROL	0x6d
#define AYANEO_LED_POS_COLOR	0xb1
#define AYANEO_LED_BRIGHTNESS	0xb2
#define AYANEO_LED_MODE_REG     0xbf

#define AYANEO_LED_CMD_OFF	0x02

/* RGB Mode values */
#define AYANEO_LED_MODE_WRITE             0x10 /* Default write mode */
#define AYANEO_LED_MODE_WRITE_END         0xff /* close channel */


enum ayaneo_model {
	air = 1,
	air_1s,
	air_pro,
	air_plus,
        air_plus_mendo,
	geek,
	geek_1s,
	ayaneo_2,
	ayaneo_2s,
};

static enum ayaneo_model model;

static const struct dmi_system_id dmi_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AB05-Mendocino"),
		},
		.driver_data = (void *)air_plus_mendo,
	},
	{},
};

/* Helper functions to handle EC read/write */
/*
static int read_from_ec(u8 reg, int size, long *val)
{
	int i;
	int ret;
	u8 buffer;

	if (!lock_global_acpi_lock())
		return -EBUSY;

	*val = 0;
	for (i = 0; i < size; i++) {
		ret = ec_read(reg + i, &buffer);
		if (ret)
			return ret;
		*val <<= i * 8;
		*val += buffer;
	}

	if (!unlock_global_acpi_lock())
		return -EBUSY;

	return 0;
}
*/
static int write_to_ec(u8 reg, u8 val)
{
	int ret;

	if (!lock_global_acpi_lock())
		return -EBUSY;

	ret = ec_write(reg, val);

	if (!unlock_global_acpi_lock())
		return -EBUSY;

	return ret;
}

static void probe_ec_ram_index(u8 index)
{

	outb(0x2e, AYANEO_ADDR_PORT);
        outb(0x11, AYANEO_DATA_PORT);
        outb(0x2f, AYANEO_ADDR_PORT);
        outb(AYANEO_HIGH_BYTE, AYANEO_DATA_PORT);
                                                                     
        outb(0x2e, AYANEO_ADDR_PORT);
        outb(0x10, AYANEO_DATA_PORT);
        outb(0x2f, AYANEO_ADDR_PORT);
        outb(index, AYANEO_DATA_PORT);
                                                                     
        outb(0x2e, AYANEO_ADDR_PORT);
        outb(0x12, AYANEO_DATA_PORT);
        outb(0x2f, AYANEO_ADDR_PORT);
}

static u8 read_ec_ram(u8 index)
{
	probe_ec_ram_index(index);
	return inb(AYANEO_DATA_PORT);
}

static void write_ec_ram(u8 index, u8 val)
{
	probe_ec_ram_index(index);
        outb(val, AYANEO_DATA_PORT);
}

static void ayaneo_led_mc_open(void)
{
	write_ec_ram(0x87, 0xa5);
}

static void ayaneo_led_mc_close(u8 index)
{
	write_ec_ram(index, 0x01);
}

static void ayaneo_led_mc_write(void)
{
	ayaneo_led_mc_open();
	write_ec_ram(0x70, 0x00);
	ayaneo_led_mc_close(0x86);
}

static void ayaneo_led_mc_set(u8 color, u8 brightness)
{
        write_to_ec(AYANEO_LED_PWM_CONTROL, 0x03);
        write_to_ec(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_WRITE);
        write_to_ec(AYANEO_LED_POS_COLOR, color);
        write_to_ec(AYANEO_LED_BRIGHTNESS, brightness);
        msleep(5);
        write_to_ec(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_WRITE_END);
}
/*
static void ayaneo_led_mc_off(void)
{
	ayaneo_led_mc_set(AYANEO_LED_CMD_OFF, 0x00);
}
*/
/* RGB LED Logic */
static void ayaneo_led_mc_brightness_set(struct led_classdev *led_cdev,
                                      enum led_brightness brightness)
{
        struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(led_cdev);
	int val;
        int i, j, zone;
	struct mc_subled s_led;
        u8 zones[4] = {2, 5, 8, 11};

	led_cdev->brightness = brightness;

        for (zone = 0; zone < 4; zone++) {
	        for (i = 0; i < mc_cdev->num_colors; i++) {
		        s_led = mc_cdev->subled_info[i];
	        	val = brightness * s_led.intensity / led_cdev->max_brightness;
		        ayaneo_led_mc_set(zones[zone] + s_led.channel, val);
	        }
        }
};

static enum led_brightness ayaneo_led_mc_brightness_get(struct led_classdev *led_cdev)
{
	return led_cdev->brightness;
};

struct mc_subled ayaneo_led_mc_subled_info[] = {
        {
                .color_index = LED_COLOR_ID_RED,
                .brightness = 0,
                .intensity = 0,
                .channel = 1,
        },
        {
                .color_index = LED_COLOR_ID_GREEN,
                .brightness = 0,
                .intensity = 0,
                .channel = 2,
        },
        {
                .color_index = LED_COLOR_ID_BLUE,
                .brightness = 0,
                .intensity = 0,
                .channel = 3,
        },
};

struct led_classdev_mc ayaneo_led_mc = {
        .led_cdev = {
                .name = "multicolor:chassis",
                .brightness = 0,
                .max_brightness = 255,
                .brightness_set = ayaneo_led_mc_brightness_set,
                .brightness_get = ayaneo_led_mc_brightness_get,
        },
        .num_colors = ARRAY_SIZE(ayaneo_led_mc_subled_info),
        .subled_info = ayaneo_led_mc_subled_info,
};

static int ayaneo_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwdev;
	int ret;

        ret = devm_led_classdev_multicolor_register(dev, &ayaneo_led_mc);
        
	//if (ret)
	return ret;

	//hwdev = devm_device_register("TODO");
	//return PTR_ERR_OR_ZERO(hwdev);
}

static struct platform_driver ayaneo_platform_driver = {
	.driver = {
		.name = "ayaneo-platform",
	},
	.probe = ayaneo_platform_probe,
};

static struct platform_device *ayaneo_platform_device;

static int __init ayaneo_platform_init(void)
{
	ayaneo_platform_device =
		platform_create_bundle(&ayaneo_platform_driver,
				       ayaneo_platform_probe, NULL, 0, NULL, 0);

	return PTR_ERR_OR_ZERO(ayaneo_platform_device);
}

static void __exit ayaneo_platform_exit(void)
{
	platform_device_unregister(ayaneo_platform_device);
	platform_driver_unregister(&ayaneo_platform_driver);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

module_init(ayaneo_platform_init);
module_exit(ayaneo_platform_exit);

MODULE_AUTHOR("Derek John Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Platform driver that handles EC sensors of AYANEO x86 devices");
MODULE_LICENSE("GPL");

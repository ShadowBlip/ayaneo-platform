// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for AYANEO x86 Handhelds that exposes RGB LED
 * control via a sysfs led_class_multicolor interface.
 *
 * Copyright (C) 2023-2024 Derek J. Clark <derekjohn.clark@gmail.com>
 * Copyright (C) 2023-2024 JELOS <https://github.com/JustEnoughLinuxOS>
 * Copyright (C) 2024 Sebastian Kranz <https://github.com/Lightwars>
 * Derived from original reverse engineering work by Maya Matuszczyk
 * <https://github.com/Maccraft123/ayaled>
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

#define ACPI_LOCK_DELAY_MS        500

static bool lock_global_acpi_lock(void)
{
        return ACPI_SUCCESS(acpi_acquire_global_lock(ACPI_LOCK_DELAY_MS, &ayaneo_mutex));
}

static bool unlock_global_acpi_lock(void)
{
        return ACPI_SUCCESS(acpi_release_global_lock(ayaneo_mutex));
}

/* Common ec ram port data */
#define AYANEO_ADDR_PORT         0x4e
#define AYANEO_DATA_PORT         0x4f
#define AYANEO_HIGH_BYTE         0xd1
#define AYANEO_LED_MC_OFF        0x31
#define AYANEO_LED_MC_ON         0x37


/* RGB LED EC Ram Registers */
/*
#define AYANEO_LED_MC_L_Q1_R     0xb3
#define AYANEO_LED_MC_L_Q1_G     0xb4
#define AYANEO_LED_MC_L_Q1_B     0xb5
#define AYANEO_LED_MC_L_Q2_R     0xb6
#define AYANEO_LED_MC_L_Q2_G     0xb7
#define AYANEO_LED_MC_L_Q2_B     0xb8
#define AYANEO_LED_MC_L_Q3_R     0xb9
#define AYANEO_LED_MC_L_Q3_G     0xba
#define AYANEO_LED_MC_L_Q3_B     0xbb
#define AYANEO_LED_MC_L_Q4_R     0xbc
#define AYANEO_LED_MC_L_Q4_G     0xbd
#define AYANEO_LED_MC_L_Q4_B     0xbe
#define AYANEO_LED_MC_R_Q1_R     0x73
#define AYANEO_LED_MC_R_Q1_G     0x74
#define AYANEO_LED_MC_R_Q1_B     0x75
#define AYANEO_LED_MC_R_Q2_R     0x76
#define AYANEO_LED_MC_R_Q2_G     0x77
#define AYANEO_LED_MC_R_Q2_B     0x78
#define AYANEO_LED_MC_R_Q3_R     0x79
#define AYANEO_LED_MC_R_Q3_G     0x7a
#define AYANEO_LED_MC_R_Q3_B     0x7b
#define AYANEO_LED_MC_R_Q4_R     0x7c
#define AYANEO_LED_MC_R_Q4_G     0x7d
#define AYANEO_LED_MC_R_Q4_B     0x7e
*/
#define CLOSE_CMD_1              0x86
#define CLOSE_CMD_2              0xc6
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
#   0xe3-0xe5 - Tint + blink (unused)
#
#   0xff - Close channel
*/
/* EC Controlled RGB registers */
#define AYANEO_LED_PWM_CONTROL      0x6d
#define AYANEO_LED_POS              0xb1
#define AYANEO_LED_BRIGHTNESS       0xb2
#define AYANEO_LED_MODE_REG         0xbf
#define AYANEO_LED_CMD_OFF          0x02

/* RGB Mode values */
#define AYANEO_LED_MODE_WRITE       0x10 /* Default write mode */
#define AYANEO_LED_MODE_WRITE_END   0xff /* close channel */

enum ayaneo_model {
        air = 1,
        air_1s,
        air_plus,
        air_plus_mendo,
        air_pro,
        ayaneo_2,
        ayaneo_2s,
        geek,
        geek_1s,
        kun,
};

static enum ayaneo_model model;

static const struct dmi_system_id dmi_table[] = {
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR"),
                },
                .driver_data = (void *)air,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR 1S"),
                },
                .driver_data = (void *)air_1s,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR 1S Limited"),
                },
                .driver_data = (void *)air_1s,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AB05-AMD"),
                },
                .driver_data = (void *)air_plus,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AB05-Mendocino"),
                },
                .driver_data = (void *)air_plus_mendo,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AIR Pro"),
                },
                .driver_data = (void *)air_pro,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AYANEO 2"),
                },
                .driver_data = (void *)ayaneo_2,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AYANEO 2S"),
                },
                .driver_data = (void *)ayaneo_2s,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "GEEK"),
                },
                .driver_data = (void *)geek,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "GEEK 1S"),
                },
                .driver_data = (void *)geek_1s,
        },
        {
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AYANEO KUN"),
                },
                .driver_data = (void *)kun,
        },
        {},
};

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

static int write_ec_ram(u8 index, u8 val)
{
        int ret;

        if (!lock_global_acpi_lock())
                return -EBUSY;

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
        outb(val, AYANEO_DATA_PORT);

        if (!unlock_global_acpi_lock())
                return -EBUSY;

        return ret;
}

/* AIR Plus methods */
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
        write_ec_ram(0x70, 0x00);
        ayaneo_led_mc_close(CLOSE_CMD_1);
}

static void ayaneo_led_mc_state(u8 state)
{
        // 0x31 = off
        // 0x37 = on
        u8 zone[2] = {0xb2, 0x72};
        u8 zoneindex;
  
        ayaneo_led_mc_open();
        for (zoneindex = 0; zoneindex < 2; zoneindex++) {
                write_ec_ram(zone[zoneindex], state);
                ayaneo_led_mc_close(CLOSE_CMD_2);
        }
        ayaneo_led_mc_write();
}

/* Open every LED register as write enabled */
static void ayaneo_led_mc_apply(void)
{
        ayaneo_led_mc_state(AYANEO_LED_MC_ON);

        ayaneo_led_mc_open();
        write_ec_ram(0x70, 0x0);
        ayaneo_led_mc_close(CLOSE_CMD_1);

        ayaneo_led_mc_open();
        write_ec_ram(0xb2, 0xba);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0x72, 0xba);
        ayaneo_led_mc_close(CLOSE_CMD_1);
  
        ayaneo_led_mc_open();
        write_ec_ram(0xbf, 0x0);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0x7f, 0x0);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0xc0, 0x0);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0x80, 0x0);
        ayaneo_led_mc_close(CLOSE_CMD_1);

        ayaneo_led_mc_open();
        write_ec_ram(0xc1, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0x81, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_2);
  
        ayaneo_led_mc_open();
        write_ec_ram(0xc2, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0x82, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_1);

        ayaneo_led_mc_open();
        write_ec_ram(0xc3, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_1);

        ayaneo_led_mc_open();
        write_ec_ram(0x83, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_1);

        ayaneo_led_mc_open();
        write_ec_ram(0xc4, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0x84, 0x5);
        ayaneo_led_mc_close(CLOSE_CMD_1);

        ayaneo_led_mc_open();
        write_ec_ram(0xc5, 0x7);
        ayaneo_led_mc_close(CLOSE_CMD_2);

        ayaneo_led_mc_open();
        write_ec_ram(0x85, 0x7);
        ayaneo_led_mc_close(CLOSE_CMD_1);
  
        ayaneo_led_mc_write();
}

static void ayaneo_led_mc_color(u8 *color)
{
        u8 quadrant;
  
        // Zone 1 (Left Stick)
        ayaneo_led_mc_open();
        for(quadrant = 0; quadrant < 12; quadrant++) {
                write_ec_ram(0xB3 + quadrant, color[quadrant % 3]); // Quadrant 1
        }
        ayaneo_led_mc_write();
  
        // Zone 2 (Right Stick)
        ayaneo_led_mc_open();
        for(quadrant = 0; quadrant < 12; quadrant++) {
                write_ec_ram(0x73 + quadrant, color[quadrant % 3]); // Quadrant 1
        }
        ayaneo_led_mc_write();
}

/* Legacy methods */
static void ayaneo_led_mc_set(u8 pos, u8 brightness)
{
        write_to_ec(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_WRITE);
        write_to_ec(AYANEO_LED_POS, pos);
        write_to_ec(AYANEO_LED_BRIGHTNESS, brightness);
        mdelay(1);
        write_to_ec(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_WRITE_END);
}

static void ayaneo_led_mc_intensity(u8 *color, u8 group, u8 zones[])
{
        int zone;
  
        write_to_ec(AYANEO_LED_PWM_CONTROL, group);
        for (zone = 0; zone < 4; zone++) {
                ayaneo_led_mc_set(zones[zone], color[0]);
                ayaneo_led_mc_set(zones[zone] + 1, color[1]);
                ayaneo_led_mc_set(zones[zone] + 2, color[2]);
        }
}

static void ayaneo_led_mc_off(u8 group)
{
        write_to_ec(AYANEO_LED_PWM_CONTROL, group);
        ayaneo_led_mc_set(AYANEO_LED_CMD_OFF, 0xc0); // set all leds to off
        ayaneo_led_mc_set(AYANEO_LED_CMD_OFF, 0x80); // needed to switch leds on again
}

static void ayaneo_led_mc_take_control(void)
{
        switch (model) {
                case air:
                case air_1s:
                case air_pro:
                case air_plus_mendo:
                case geek:
                case geek_1s:
                case ayaneo_2:
                case ayaneo_2s:
                        ayaneo_led_mc_off(0x03);
                        break;
                case air_plus:
                        ayaneo_led_mc_state(AYANEO_LED_MC_OFF);
                        break;
                case kun:
                        ayaneo_led_mc_off(0x03);
                        ayaneo_led_mc_off(0x04);
                        break;
                default:
                        break;
                }
}

/* RGB LED Logic */
static void ayaneo_led_mc_brightness_set(struct led_classdev *led_cdev,
                                      enum led_brightness brightness)
{
        struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(led_cdev);
        int val;
        int i;
        struct mc_subled s_led;
        u8 color[3];

        if (brightness < 0 || brightness > 255)
                return;

        led_cdev->brightness = brightness;

        for (i = 0; i < mc_cdev->num_colors; i++) {
                s_led = mc_cdev->subled_info[i];
                if (s_led.intensity < 0 || s_led.intensity > 255)
                        return;
                val = brightness * s_led.intensity / led_cdev->max_brightness;
                color[s_led.channel] = val;
        }

        switch (model) {
                case air:
                case air_1s:
                case air_pro:
                case air_plus_mendo:
                case geek:
                case geek_1s:
                case ayaneo_2:
                case ayaneo_2s:
                        u8 zones[4] = {3, 6, 9, 12};
                        ayaneo_led_mc_intensity(color, 0x03, zones);
                        break;
                case air_plus:
                        ayaneo_led_mc_color(color);
                        ayaneo_led_mc_apply();
                        break;
                case kun:
                        u8 button_zone[1] = {12};
                        ayaneo_led_mc_intensity(color, 0x04, button_zone);
                        u8 joystick_zones[4] = {3, 6, 9, 12};
                        ayaneo_led_mc_intensity(color, 0x03, joystick_zones);
                        break;
                default:
                        break;
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
                .channel = 0,
        },
        {
                .color_index = LED_COLOR_ID_GREEN,
                .brightness = 0,
                .intensity = 0,
                .channel = 1,
        },
        {
                .color_index = LED_COLOR_ID_BLUE,
                .brightness = 0,
                .intensity = 0,
                .channel = 2,
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


static int ayaneo_platform_resume(struct platform_device *pdev)
{
        struct led_classdev *led_cdev = &ayaneo_led_mc.led_cdev;
        ayaneo_led_mc_brightness_set(led_cdev, led_cdev->brightness);
        return 0;
}

static int ayaneo_platform_probe(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;
        const struct dmi_system_id *match;
        int ret;

        match = dmi_first_match(dmi_table);
        ret = PTR_ERR_OR_ZERO(match);
        if (ret)
                return ret;
        model = (enum ayaneo_model)match->driver_data;
        ayaneo_led_mc_take_control();
        ret = devm_led_classdev_multicolor_register(dev, &ayaneo_led_mc);
        return ret;
}

static struct platform_driver ayaneo_platform_driver = {
        .driver = {
                .name = "ayaneo-platform",
        },
        .probe = ayaneo_platform_probe,
        .resume = ayaneo_platform_resume,
};

static struct platform_device *ayaneo_platform_device;

static int __init ayaneo_platform_init(void)
{
        ayaneo_platform_device = platform_create_bundle(&ayaneo_platform_driver,
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

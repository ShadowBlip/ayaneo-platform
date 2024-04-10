// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for AYANEO x86 Handhelds that exposes RGB LED
 * control via a sysfs led_class_multicolor interface.
 *
 * Copyright (C) 2023-2024 Derek J. Clark <derekjohn.clark@gmail.com>
 * Copyright (C) 2023-2024 JELOS <https://github.com/JustEnoughLinuxOS>
 * Copyright (C) 2024 Sebastian Kranz <https://github.com/Lightwars>
 * Copyright (C) 2024 SytheZN <https://github.com/SytheZN>
 * Derived from original reverse engineering work by Maya Matuszczyk
 * <https://github.com/Maccraft123/ayaled>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/processor.h>

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

#define AYANEO_LED_MC_ADDR_L          0xb0
#define AYANEO_LED_MC_ADDR_R          0x70

#define AYANEO_LED_MC_ADDR_CLOSE_1    0x86
#define AYANEO_LED_MC_ADDR_CLOSE_2    0xc6

#define AYANEO_LED_MC_ENABLE_ADDR     0xb2
#define AYANEO_LED_MC_ENABLE_ON       0xb1
#define AYANEO_LED_MC_ENABLE_OFF      0x31
#define AYANEO_LED_MC_ENABLE_RESET    0xc0

#define AYANEO_LED_MC_MODE_ADDR       0x87
#define AYANEO_LED_MC_MODE_HOLD       0xa5
#define AYANEO_LED_MC_MODE_RELEASE    0x00

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

#define AYANEO_LED_CMD_ENABLE_ADDR  0x02
#define AYANEO_LED_CMD_ENABLE_ON    0xb1
#define AYANEO_LED_CMD_ENABLE_OFF   0x31
#define AYANEO_LED_CMD_ENABLE_RESET 0xc0

/* RGB Mode values */
#define AYANEO_LED_MODE_RELEASE     0x00 /* close channel, release control */
#define AYANEO_LED_MODE_WRITE       0x10 /* Default write mode */
#define AYANEO_LED_MODE_HOLD        0xfe /* close channel, hold control */

#define AYANEO_LED_GROUP_LEFT       0x01
#define AYANEO_LED_GROUP_RIGHT      0x02
#define AYANEO_LED_GROUP_LEFT_RIGHT 0x03
#define AYANEO_LED_GROUP_BUTTON     0x04

#define AYANEO_LED_WRITE_DELAY_LEGACY_MS        2
#define AYANEO_LED_WRITE_DELAY_MS               10
#define AYANEO_LED_WRITER_DELAY_RANGE_US        10000, 20000

enum ayaneo_model {
        air = 1,
        air_1s,
        air_1s_limited,
        air_plus,
        air_plus_mendo,
        air_pro,
        ayaneo_2,
        ayaneo_2s,
        geek,
        geek_1s,
        kun,
        slide,
};

static enum ayaneo_model model;

enum AYANEO_LED_SUSPEND_MODE {
        AYANEO_LED_SUSPEND_MODE_OEM,
        AYANEO_LED_SUSPEND_MODE_KEEP,
        AYANEO_LED_SUSPEND_MODE_OFF
};

static const char * const AYANEO_LED_SUSPEND_MODE_TEXT[] = {
        [AYANEO_LED_SUSPEND_MODE_OEM] = "oem",
        [AYANEO_LED_SUSPEND_MODE_KEEP] ="keep",
        [AYANEO_LED_SUSPEND_MODE_OFF] = "off"
};

static enum AYANEO_LED_SUSPEND_MODE suspend_mode;

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
                .driver_data = (void *)air_1s_limited,
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
	{
                .matches = {
                        DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
                        DMI_EXACT_MATCH(DMI_BOARD_NAME, "AS01"),
                },
                .driver_data = (void *)slide,
        },
        {},
};

static int ec_write_ram(u8 index, u8 val)
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

/* Function Summary
# AyaNeo devices can be largely divided into 2 groups; modern and legacy.
# - Legacy devices use a microcontroller either embedded into or controlled via
# the system's ACPI controller.
# - Modern devices use a dedicated microcontroller and communicate via shared
# memory.
#
# The control scheme is largely shared between the 2 device types and many of
# the command values are shared.
#
# ayaneo_led_mc_set / ayaneo_led_mc_legacy_set
#       Sets the value of a single address or subpixel
#
# ayaneo_led_mc_release / ayaneo_led_mc_legacy_release
#       Releases control of the LEDs back to the microcontroller.
#       This function is abstracted by ayaneo_led_mc_release_control.
#
# ayaneo_led_mc_hold / ayaneo_led_mc_legacy_hold
#       Takes and holds control of the LEDs from the microcontroller.
#       This function is abstracted by ayaneo_led_mc_take_control.
#
# ayaneo_led_mc_intensity / ayaneo_led_mc_legacy_intensity
#       Sets the values of all of the LEDs in the zones of a given group.
#
# ayaneo_led_mc_off / ayaneo_led_mc_legacy_off
#       Instructs the microcontroller to disable output for the given group.
#
# ayaneo_led_mc_on / ayaneo_led_mc_legacy_on
#       Instructs the microcontroller to enable output for the given group.
#
# ayaneo_led_mc_reset / ayaneo_led_mc_legacy_reset
#       Reverts all of the microcontroller internal registers to power on
#       defaults.
*/

/* Dedicated microcontroller methods */
static void ayaneo_led_mc_set(u8 group, u8 pos, u8 brightness)
{
        u8 led_offset;
        u8 close_cmd;

        if (group < 2)
        {
                led_offset = AYANEO_LED_MC_ADDR_L;
                close_cmd = AYANEO_LED_MC_ADDR_CLOSE_2;
        }
        else
        {
                led_offset = AYANEO_LED_MC_ADDR_R;
                close_cmd = AYANEO_LED_MC_ADDR_CLOSE_1;
        }

        ec_write_ram(led_offset + pos, brightness);
        ec_write_ram(close_cmd, 0x01);
        mdelay(AYANEO_LED_WRITE_DELAY_MS);
}

static void ayaneo_led_mc_release(void)
{
        ec_write_ram(AYANEO_LED_MC_MODE_ADDR, AYANEO_LED_MC_MODE_RELEASE);
}

static void ayaneo_led_mc_hold(void)
{
        ec_write_ram(AYANEO_LED_MC_MODE_ADDR, AYANEO_LED_MC_MODE_HOLD);
}

static void ayaneo_led_mc_intensity(u8 group, u8 *color, u8 zones[])
{
        int zone;

        for (zone = 0; zone < 4; zone++) {
                ayaneo_led_mc_set(group, zones[zone], color[0]);
                ayaneo_led_mc_set(group, zones[zone] + 1, color[1]);
                ayaneo_led_mc_set(group, zones[zone] + 2, color[2]);
        }
}

static void ayaneo_led_mc_off(void)
{
        ec_write_ram(AYANEO_LED_MC_ENABLE_ADDR, AYANEO_LED_MC_ENABLE_OFF);
        ec_write_ram(AYANEO_LED_MC_ADDR_CLOSE_1, 0x01);
        mdelay(AYANEO_LED_WRITE_DELAY_MS);
}

static void ayaneo_led_mc_on(void)
{
        ec_write_ram(AYANEO_LED_MC_ENABLE_ADDR, AYANEO_LED_MC_ENABLE_ON);
        ec_write_ram(AYANEO_LED_MC_ADDR_CLOSE_1, 0x01);
        mdelay(AYANEO_LED_WRITE_DELAY_MS);
}

static void ayaneo_led_mc_reset(void)
{
        ec_write_ram(AYANEO_LED_MC_ENABLE_ADDR, AYANEO_LED_MC_ENABLE_RESET);
        ec_write_ram(AYANEO_LED_MC_ADDR_CLOSE_1, 0x01);
        mdelay(AYANEO_LED_WRITE_DELAY_MS);
}

/* ACPI controller methods */
static void ayaneo_led_mc_legacy_set(u8 group, u8 pos, u8 brightness)
{
        if (!lock_global_acpi_lock())
                return;

        ec_write(AYANEO_LED_PWM_CONTROL, group);
        ec_write(AYANEO_LED_POS, pos);
        ec_write(AYANEO_LED_BRIGHTNESS, brightness);
        ec_write(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_WRITE);

        if (!unlock_global_acpi_lock())
                return;

        mdelay(AYANEO_LED_WRITE_DELAY_LEGACY_MS);

        if (!lock_global_acpi_lock())
                return;

        ec_write(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_HOLD);

        if (!unlock_global_acpi_lock())
                return;
}

static void ayaneo_led_mc_legacy_release(void)
{
        if (!lock_global_acpi_lock())
                return;

        ec_write(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_RELEASE);

        if (!unlock_global_acpi_lock())
                return;
}

static void ayaneo_led_mc_legacy_hold(void)
{
        if (!lock_global_acpi_lock())
                return;

        ec_write(AYANEO_LED_MODE_REG, AYANEO_LED_MODE_HOLD);

        if (!unlock_global_acpi_lock())
                return;
}

static void ayaneo_led_mc_legacy_intensity_single(u8 group, u8 *color, u8 zone)
{
        ayaneo_led_mc_legacy_set(group, zone, color[0]);
        ayaneo_led_mc_legacy_set(group, zone + 1, color[1]);
        ayaneo_led_mc_legacy_set(group, zone + 2, color[2]);
}

static void ayaneo_led_mc_legacy_intensity(u8 group, u8 *color, u8 zones[])
{
        int zone;

        for (zone = 0; zone < 4; zone++) {
                ayaneo_led_mc_legacy_intensity_single(group, color, zones[zone]);
        }

        // note: omit for aya flip when implemented, causes unexpected behavior
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

/* KUN doesn't use consistant zone mapping for RGB, adjust */
static void ayaneo_led_mc_legacy_intensity_kun(u8 group, u8 *color)
{
        u8 zone;
        u8 remap_color[3];

        if (group == AYANEO_LED_GROUP_BUTTON)
        {
                zone = 12;
                remap_color[0] = color[2];
                remap_color[1] = color[0];
                remap_color[2] = color[1];
                ayaneo_led_mc_legacy_intensity_single(AYANEO_LED_GROUP_BUTTON, remap_color, zone);
                ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
                return;
        }

        zone = 3;
        remap_color[0] = color[1];
        remap_color[1] = color[0];
        remap_color[2] = color[2];
        ayaneo_led_mc_legacy_intensity_single(group, remap_color, zone);

        zone = 6;
        remap_color[0] = color[1];
        remap_color[1] = color[2];
        remap_color[2] = color[0];
        ayaneo_led_mc_legacy_intensity_single(group, remap_color, zone);

        zone = 9;
        remap_color[0] = color[2];
        remap_color[1] = color[0];
        remap_color[2] = color[1];
        ayaneo_led_mc_legacy_intensity_single(group, remap_color, zone);

        zone = 12;
        remap_color[0] = color[2];
        remap_color[1] = color[1];
        remap_color[2] = color[0];
        ayaneo_led_mc_legacy_intensity_single(group, remap_color, zone);

        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

static void ayaneo_led_mc_legacy_off(void)
{
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_OFF);
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_OFF);

        // note: omit for aya flip when implemented, causes unexpected behavior
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

static void ayaneo_led_mc_legacy_on(void)
{
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_ON);
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_ON);

        // note: omit for aya flip when implemented, causes unexpected behavior
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

static void ayaneo_led_mc_legacy_reset(void)
{
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_RESET);
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_RESET);

        // note: omit for aya flip when implemented, causes unexpected behavior
        ayaneo_led_mc_legacy_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

/* Device command abstractions */
static void ayaneo_led_mc_take_control(void)
{
        switch (model) {
                case air:
                case air_1s:
                case air_1s_limited:
                case air_pro:
                case air_plus_mendo:
                case geek:
                case geek_1s:
                case ayaneo_2:
                case ayaneo_2s:
                case kun:
                        ayaneo_led_mc_legacy_hold();
                        ayaneo_led_mc_legacy_reset();
                        ayaneo_led_mc_legacy_off();
                        break;
                case air_plus:
                case slide:
                        ayaneo_led_mc_hold();
                        ayaneo_led_mc_reset();
                        ayaneo_led_mc_off();
                        break;
                default:
                        break;
                }
}

static void ayaneo_led_mc_release_control(void)
{
        switch (model) {
                case air:
                case air_1s:
                case air_1s_limited:
                case air_pro:
                case air_plus_mendo:
                case geek:
                case geek_1s:
                case ayaneo_2:
                case ayaneo_2s:
                case kun:
                        ayaneo_led_mc_legacy_reset();
                        ayaneo_led_mc_legacy_release();
                        break;
                case air_plus:
                case slide:
                        ayaneo_led_mc_reset();
                        ayaneo_led_mc_release();
                        break;
                default:
                        break;
                }
}

/* Threaded writes:
# The writer thread's job is to push updates to the physical LEDs as fast as
# possible while allowing updates to the LED multi_intensity/brightness sysfs
# attributes to return quickly.
#
# During multi_intensity/brightness set, the ayaneo_led_mc_update_color array
# is updated with the target color and ayaneo_led_mc_update_required is
# incremented by 1.
#
# When the writer thread begins its next loop, it copies the current values of
# ayaneo_led_mc_update_required, and ayaneo_led_mc_update_color, after which
# the new color is pushed to the microcontroller. After the color has been
# pushed the writer thread subtracts the starting value from
# ayaneo_led_mc_update_required. If any updates were pushed to
# ayaneo_led_mc_update_required during the writes then the following iteration
# will immediately begin writing the new colors to the microcontroller,
# otherwise it'll sleep for a short while.
#
# Updates to ayaneo_led_mc_update_required and ayaneo_led_mc_update_color are
# syncronised by ayaneo_led_mc_update_lock to prevent a race condition between
# the writer thread and the brightness set function.
#
# During suspend the kthread_stop is called which causes the writer thread to
# terminate after its current iteration. Th writer thread is then restarted
# during resume to allow updates to continue.
*/
static struct task_struct *ayaneo_led_mc_writer_thread;
static int ayaneo_led_mc_update_required;
static u8 ayaneo_led_mc_update_color[3];
DEFINE_RWLOCK(ayaneo_led_mc_update_lock);

static void ayaneo_led_mc_scale_color(u8 *color, u8 max_value)
{
        for (int i = 0; i < 3; i++)
        {
                int c_color = (int)color[i] * (int)max_value / 255;

                // prevents left-right discrepancy when brightness/color are low
                if (c_color == 0 && color[i] > 0)
                        c_color = 1;

                color[i] = (u8)c_color;
        }
}

static void ayaneo_led_mc_brightness_apply(u8 *color)
{
        u8 color_l[3];
        u8 color_r[3];
        u8 color_b[3];

        u8 zones[4] = {3, 6, 9, 12};

        for (int i = 0; i < 3; i++)
        {
                color_l[i] = color[i];
                color_r[i] = color[i];
                color_b[i] = color[i];
        }

        ayaneo_led_mc_scale_color(color_l, 192);
        ayaneo_led_mc_scale_color(color_r, 192);
        ayaneo_led_mc_scale_color(color_b, 192);

        switch (model) {
                case air:
                case air_pro:
                case air_1s:
                        ayaneo_led_mc_scale_color(color_l, 69);
                        ayaneo_led_mc_legacy_on();
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_LEFT, color_l, zones);
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_RIGHT, color_r, zones);
                        break;
                case air_1s_limited:
                        ayaneo_led_mc_scale_color(color_r, 204);
                        ayaneo_led_mc_legacy_on();
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_LEFT, color_l, zones);
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_RIGHT, color_r, zones);
                        break;
                case geek:
                case geek_1s:
                case ayaneo_2:
                case ayaneo_2s:
                        ayaneo_led_mc_legacy_on();
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_LEFT, color_l, zones);
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_RIGHT, color_r, zones);
                        break;
                case air_plus_mendo:
                        ayaneo_led_mc_scale_color(color_l, 64);
                        ayaneo_led_mc_scale_color(color_r, 32);
                        ayaneo_led_mc_legacy_on();
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_LEFT, color_l, zones);
                        ayaneo_led_mc_legacy_intensity(AYANEO_LED_GROUP_RIGHT, color_r, zones);
                        break;
                case air_plus:
                        ayaneo_led_mc_scale_color(color_l, 64);
                        ayaneo_led_mc_scale_color(color_r, 32);
                        ayaneo_led_mc_on();
                        ayaneo_led_mc_intensity(AYANEO_LED_GROUP_LEFT, color_l, zones);
                        ayaneo_led_mc_intensity(AYANEO_LED_GROUP_RIGHT, color_r, zones);
                        break;
                case slide:
                        ayaneo_led_mc_on();
                        ayaneo_led_mc_intensity(AYANEO_LED_GROUP_LEFT, color_l, zones);
                        ayaneo_led_mc_intensity(AYANEO_LED_GROUP_RIGHT, color_r, zones);
                        break;
                case kun:
                        ayaneo_led_mc_legacy_on();
                        ayaneo_led_mc_legacy_intensity_kun(AYANEO_LED_GROUP_LEFT, color_l);
                        ayaneo_led_mc_legacy_intensity_kun(AYANEO_LED_GROUP_RIGHT, color_r);
                        ayaneo_led_mc_legacy_intensity_kun(AYANEO_LED_GROUP_BUTTON, color_b);
                        break;
                default:
                        break;
        }
}

int ayaneo_led_mc_writer(void *pv)
{
        pr_info("Writer thread started.\n");
        int count;
        u8 color[3];

        while (!kthread_should_stop())
        {
                read_lock(&ayaneo_led_mc_update_lock);
                count = ayaneo_led_mc_update_required;

                if (count)
                {
                        color[0] = ayaneo_led_mc_update_color[0];
                        color[1] = ayaneo_led_mc_update_color[1];
                        color[2] = ayaneo_led_mc_update_color[2];
                }
                read_unlock(&ayaneo_led_mc_update_lock);

                if (count)
                {
                        ayaneo_led_mc_brightness_apply(color);

                        write_lock(&ayaneo_led_mc_update_lock);
                        ayaneo_led_mc_update_required -= count;
                        write_unlock(&ayaneo_led_mc_update_lock);
                }
                else
                        usleep_range(AYANEO_LED_WRITER_DELAY_RANGE_US);
        }

        pr_info("Writer thread stopped.\n");
        return 0;
}

/* RGB LED Logic */
static void ayaneo_led_mc_brightness_set(struct led_classdev *led_cdev,
                                      enum led_brightness brightness)
{
        struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(led_cdev);
        int val;
        int i;
        struct mc_subled s_led;

        if (brightness < 0 || brightness > 255)
                return;

        led_cdev->brightness = brightness;

        write_lock(&ayaneo_led_mc_update_lock);
        for (i = 0; i < mc_cdev->num_colors; i++) {
                s_led = mc_cdev->subled_info[i];
                if (s_led.intensity < 0 || s_led.intensity > 255)
                        return;
                val = brightness * s_led.intensity / led_cdev->max_brightness;
                ayaneo_led_mc_update_color[s_led.channel] = val;
        }
        ayaneo_led_mc_update_required++;
        write_unlock(&ayaneo_led_mc_update_lock);
};

static enum led_brightness ayaneo_led_mc_brightness_get(struct led_classdev *led_cdev)
{
        return led_cdev->brightness;
};

/* Suspend Mode
# Multiple modes of operation are supported during suspend:
#
# OEM:  Retains the default behavior of the device by returning control to the
#       microcontroller. On most devices the LEDs will flash periodically, and
#       will turn red when charging.
# Off:  The LEDs are turned off during suspend, and control of the LEDs is
#       retained by the driver. On most devices, charging will not turn on the
#       LEDs. During resume, the last color set is restored.
# Keep: The color currently set on the LEDs remains unchanged, and control of
#       the LEDs is retained by the driver. On most devices, charging will not
#       change the color of the LEDs.
*/
static ssize_t suspend_mode_show(struct device *dev, struct device_attribute *attr,
                                 char *buf)
{
        bool active;
        ssize_t count = 0;
        int i;

        for (i = 0; i <  ARRAY_SIZE(AYANEO_LED_SUSPEND_MODE_TEXT); i++) {
                active = i == suspend_mode;

                if (active) {
                        count += sysfs_emit_at(buf, count, "[%s] ",
                                   AYANEO_LED_SUSPEND_MODE_TEXT[i]);
                }
                else {
                        count += sysfs_emit_at(buf, count, "%s ",
                                   AYANEO_LED_SUSPEND_MODE_TEXT[i]);
                }
        }

        if (count)
                buf[count - 1] = '\n';

        return count;
}

static ssize_t suspend_mode_store(struct device *dev, struct device_attribute *attr,
                               const char *buf, size_t count)
{
        int res = sysfs_match_string(AYANEO_LED_SUSPEND_MODE_TEXT, buf);

        if (res < 0)
                return -EINVAL;

        suspend_mode = res;

        return count;
}

static DEVICE_ATTR_RW(suspend_mode);

static struct attribute *ayaneo_led_mc_attrs[] = {
        &dev_attr_suspend_mode.attr,
        NULL,
};

ATTRIBUTE_GROUPS(ayaneo_led_mc);

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
        ayaneo_led_mc_take_control();

        write_lock(&ayaneo_led_mc_update_lock);
        ayaneo_led_mc_update_required++;
        write_unlock(&ayaneo_led_mc_update_lock);

        ayaneo_led_mc_writer_thread = kthread_run(ayaneo_led_mc_writer,
                                                  NULL,
                                                  "ayaneo-platform led writer");
        return 0;
}

static int ayaneo_platform_suspend(struct platform_device *pdev, pm_message_t state)
{
        kthread_stop(ayaneo_led_mc_writer_thread);

        switch (suspend_mode)
        {
        case AYANEO_LED_SUSPEND_MODE_OEM:
                ayaneo_led_mc_release_control();
                break;

        case AYANEO_LED_SUSPEND_MODE_KEEP:
                // Nothing to do.
                break;

        case AYANEO_LED_SUSPEND_MODE_OFF:
                ayaneo_led_mc_take_control();
                break;

        default:
                break;
        }
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
        if (ret)
                return ret;

        ret = devm_device_add_groups(ayaneo_led_mc.led_cdev.dev, ayaneo_led_mc_groups);
        return ret;
}

static struct platform_driver ayaneo_platform_driver = {
        .driver = {
                .name = "ayaneo-platform",
        },
        .probe = ayaneo_platform_probe,
        .resume = ayaneo_platform_resume,
        .suspend = ayaneo_platform_suspend,
};

static struct platform_device *ayaneo_platform_device;

static int __init ayaneo_platform_init(void)
{
        int ret;
        ayaneo_platform_device = platform_create_bundle(&ayaneo_platform_driver,
                                        ayaneo_platform_probe, NULL, 0, NULL, 0);
        ret = PTR_ERR_OR_ZERO(ayaneo_platform_device);
        if (ret)
                return ret;

        ayaneo_led_mc_writer_thread = kthread_run(ayaneo_led_mc_writer,
                                                  NULL,
                                                  "ayaneo-platform led writer");

        if (!ayaneo_led_mc_writer_thread)
        {
                pr_err("Failed to start writer thread.\n");
                platform_device_unregister(ayaneo_platform_device);
                return -1;
        }

        return 0;
}

static void __exit ayaneo_platform_exit(void)
{
        kthread_stop(ayaneo_led_mc_writer_thread);
        platform_device_unregister(ayaneo_platform_device);
        platform_driver_unregister(&ayaneo_platform_driver);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

module_init(ayaneo_platform_init);
module_exit(ayaneo_platform_exit);

MODULE_AUTHOR("Derek John Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Platform driver that handles EC sensors of AYANEO x86 devices");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for AYANEO x86 Handhelds that exposes RGB LED
 * control via a sysfs led_class_multicolor interface.
 *
 * Copyright (C) 2023-2024 Derek J. Clark <derekjohn.clark@gmail.com>
 * Copyright (C) 2023-2024 JELOS <https://github.com/JustEnoughLinuxOS>
 * Copyright (C) 2024, 2025 Sebastian Kranz <https://github.com/Lightwars>
 * Copyright (C) 2024 Trevor Heslop <https://github.com/SytheZN>
 * Derived from ayaled originally developed by Maya Matuszczyk
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
#include <acpi/battery.h>

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

/* RGB LED EC Ram Registers
 * #define AYANEO_LED_MC_L_Q1_R     0xb3
 * #define AYANEO_LED_MC_L_Q1_G     0xb4
 * #define AYANEO_LED_MC_L_Q1_B     0xb5
 * #define AYANEO_LED_MC_L_Q2_R     0xb6
 * #define AYANEO_LED_MC_L_Q2_G     0xb7
 * #define AYANEO_LED_MC_L_Q2_B     0xb8
 * #define AYANEO_LED_MC_L_Q3_R     0xb9
 * #define AYANEO_LED_MC_L_Q3_G     0xba
 * #define AYANEO_LED_MC_L_Q3_B     0xbb
 * #define AYANEO_LED_MC_L_Q4_R     0xbc
 * #define AYANEO_LED_MC_L_Q4_G     0xbd
 * #define AYANEO_LED_MC_L_Q4_B     0xbe
 * #define AYANEO_LED_MC_R_Q1_R     0x73
 * #define AYANEO_LED_MC_R_Q1_G     0x74
 * #define AYANEO_LED_MC_R_Q1_B     0x75
 * #define AYANEO_LED_MC_R_Q2_R     0x76
 * #define AYANEO_LED_MC_R_Q2_G     0x77
 * #define AYANEO_LED_MC_R_Q2_B     0x78
 * #define AYANEO_LED_MC_R_Q3_R     0x79
 * #define AYANEO_LED_MC_R_Q3_G     0x7a
 * #define AYANEO_LED_MC_R_Q3_B     0x7b
 * #define AYANEO_LED_MC_R_Q4_R     0x7c
 * #define AYANEO_LED_MC_R_Q4_G     0x7d
 * #define AYANEO_LED_MC_R_Q4_B     0x7e
 */

#define AYANEO_LED_MC_ADDR_L          0xb0
#define AYANEO_LED_MC_ADDR_R          0x70

#define AYANEO_LED_MC_ADDR_CLOSE_1    0x86
#define AYANEO_LED_MC_ADDR_CLOSE_2    0xc6

#define AYANEO_LED_MC_MODE_ADDR       0x87
#define AYANEO_LED_MC_MODE_HOLD       0xa5
#define AYANEO_LED_MC_MODE_RELEASE    0x00

/* Bypass Charge EC Ram Registers */
#define AYANEO_BYPASSCHARGE_CONTROL 0xd1
#define AYANEO_BYPASSCHARGE_OPEN    0x01
#define AYANEO_BYPASSCHARGE_CLOSE   0x65

/* Schema:
 *
 * 0x6d - LED PWM control (0x03)
 *
 * 0xb1 - Support for 4 zones and RGB color
 *   Colors: Red (1), Green (2), Blue (3)
 *   Zones:  Right (2), Down (5), Left (8) , Up (11)
 *   Off: Set 0xb1 to 02
 *
 * 0xb2 - Brightness [0-255]. Left/Right produce different brightness for
 *   the same value on different models, must be scaled. Requires b1
 *   to be set at the same time.
 *
 * 0xbf - Set Mode
 *   Modes: Enable (0x10), Tint (0xe2), Close (0xff)
 */

/* EC Controlled RGB registers */
#define AYANEO_LED_PWM_CONTROL        0x6d
#define AYANEO_LED_POS                0xb1
#define AYANEO_LED_BRIGHTNESS         0xb2
#define AYANEO_LED_MODE_REG           0xbf

#define AYANEO_LED_CMD_ENABLE_ADDR    0x02
#define AYANEO_LED_CMD_ENABLE_ON      0xb1
#define AYANEO_LED_CMD_ENABLE_OFF     0x31
#define AYANEO_LED_CMD_ENABLE_RESET   0xc0

#define AYANEO_LED_CMD_PATTERN_ADDR   0x0f
#define AYANEO_LED_CMD_PATTERN_OFF    0x00

#define AYANEO_LED_CMD_FADE_ADDR      0x10
#define AYANEO_LED_CMD_FADE_OFF       0x00

#define AYANEO_LED_CMD_WATCHDOG_ADDR  0x15
#define AYANEO_LED_CMD_WATCHDOG_ON    0x07

#define AYANEO_LED_CMD_ANIM_1_ADDR    0x11 /* Animation step 1 */
#define AYANEO_LED_CMD_ANIM_2_ADDR    0x12 /* Animation step 2 */
#define AYANEO_LED_CMD_ANIM_3_ADDR    0x13 /* Animation step 3 */
#define AYANEO_LED_CMD_ANIM_4_ADDR    0x14 /* Animation step 4 */
#define AYANEO_LED_CMD_ANIM_STATIC    0x05

/* RGB Mode values */
#define AYANEO_LED_MODE_RELEASE       0x00 /* close channel, release control */
#define AYANEO_LED_MODE_WRITE         0x10 /* Default write mode */
#define AYANEO_LED_MODE_HOLD          0xfe /* close channel, hold control */

#define AYANEO_LED_GROUP_LEFT         0x01
#define AYANEO_LED_GROUP_RIGHT        0x02
#define AYANEO_LED_GROUP_LEFT_RIGHT   0x03 /* omit for aya flip when implemented */
#define AYANEO_LED_GROUP_BUTTON       0x04

#define AYANEO_LED_WRITE_DELAY_LEGACY_MS        2
#define AYANEO_LED_WRITE_DELAY_MS               1
#define AYANEO_LED_WRITER_DELAY_RANGE_US        10000, 20000
#define AYANEO_LED_SUSPEND_RESUME_DELAY_MS      100

/* EC Controlled Bypass Charge Register */
#define AYANEO_BYPASS_CHARGE_CONTROL  0x1e
#define AYANEO_BYPASS_CHARGE_OPEN     0x55
#define AYANEO_BYPASS_CHARGE_CLOSE    0xaa
#define AYANEO_BYPASS_WRITER_DELAY_MS 30000
#define AYANEO_EC_VERSION_REG 0x00 /* until 0x04 */

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

static int ec_read_ram(u8 index, u8 *val)
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
        *val = inb(AYANEO_DATA_PORT);

        if (!unlock_global_acpi_lock())
                return -EBUSY;

        return ret;
}

/* Function Summary
 * AYANEO devices can be largely divided into 2 groups; modern and legacy.
 *   - Legacy devices use a microcontroller either embedded into or controlled via
 *     the system's ACPI controller.
 *   - Modern devices use a dedicated microcontroller and communicate via shared
 *     memory.
 *
 * The control scheme is largely shared between both device types and many of
 * the command values are shared.
 *
 * ayaneo_led_mc_set / ayaneo_led_mc_legacy_set
 *       Sets the value of a single address or subpixel
 *
 * ayaneo_led_mc_release / ayaneo_led_mc_legacy_release
 *       Releases control of the LEDs back to the microcontroller.
 *       This function is abstracted by ayaneo_led_mc_release_control.
 *
 * ayaneo_led_mc_hold / ayaneo_led_mc_legacy_hold
 *       Takes and holds control of the LEDs from the microcontroller.
 *       This function is abstracted by ayaneo_led_mc_take_control.
 *
 * ayaneo_led_mc_intensity / ayaneo_led_mc_legacy_intensity
 *       Sets the values of all of the LEDs in the zones of a given group.
 *
 * ayaneo_led_mc_off / ayaneo_led_mc_legacy_off
 *       Instructs the microcontroller to disable output for the given group.
 *
 * ayaneo_led_mc_on / ayaneo_led_mc_legacy_on
 *       Instructs the microcontroller to enable output for the given group.
 *
 * ayaneo_led_mc_reset / ayaneo_led_mc_legacy_reset
 *       Reverts all of the microcontroller internal registers to power on
 *       defaults.
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

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

static void ayaneo_led_mc_intensity(u8 group, u8 *color, u8 zones[])
{
        int zone;

        for (zone = 0; zone < 4; zone++) {
                ayaneo_led_mc_set(group, zones[zone], color[0]);
                ayaneo_led_mc_set(group, zones[zone] + 1, color[1]);
                ayaneo_led_mc_set(group, zones[zone] + 2, color[2]);
        }

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

static void ayaneo_led_mc_off(void)
{
        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_OFF);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_OFF);

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

static void ayaneo_led_mc_on(void)
{
        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_ON);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_ON);

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_PATTERN_ADDR, AYANEO_LED_CMD_PATTERN_OFF);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_PATTERN_ADDR, AYANEO_LED_CMD_PATTERN_OFF);

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_FADE_ADDR, AYANEO_LED_CMD_FADE_OFF);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_FADE_ADDR, AYANEO_LED_CMD_FADE_OFF);

        /* Set static color animation */
        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ANIM_1_ADDR, AYANEO_LED_CMD_ANIM_STATIC);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ANIM_1_ADDR, AYANEO_LED_CMD_ANIM_STATIC);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ANIM_2_ADDR, AYANEO_LED_CMD_ANIM_STATIC);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ANIM_2_ADDR, AYANEO_LED_CMD_ANIM_STATIC);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ANIM_3_ADDR, AYANEO_LED_CMD_ANIM_STATIC);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ANIM_3_ADDR, AYANEO_LED_CMD_ANIM_STATIC);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ANIM_4_ADDR, AYANEO_LED_CMD_ANIM_STATIC);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ANIM_4_ADDR, AYANEO_LED_CMD_ANIM_STATIC);

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_WATCHDOG_ADDR, AYANEO_LED_CMD_WATCHDOG_ON);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_WATCHDOG_ADDR, AYANEO_LED_CMD_WATCHDOG_ON);

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
}

static void ayaneo_led_mc_reset(void)
{
        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_RESET);
        ayaneo_led_mc_set(AYANEO_LED_GROUP_RIGHT,
                AYANEO_LED_CMD_ENABLE_ADDR, AYANEO_LED_CMD_ENABLE_RESET);

        ayaneo_led_mc_set(AYANEO_LED_GROUP_LEFT_RIGHT, 0x00, 0x00);
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
 *  The writer thread's job is to push updates to the physical LEDs as fast as
 *  possible while allowing updates to the LED multi_intensity/brightness sysfs
 *  attributes to return quickly.
 *
 *  During multi_intensity/brightness set, the ayaneo_led_mc_update_color array
 *  is updated with the target color and ayaneo_led_mc_update_required is
 *  incremented by 1.
 *
 *  When the writer thread begins its next loop, it copies the current values of
 *  ayaneo_led_mc_update_required, and ayaneo_led_mc_update_color, after which
 *  the new color is pushed to the microcontroller. After the color has been
 *  pushed the writer thread subtracts the starting value from
 *  ayaneo_led_mc_update_required. If any updates were pushed to
 *  ayaneo_led_mc_update_required during the writes then the following iteration
 *  will immediately begin writing the new colors to the microcontroller,
 *  otherwise it'll sleep for a short while.
 *
 *  Updates to ayaneo_led_mc_update_required and ayaneo_led_mc_update_color are
 *  syncronised by ayaneo_led_mc_update_lock to prevent a race condition between
 *  the writer thread and the brightness set function.
 *
 *  During suspend kthread_stop is called which causes the writer thread to
 *  terminate after its current iteration. The writer thread is restarted during
 *  resume to allow updates to continue.
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
        u8 color_l[3]; /* Left joystick ring */
        u8 color_r[3]; /* Right joystick ring */
        u8 color_b[3]; /* AyaSpace Button (KUN Only) */

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

int ayaneo_led_mc_writer(void *pv);
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
        NULL,
};

static struct attribute_group ayaneo_led_mc_group = {
      .attrs = ayaneo_led_mc_attrs,
};

static void suspend_mode_register_attr(void)
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
                case air_plus:
                case slide:
                        ayaneo_led_mc_attrs[0] = &dev_attr_suspend_mode.attr;
                        break;
                default:
                        break;
        }
}

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
                .name = "ayaneo:rgb:joystick_rings",
                .brightness = 0,
                .max_brightness = 255,
                .brightness_set = ayaneo_led_mc_brightness_set,
                .brightness_get = ayaneo_led_mc_brightness_get,
        },
        .num_colors = ARRAY_SIZE(ayaneo_led_mc_subled_info),
        .subled_info = ayaneo_led_mc_subled_info,
};

/* Handling bypass charge */
struct ayaneo_ps_priv {
    u8 charge_behaviour;
    u8 bypass_available;
};
static struct ayaneo_ps_priv ps_priv = { POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO, 0 };

static int ayaneo_psy_ext_get_prop(struct power_supply *psy, const struct power_supply_ext *ext,
                  void *data, enum power_supply_property psp, union power_supply_propval *val)
{
    if(psp == POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR) {
        val->intval = ps_priv.charge_behaviour;
        return 0;
    }
    return -EINVAL;
}

static int ayaneo_psy_ext_set_prop(struct power_supply *psy, const struct power_supply_ext *ext,
                  void *data, enum power_supply_property psp, const union power_supply_propval *val)
{
    if((psp == POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR) && ((val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) || (val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE))) {
        ps_priv.charge_behaviour = val->intval;
        return 0;
    }
    return -EINVAL;
}

static int ayaneo_psy_ext_is_writeable(struct power_supply *psy, const struct power_supply_ext *ext,
                  void *data, enum power_supply_property psp)
{
    if(psp == POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR) {
        return 1;
    } else {
        return -ENOENT;
    }
}

/* Function Summary
 * AYANEO devices can be largely divided into 2 groups; modern and legacy.
 *   - Legacy devices use a microcontroller either embedded into or controlled via
 *     the system's ACPI controller.
 *   - Modern devices use a dedicated microcontroller and communicate via shared
 *     memory.
 *
 * The control scheme is largely shared between both device types and many of
 * the command values are shared.
 *
 * ayaneo_bypass_charge_open / ayaneo_bypass_charge_legacy_open
 *       Bypasses the charging of the battery and supplies power directly to 
 *       the hardware.
 *
 * ayaneo_bypass_charge_close / ayaneo_bypass_charge_legacy_close
 *       Renable the charging of the battery.
 */
static void ayaneo_bypass_charge_open(void)
{
    u8 val;
    ec_read_ram(AYANEO_BYPASSCHARGE_CONTROL, &val);
    if(val != AYANEO_BYPASSCHARGE_OPEN) {
        ec_write_ram(AYANEO_BYPASSCHARGE_CONTROL, AYANEO_BYPASSCHARGE_OPEN);
    }
}

static void ayaneo_bypass_charge_close(void)
{
    u8 val;
    ec_read_ram(AYANEO_BYPASSCHARGE_CONTROL, &val);
    if(val != AYANEO_BYPASSCHARGE_CLOSE) {
        ec_write_ram(AYANEO_BYPASSCHARGE_CONTROL, AYANEO_BYPASSCHARGE_CLOSE);
    }
}

static void ayaneo_bypass_charge_legacy_open(void)
{
    u8 val;
        if (!lock_global_acpi_lock())
                return;

    if(ec_read(AYANEO_BYPASS_CHARGE_CONTROL, &val) == 0) {
            if(val != AYANEO_BYPASS_CHARGE_OPEN) {
                ec_write(AYANEO_BYPASS_CHARGE_CONTROL, AYANEO_BYPASS_CHARGE_OPEN);
        }
    }

        if (!unlock_global_acpi_lock())
                return;
}

static void ayaneo_bypass_charge_legacy_close(void)
{
    u8 val;
        if (!lock_global_acpi_lock())
                return;

    if(ec_read(AYANEO_BYPASS_CHARGE_CONTROL, &val) == 0) {
            if(val != AYANEO_BYPASS_CHARGE_CLOSE) {
                ec_write(AYANEO_BYPASS_CHARGE_CONTROL, AYANEO_BYPASS_CHARGE_CLOSE);
            }
    }

        if (!unlock_global_acpi_lock())
                return;
}
/* Threaded writes:
 *  The writer thread's job is to enable or disable the bypass charge function
 *  depending on the POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR.
 *
 *  When the writer thread begins its next loop, it checks if it should activate
 *  the bypass charge if POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE is set. It
 *  then saves the state of charge behaviour and sleeps for some seconds.
 *
 *  During suspend kthread_stop is called which causes the writer thread to
 *  terminate after its current iteration. The writer thread is restarted during
 *  resume to allow updates to continue.
 */
static struct task_struct *ayaneo_bypass_charge_writer_thread;
int ayaneo_bypass_charge_writer(void *pv);
int ayaneo_bypass_charge_writer(void *pv)
{
        u8 last_charge_behaviour = 0xff;
        pr_info("Bypass-Writer thread started.\n");

        while (!kthread_should_stop())
        {
            if(last_charge_behaviour != ps_priv.charge_behaviour) {
                if (POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE == ps_priv.charge_behaviour){
                    switch (model) {
                        case air:
                        case air_1s:
                        case air_1s_limited:
                        case air_pro:
                        case air_plus_mendo:
                        case geek_1s:
                        case ayaneo_2s:
                        case kun:
                                ayaneo_bypass_charge_legacy_open();
                                break;
                        case air_plus:
                        case slide:
                                ayaneo_bypass_charge_open();
                                break;
                        default:
                                break;
                    }
                } else if(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO == ps_priv.charge_behaviour) {
                    switch (model) {
                        case air:
                        case air_1s:
                        case air_1s_limited:
                        case air_pro:
                        case air_plus_mendo:
                        case geek_1s:
                        case ayaneo_2s:
                        case kun:
                                ayaneo_bypass_charge_legacy_close();
                                break;
                        case air_plus:
                        case slide:
                                ayaneo_bypass_charge_close();
                                break;
                        default:
                                break;
                    }
                }
                last_charge_behaviour = ps_priv.charge_behaviour;
            }
            msleep(AYANEO_BYPASS_WRITER_DELAY_MS);
        }

        pr_info("Bypass-Writer thread stopped.\n");
        return 0;
}

static enum power_supply_property ayaneo_psy_ext_props[] = {
    POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
};

static struct power_supply_ext ayaneo_psy_ext = {
    .name = "ayaneo-bypass-charge",
    .charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) | BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE),
    .properties = ayaneo_psy_ext_props,
    .num_properties = ARRAY_SIZE(ayaneo_psy_ext_props),
    .get_property = ayaneo_psy_ext_get_prop,
    .set_property = ayaneo_psy_ext_set_prop,
    .property_is_writeable = ayaneo_psy_ext_is_writeable,
};

static int ayaneo_battery_add(struct power_supply *battery, struct acpi_battery_hook *hook)
{
    /* Ayaneo devices only have one battery. */
    if (strcmp(battery->desc->name, "BAT0") != 0 &&
        strcmp(battery->desc->name, "BAT1") != 0 &&
        strcmp(battery->desc->name, "BATC") != 0 &&
        strcmp(battery->desc->name, "BATT") != 0)
        return -ENODEV;

    return power_supply_register_extension(battery, &ayaneo_psy_ext, &battery->dev, NULL);
}

static int ayaneo_battery_remove(struct power_supply *battery, struct acpi_battery_hook *hook)
{
    power_supply_unregister_extension(battery, &ayaneo_psy_ext);
    return 0;
}

static struct acpi_battery_hook battery_hook = {
    .add_battery = ayaneo_battery_add,
    .remove_battery = ayaneo_battery_remove,
    .name = "Ayaneo Battery",
};

/* check the ec version of devices which can handle the bypass charge function */
static int ayaneo_check_charge_control(void)
{
#define VERSION_LENGTH 5
        int ret;
        u8 version[VERSION_LENGTH];
        u8 version_needed[VERSION_LENGTH];
        u8 version_length = VERSION_LENGTH;
        int index;
        for(index = 0; index < VERSION_LENGTH; index++) {
            ec_read(AYANEO_EC_VERSION_REG + index, &version[index]);
        }
        switch (model) {
            case air:
            case air_pro:
                    version_needed[0] = 3;
                    version_needed[1] = 1;
                    version_needed[2] = 0;
                    version_needed[3] = 4;
                    version_needed[4] = 78;
                    break;
            case air_1s:
            case air_1s_limited:
                    version_needed[0] = 8;
                    version_needed[1] = 4;
                    version_needed[2] = 0;
                    version_needed[3] = 0;
                    version_needed[4] = 27;
                    break;
            case air_plus_mendo:
                    version_needed[0] = 7;
                    version_needed[1] = 0;
                    version_needed[2] = 0;
                    version_needed[3] = 0;
                    version_needed[4] = 13;
                    break;
            case ayaneo_2s:
            case geek_1s:
                    version_needed[0] = 8;
                    version_needed[1] = 0;
                    version_needed[2] = 0;
                    version_needed[3] = 1;
                    version_needed[4] = 10;
                    break;
            case kun:
                    version_needed[0] = 8;
                    version_needed[1] = 3;
                    version_needed[2] = 0;
                    version_needed[3] = 0;
                    version_needed[4] = 63;
                    break;
            case air_plus:
            case slide:
                    version_needed[0] = 0;
                    version_needed[1] = 0x1b;
                    version_needed[2] = 0;
                    version_needed[3] = 0;
                    version_needed[4] = 0;
                    version_length = 2;
                    break;
            default:
                    return -1;
                    break;
        }
        ret = memcmp(version, version_needed, version_length);
        return ret;
}

static int ayaneo_platform_resume(struct platform_device *pdev)
{
        ayaneo_led_mc_take_control();

        /* Re-apply last color */
        write_lock(&ayaneo_led_mc_update_lock);
        ayaneo_led_mc_update_required++;
        write_unlock(&ayaneo_led_mc_update_lock);

        /* Allow the MCU to sync with the new state */
        msleep(AYANEO_LED_SUSPEND_RESUME_DELAY_MS);

        ayaneo_led_mc_writer_thread = kthread_run(ayaneo_led_mc_writer,
                                                  NULL,
                                                  "ayaneo-platform led writer");
        if(ps_priv.bypass_available) {
            ayaneo_bypass_charge_writer_thread = kthread_run(ayaneo_bypass_charge_writer,
                                                  NULL,
                                                  "ayaneo-platform bypass charge writer");
        }
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

        /* Allow the MCU to sync with the new state */
        msleep(AYANEO_LED_SUSPEND_RESUME_DELAY_MS);

        if(ps_priv.bypass_available) {
            kthread_stop(ayaneo_bypass_charge_writer_thread);
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
        suspend_mode_register_attr();
        ayaneo_led_mc_take_control();

        ret = devm_led_classdev_multicolor_register(dev, &ayaneo_led_mc);
        if (ret)
                return ret;

        ret = devm_device_add_group(ayaneo_led_mc.led_cdev.dev, &ayaneo_led_mc_group);
        if (ret)
                return ret;

        if(ayaneo_check_charge_control() >= 0) {
            ps_priv.bypass_available = 1;
        }
        return ret;
}

static void ayaneo_platform_shutdown(struct platform_device *pdev)
{
        kthread_stop(ayaneo_led_mc_writer_thread);
        ayaneo_led_mc_release_control();
        if(ps_priv.bypass_available) {
            kthread_stop(ayaneo_bypass_charge_writer_thread);
        }
}

static void ayaneo_platform_remove(struct platform_device *pdev)
{
        kthread_stop(ayaneo_led_mc_writer_thread);
        ayaneo_led_mc_release_control();
        if(ps_priv.bypass_available) {
            kthread_stop(ayaneo_bypass_charge_writer_thread);
        }
}

static struct platform_driver ayaneo_platform_driver = {
        .driver = {
                .name = "ayaneo-platform",
        },
        .probe = ayaneo_platform_probe,
        .resume = ayaneo_platform_resume,
        .suspend = ayaneo_platform_suspend,
        .shutdown = ayaneo_platform_shutdown,
        .remove = ayaneo_platform_remove,
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

        if(ps_priv.bypass_available) {
            battery_hook_register(&battery_hook);
            ayaneo_bypass_charge_writer_thread = kthread_run(ayaneo_bypass_charge_writer,
                                                  NULL,
                                                  "ayaneo-platform bypass charge writer");

            if (!ayaneo_bypass_charge_writer_thread)
            {
                    pr_err("Failed to start ps writer thread.\n");
                    platform_device_unregister(ayaneo_platform_device);
                    return -1;
            }
        }

        return 0;
}

static void __exit ayaneo_platform_exit(void)
{
        kthread_stop(ayaneo_led_mc_writer_thread);
        if(ps_priv.bypass_available) {
            kthread_stop(ayaneo_bypass_charge_writer_thread);
            battery_hook_unregister(&battery_hook);
        }
        platform_device_unregister(ayaneo_platform_device);
        platform_driver_unregister(&ayaneo_platform_driver);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

module_init(ayaneo_platform_init);
module_exit(ayaneo_platform_exit);

MODULE_AUTHOR("Derek John Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Platform driver that handles EC sensors of AYANEO x86 devices");
MODULE_LICENSE("GPL");

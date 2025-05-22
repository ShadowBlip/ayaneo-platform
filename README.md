# Platform driver for AYANEO x86 handhelds

This driver provides a sysfs interface for RGB control.
And also a sysfs interface for bypass charge.

Supported devices include:

 - AIR
 - AIR Pro
 - AIR Plus
 - AIR 1s
 - 2
 - 2S
 - GEEK
 - GEEK 1S
 - KUN
 - Slide

## Build

If you only want to build and test the module (you need headers for your
kernel):

```shell
$ git clone https://github.com/ShadowBlip/ayaneo-platform.git
$ cd ayaneo-platform
$ make
```

Then insert the module and check `dmesg`:
```shell
# insmod ayaneo-platform.ko
```

## Install

You'll need appropriate headers for your kernel and `dkms` package from your
distribution.

```shell
$ git clone https://github.com/ShadowBlip/ayaneo-platform.git
$ cd ayaneo-platform
$ make
# make dkms
```

## Usage

### RGB Control

LED color and brightness can be controlled via sysfs.

On most systems this will be mounted at `/sys/class/leds/ayaneo:rgb:joystick_rings/` and provides the following files:

#### `brightness`

Read/write.

Gets or sets the overall brightness of the LEDs. Accepts a value between 0 and `max_brightness`.

Defaults to 0.

#### `max_brightness`

Read only.

Gets the maximum brightness supported by the LEDs. On most systems this value is 255.

#### `multi_index`

Read only.

Gets the names of each of the channels supported by `multi_intensity`. On most systems this value will be "red green blue".

#### `multi_intensity`

Read/write.

Gets or sets the intensity of each of the channels. The expected value is list of numbers between 0 and 255 for each of the channels listed in `multi_index`.

Default is "0 0 0".

#### `suspend_mode`

Read/write.

Controls how the LEDs should behave during suspend. When reading, the currently selected option will be wrapped in square brackets `[ ]`.

|Value|Description|
|-|-|
|oem|The LEDs retain OEM behavior.On most devices, they will breathe white during suspend and breathe red when charging.|
|off|The LEDs are turned off during suspend. They do not respond to charging.|
|keep|The LEDs retain their current color during suspend. They do not respond to charging.|

Default is "oem".

#### Identifying and Setting Color Values

The current brightness and color can be retrieved as follows:
```shell
$ cat /sys/class/leds/ayaneo:rgb:joystick_rings/brightness
0
$ cat /sys/class/leds/ayaneo:rgb:joystick_rings/multi_intensity
0 0 0
```

New values can be set as follows:
```shell
$ echo "255" | sudo tee /sys/class/leds/ayaneo:rgb:joystick_rings/brightness
255
$ echo "255 0 128" | sudo tee /sys/class/leds/ayaneo:rgb:joystick_rings/multi_intensity
255 0 128
```

### Bypass Charge Control

Bypass Charge Control can be enabled, disabled, start and stop battery level can be set via sysfs.

On most systems this will be mounted/hooked at `/sys/class/power_supply/BAT*/` and provides the following files:

#### charge_type

Read/write.

Enables or disables the bypass charge. Accepts a value of "Standard", "Limit" and "Bypass".
When reading, the currently selected option will be wrapped in square brackets `[ ]`.

|Value|Description|
|-|-|
|"Standard"|Disables the bypass charge. The battery is normally charged.|
|"Limit"|Enables the bypass charge. Battery charging will depend only on the stop value.|
|"Bypass"|Enables the bypass charge. Battery charging will depend on the start and stop values.|

Default is "Standard"

#### charge_control_start_threshold

Read/write.

Gets or sets the battery percentage level, below which charging will begin. Valid values between 0 and 100 (percent).

Default is 100.

#### charge_control_end_threshold

Read/write.

Gets or sets the battery percentage level, on or above which charging will stop. Valid values between 0 and 100 (percent).

Default is 100.

#### Identifying and Setting Control Values

The current charge type and end threshold can be retrieved as follows:
```shell
$ cat /sys/class/power_supply/BAT*/charge_type
[Standard] Limit Bypass
$ cat /sys/class/power_supply/BAT*/charge_control_end_threshold
100
```

New values can be set as follows:
```shell
$ echo "Limit" | sudo tee /sys/class/power_supply/BAT*/charge_type
Limit
$ echo "80" | sudo tee /sys/class/power_supply/BAT*/charge_control_end_threshold
80
```

## Changing Startup Defaults
The platform driver is fully exposed over systemd udev. This can be used to write udev rules that set attributes at startup.

### Udev Attributes Tree
```shell
$ udevadm info --attribute-walk /sys/class/leds/ayaneo:rgb:joystick_rings

  looking at device '/devices/platform/ayaneo-platform/leds/ayaneo:rgb:joystick_rings':
    KERNEL=="ayaneo:rgb:joystick_rings"
    SUBSYSTEM=="leds"
    DRIVER==""
    ATTR{brightness}=="0"
    ATTR{max_brightness}=="255"
    ATTR{multi_index}=="red green blue"
    ATTR{multi_intensity}=="0 0 0"
    ATTR{power/control}=="auto"
    ATTR{power/runtime_active_time}=="0"
    ATTR{power/runtime_status}=="unsupported"
    ATTR{power/runtime_suspended_time}=="0"
    ATTR{suspend_mode}=="[oem] keep off"
    ATTR{trigger}=="[none] usb-gadget usb-host rc-feedback kbd-scrolllock kbd-numlock kbd-capslo>

  looking at parent device '/devices/platform/ayaneo-platform':
    KERNELS=="ayaneo-platform"
    SUBSYSTEMS=="platform"
    DRIVERS=="ayaneo-platform"
    ATTRS{driver_override}=="(null)"
    ATTRS{power/control}=="auto"
    ATTRS{power/runtime_active_time}=="0"
    ATTRS{power/runtime_status}=="unsupported"
    ATTRS{power/runtime_suspended_time}=="0"

  looking at parent device '/devices/platform':
    KERNELS=="platform"
    SUBSYSTEMS==""
    DRIVERS==""
    ATTRS{power/control}=="auto"
    ATTRS{power/runtime_active_time}=="0"
    ATTRS{power/runtime_status}=="unsupported"
    ATTRS{power/runtime_suspended_time}=="0"
```


### Creating a rule
You can store a udev rule as `/etc/udev/rules.d/##-rule_name.rules`

All rules will start like this:
`ACTION=="add|change", KERNEL=="ayaneo:rgb:joystick_rings", SUBSYSTEM=="leds"`

To define a default, add the attribute in the format `ATTR{name}=value`
Valid attributes are:
```
ATTR{brightness}=="[0-255]"
ATTR{multi_intensity}=="[0-255] [0-255] [0-255]"
ATTR{suspend_mode}=="[oem|keep|off]"
```

As en example, to set `suspend_mode`to `off`:
`ACTION=="add|change", KERNEL=="ayaneo:rgb:joystick_rings", SUBSYSTEM=="leds", ATTR{suspend_mode}="off"
`

to create the rule in one line:
```shell
$ echo 'ACTION=="add|change", KERNEL=="ayaneo:rgb:joystick_rings", SUBSYSTEM=="leds", ATTR{suspend_mode}="off"' | sudo tee /etc/udev/rules.d/99-led_suspend_mode.rules
```

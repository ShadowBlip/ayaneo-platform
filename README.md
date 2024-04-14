# Platform driver for AYANEO x86 handhelds

This driver provides a sysfs interface for RGB control.

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

On most systems this will be mounted at `/sys/class/leds/multicolor:chassis/` and provides the following files:

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
$ cat /sys/class/leds/multicolor:chassis/brightness
0
$ cat /sys/class/leds/multicolor:chassis/multi_intensity
0 0 0
```

New values can be set as follows:
```shell
$ echo "255" | sudo tee /sys/class/leds/multicolor:chassis/brightness
255
$ echo "255 0 128" | sudo tee /sys/class/leds/multicolor:chassis/multi_intensity
255 0 128
```

# Platform driver for AYANEO x86 handhelds

This driver provides a sysfs interface for RGB control.

Supported devices include:

 - AIR
 - AIR Pro
 - AIR Plus
 - 2
 - 2S
 - GEEK
 - DEEK 1S

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

#### Mode Setting

#### Identifying and Setting Color Values

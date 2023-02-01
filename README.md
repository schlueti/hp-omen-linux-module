HP Omen special feature control for Linux
-----------------------------------------

This is a version of the hp-wmi kernel module that implements some of the features of HP Omen Command Centre.

It's totally experimental right now, and could easily crash your machine. 

**USE AT YOUR OWN RISK**

Currently working:

- FourZone keyboard colour control (`/sys/devices/platforms/hp-wmi/rgb-zones/zone0[0-3]`)
- Omen hotkeys

## Installation

First at all, since Linux Kernel 5.16 the DKMS tree already contains: hp-omen-wmi-0.9

So to install this repo you need to force the installation so the files in `/sys/devices/platform/hp-wmi/rgb_zones/` are created.

1. sudo dkms install -m hp-omen-wmi/0.9 --force

2. Reboot

---------------------------------------------

If you using Kernel lower 5.16 follow the instructions below.

1. Install dkms and kernel headers if needed (already present on Ubuntu)

1. Run `sudo make install`

Module will be built and installed, and DKMS will manage rebuilding it on kernel updates.

## Usage

The module creates four files in `/sys/devices/platform/hp-wmi/rgb_zones/` named `zone00 - zone03`.

To change zone highlight color, just print hex colour value in RGB format to the respective file. e.g:

`sudo bash -c 'echo 00FFFF > /sys/devices/platform/hp-wmi/rgb_zones/zone00'` to get sky-blue zone 0.

Omen and other hotkeys are bound to regular X11 keysyms, use your chosen desktop's hotkey manager to assign them to functions like any other key.

## To do:

- [ ] FourZone brightness control
- [ ] Fan control 


# nBoot Boot Loader
###### Just another XNU bootloader

## How to build this module:

 1. Clone u-boot source code:

   ```git clone git://git.denx.de/u-boot.git```

 1. Add this project to the root of u-boot:

   ```bash
   cd u-boot
   git clone https://github.com/aBradleyUno/nBoot.git
   ```

 1. Add the following line to ```u-boot/Makefile``` afrer ```libs-y += env/```:

   ```libs-y += nBoot/```

 1. Go through the normal steps in building u-boot for your platform.

## How to use

This module adds a command called `bootxnu`.

`bootxnu` recieves 6 parameters, all of them in hexadecimal:
- `XNU_ADDR`: Address of the raw XNU kernel (support for im4p coming soon).
- `XNU_LEN`: Size of the XNU kernel.
- `RAMDISK_ADDR`: Address of the Ramdisk image.
- `RAMDISK_LEN`: Size of the ramdisk.
- `AFDT_ADDR`: Address of the Apple EmbeddedDeviceTrees binary (support for im4p coming soon).
- `AFDT_LEN`: Size of the EmbeddedDeviceTrees binary.

I recommend using this command with a script since it can be a little tricky to keep track of the addresses.

On the Pi 4 I use:
```
env set bootargs "debug=0x8 kextlog=0xfff cpus=1 rd=md0 maxmem=2048"
fatload mmc 0:1 0x3A000000 /mach
env set mach_size ${filesize}
fatload mmc 0:1 0x3AE00000 /BCM2837.afdt
env set afdt_size ${filesize}
fatload mmc 0:1 0x32000000 /ramdisk.dmg
env set rd_size ${filesize}
bootxnu 0x3A000000 ${mach_size} 0x32000000 ${rd_size} 0x3AE00000 ${afdt_size}
```

## Credits

Zhuowei Zhang ([@zhuowei](https://github.com/zhuowei)) really helped me throughout most of the process.   

This loader is based on Kristina Brooks's [xnu-uboot-arm32](https://github.com/christinaa/xnu-uboot-arm32) and Brian McKenzie's [darwin-loader](https://github.com/b-man/darwin-loader) projects.

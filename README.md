patchram
========

Broadcom PatchRAM DFU (Device Firmware Upgrade) utility for macOS.

Based on original dfu-tool & dfu-programmer for Linux and BrcmPatchRAM for macOS.

Supports the Intel HEX dfu file format (including zlib compressed).

NOTE: You will need to disable your bluetooth device for this tool to be able to access it.

## Usage

`patchram <vendorId hex> <productId hex> <firmware.dfu>`

## Example

`./patchram 0x0a5c 0x216f ./BCM20702A1_001.002.014.1443.1572_v5668.zhx`

This uses the USB DFU specification (http://www.usb.org/developers/docs/devclass_docs/DFU_1.1.pdf), to upload firmware into a DFU device.

## Credits
- [headkaze](https://github.com/headkaze) for writing the software, updating and maintaining it
- [RehabMan](https://github.com/RehabMan) for BrcmPatchRAM
- [the-darkvoid](https://github.com/the-darkvoid) for dfu-util-osx and BrcmPatchRAM

**Flashing firmware is dangerous and could render your device non-functional. Use this at your own risk!**

/*
 *  Released under "The GNU General Public License (GPL-2.0)"
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <IOKit/usb/IOUSBLib.h>
#include <iostream>
#include <fstream>

extern "C"
{
	extern void NSLog(CFStringRef format, ...);
	
	#include "hci.h"
	#include "usb_device.h"
	#include "intel_firmware.h"
}

bool uploadFirmware(unsigned short vendorId, unsigned short productId, CFMutableArrayRef instructions, int initialDelay, int preResetDelay, int postResetDelay, bool supportsHandshake)
{
	IOUSBDeviceInterface300** device = getDevice(vendorId, productId);
	
	if (device == NULL)
	{
		fprintf(stderr, "[%04x:%04x]: Failed to retrieve USB device\n", vendorId, productId);
		return false;
	}
	
#ifdef DEBUG
	printDeviceInfo(device);
#endif
	
	IOReturn kr = kIOReturnSuccess;
	
	kr = (*device)->USBDeviceOpen(device);
	
	if (kr != kIOReturnSuccess)
	{
		fprintf(stderr, "USBDeviceOpen failed (0x%08x)\n", kr);
		
		return false;
	}
	
	setConfiguration(device);
	
	//findInterfaces(device);
	
	IOUSBInterfaceInterface300** interface = findFirstInterface(device);
	
	if (interface != NULL)
	{
		kr = (*interface)->USBInterfaceOpen(interface);
		
		if (kr != kIOReturnSuccess)
		{
			fprintf(stderr, "USBInterfaceOpen failed (0x%08x)\n", kr);
			
			return false;
		}
		
		UInt8 intfIndex, intfClass, intfSubClass, intfProtocol;
		
		if ((*interface)->GetInterfaceNumber(interface, &intfIndex) != kIOReturnSuccess)
			return false;
		
		if ((*interface)->GetInterfaceClass(interface, &intfClass) != kIOReturnSuccess)
			return false;
		
		if ((*interface)->GetInterfaceSubClass(interface, &intfSubClass) != kIOReturnSuccess)
			return false;
		
		if ((*interface)->GetInterfaceProtocol(interface, &intfProtocol) != kIOReturnSuccess)
			return false;
		
#ifdef DEBUG
		printf("[%04x:%04x]: Interface %d (class %02x, subclass %02x, protocol %02x) located\n", vendorId, productId, intfIndex, intfClass, intfSubClass, intfProtocol);
#endif
		
		performUpgrade(interface, instructions, initialDelay, preResetDelay, postResetDelay, supportsHandshake);

		(*interface)->USBInterfaceClose(interface);
		(*interface)->Release(interface);
	}
	else
	{
		fprintf(stderr, "[%04x:%04x]:  Failed to locate interface\n", vendorId, productId);
	}
	
	(*device)->USBDeviceClose(device);
	(*device)->Release(device);
	
	return true;
}

int main(int argc, const char * argv[])
{
	printf("patchram, Broadcom PatchRAM DFU (Device Firmware Upgrade) utility for macOS.\n");
	printf("Based on original dfu-tool & dfu-programmer for Linux and BrcmPatchRAM for macOS.\n\n");
	
	if (argc != 4)
	{
		printf("Usage: patchram <vendorId hex> <productId hex> <firmware.dfu>\n");
		return -1;
	}
	
	// Parse device vendor & product
	UInt16 vendorId = strtoul(argv[1], NULL, 16);
	UInt16 productId = strtoul(argv[2], NULL, 16);
	UInt32 initialDelay = 100, preResetDelay = 250, postResetDelay = 100;
	bool useHandshake = supportsHandshake(vendorId, productId);
	
#ifdef DEBUG
	printf("[%04x:%04x]: initialDelay: %d preResetDelay: %d postResetDelay: %d useHandshake: %d\n", vendorId, productId, initialDelay, preResetDelay, postResetDelay, useHandshake);
#endif
	
	std::ifstream inFile(argv[3], std::ios_base::in | std::ios_base::binary);
	
	if (!inFile)
	{
		fprintf(stderr, "Error reading file '%s'\n", argv[3]);
		return 1;
	}
	
	std::filebuf *pbuf = inFile.rdbuf();
	std::size_t inBufferSize = pbuf->pubseekoff (0, std::ios_base::end);
	pbuf->pubseekpos(0);
	char *inBuffer = new char[inBufferSize];
	pbuf->sgetn(inBuffer, inBufferSize);
	inFile.close();
	
	char *ext = strrchr((char *)argv[3], '.');
	
	if (strcmp(ext, ".hex") == 0 || strcmp(ext, ".dfu") == 0)
	{
		CFMutableArrayRef instructions = parseFirmware((const UInt8*)inBuffer, (UInt32)inBufferSize, vendorId, productId);
		
		//NSLog(CFSTR("%@"), instructions);
		
#ifdef DEBUG
		printf("[%04x:%04x]: Initiating DFU for USB device\n", vendorId, productId);
#endif
		
		uploadFirmware(vendorId, productId, instructions, initialDelay, preResetDelay, postResetDelay, useHandshake);
		
		CFRelease(instructions);
	}
	else
	{
		char *outBuffer[BUFFER_SIZE];
		uint32_t outBufferSize;
		
		if (decompressFirmware(inBuffer, (void *)outBuffer, (uint32_t)inBufferSize, &outBufferSize))
		{
			CFMutableArrayRef instructions = parseFirmware((const UInt8*)outBuffer, outBufferSize, vendorId, productId);
			
			//NSLog(CFSTR("%@"), instructions);
			
#ifdef DEBUG
			printf("[%04x:%04x]: Initiating DFU for USB device\n", vendorId, productId);
#endif
			
			uploadFirmware(vendorId, productId, instructions, initialDelay, preResetDelay, postResetDelay, useHandshake);
			
			CFRelease(instructions);
		}
	}
	
	delete[] inBuffer;
	
	return 0;
}

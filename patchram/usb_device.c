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

#include "usb_device.h"

/*
 *  Create a device matching dictionary
 *
 *  vendorId  - USB vendor id to match
 *  productId - USB product id to match
 *
 *  returns CFDictionaryRef or NULL on error
 */
CFDictionaryRef getMatchingDictionary(const unsigned short vendorId, const unsigned short productId)
{
	// Create a matching dictionary for IOUSBDevice
	CFMutableDictionaryRef result = NULL;
	CFMutableDictionaryRef matchingDictionary = IOServiceMatching(kIOUSBDeviceClassName);
	CFNumberRef numVendor  = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &vendorId);
	CFNumberRef numProduct = CFNumberCreate(kCFAllocatorDefault, kCFNumberShortType, &productId);
	
	if (matchingDictionary != NULL && numVendor != NULL && numProduct != NULL)
	{
		CFDictionaryAddValue(matchingDictionary, CFSTR(kUSBVendorID), numVendor);
		CFDictionaryAddValue(matchingDictionary, CFSTR(kUSBProductID), numProduct);
		
		result = matchingDictionary;
	}
	
	if (numVendor != NULL)
		CFRelease(numVendor);
	
	if (numProduct != NULL)
		CFRelease(numProduct);
	
	// Dictionary was successfully constructed
	if (result != NULL)
		return result;
	
	// Failed to construct matching dictionary
	if (matchingDictionary != NULL)
		CFRelease(matchingDictionary);
	
	return NULL;
}

/*
 *  Retrieve a string for a specified string index from the USB device
 *
 *  device      - USB device pointer
 *  stringIndex - String index to retrieve
 *  output      - Output buffer
 *  len         - Output buffer len
 *
 *  returns true or false on error
 */
bool retrieveString(IOUSBDeviceInterface300** device, const unsigned char stringIndex, char* output, const int len)
{
	IOUSBDevRequest request;
	CFStringRef string;
	const UInt8 buf[512];
	
	// Perform a device request to read the string descriptor.
	request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
	request.bRequest = kUSBRqGetDescriptor;
	request.wValue = (kUSBStringDesc << 8) | stringIndex;
	request.wIndex = 0; // Language (Optionally 0x409 for US English)
	request.wLength = sizeof(buf);
	request.pData = (void *)buf;
	
	bzero((void *)buf, sizeof(buf));
	
	if ((*device)->DeviceRequest(device, &request) == kIOReturnSuccess)
	{
		int length;
		length = buf[0] - 2; // First byte is length (in bytes)
		
		// Convert from UTF-16 Little Endian
		string = CFStringCreateWithBytes(kCFAllocatorDefault,
										 &buf[2],
										 length,
										 kCFStringEncodingUTF16LE,
										 false);
		
		// To C String
		CFStringGetCString(string, output, len, kCFStringEncodingUTF8);
		CFRelease(string);
	}
	
	return false;
}

/*
 *  Find a matching IOService for a USB device
 *
 *  vendorId    - USB device vendor
 *  productId   - USB device product
 *
 *  returns io_service_t or 0 on error
 */
io_service_t findMatchingService(const unsigned short vendorId, const unsigned short productId)
{
	CFDictionaryRef matchingDictionary = getMatchingDictionary(vendorId, productId);
	
	if (matchingDictionary == NULL)
	{
		fprintf(stderr, "Failed to initialize device matching dictionary.\n");
		return 0;
	}
	
	return IOServiceGetMatchingService(kIOMasterPortDefault, matchingDictionary);
}

/*
 *  Obtain an USB device pointer for a USB device
 *
 *  vendorId    - USB device vendor
 *  productId   - USB device product
 *
 *  returns IOUSBDeviceInterface300** NULL or on error
 */
IOUSBDeviceInterface300** getDevice(const unsigned short vendorId, const unsigned short productId)
{
	SInt32 score;
	io_service_t service = findMatchingService(vendorId, productId);
	IOCFPlugInInterface** plugin;
	IOUSBDeviceInterface300** device = NULL;
	
	if (service == 0)
	{
		fprintf(stderr, "[%04x:%04x]: Failed to find matching service.\n", vendorId, productId);
		return NULL;
	}
	
	if (IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score) == kIOReturnSuccess)
		(*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300), (LPVOID)&device);
	
	return device;
}

/*
 *  Set the USB device configuration to the first available configuration
 *
 *  device      - USB device pointer
 *
 *  returns true or false on error
 */
bool setConfiguration(IOUSBDeviceInterface300** device)
{
	UInt8 numConfigs;
	IOUSBConfigurationDescriptorPtr config;
	
	if ((*device)->GetNumberOfConfigurations(device, &numConfigs) != kIOReturnSuccess)
	{
		fprintf(stderr, "Failed to get number of configurations\n.");
		return false;
	}
	
#ifdef DEBUG
	printf("USB device has %d configurations.\n", numConfigs);
#endif
	
	if ((*device)->GetConfigurationDescriptorPtr(device, 0, &config) != kIOReturnSuccess)
	{
		fprintf(stderr, "Failed to retrieve configuration descriptor at index 0.\n");
		return false;
	}
	
	return ((*device)->SetConfiguration(device, config->bConfigurationValue) == kIOReturnSuccess);
}

IOUSBInterfaceInterface300** findFirstInterface(IOUSBDeviceInterface300** device)
{
	SInt32 score = 0;
	IOUSBFindInterfaceRequest request;
	IOCFPlugInInterface **plugin = NULL;
	IOUSBInterfaceInterface300** interface = NULL;
	io_iterator_t iterator;
	io_service_t service;
	
	request.bInterfaceClass = kIOUSBFindInterfaceDontCare;
	request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
	request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
	request.bAlternateSetting = kIOUSBFindInterfaceDontCare;
	
	if ((*device)->CreateInterfaceIterator(device, &request, &iterator) != kIOReturnSuccess)
	{
		fprintf(stderr, "Failed to create interface iterator\n");
		return NULL;
	}
   
	while ((service = IOIteratorNext(iterator)) != 0)
	{
		if (IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score) == kIOReturnSuccess)
		{
			if ((*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID)&interface) == kIOReturnSuccess)
			{
				// Located interface
				(*plugin)->Release(plugin);
				break;
			}
			
			// Interface was not the one we were looking for
			interface = NULL;
			
			(*interface)->Release(interface);
			(*plugin)->Release(plugin);
		}
		
		IOObjectRelease(service);
	}
	
	IOObjectRelease(iterator);
	
	return interface;
}

/*
 *  Prints the USB device information
 *
 *  device      - USB device pointer
 *
 */
void printDeviceInfo(IOUSBDeviceInterface300** device)
{
	UInt16 vendorId = 0;
	UInt16 productId = 0;
	UInt16 releaseVersion = 0;
	
	char manufacturer[255];
	char product[255];
	char serial[255];
	
	(*device)->GetDeviceVendor(device, &vendorId);
	(*device)->GetDeviceProduct(device, &productId);
	(*device)->GetDeviceReleaseNumber(device, &releaseVersion);
	
	UInt8 stringIndex = 0;
	
	if ((*device)->USBGetManufacturerStringIndex(device, &stringIndex) == kIOReturnSuccess)
		retrieveString(device, stringIndex, manufacturer, sizeof(manufacturer));
	
	if ((*device)->USBGetProductStringIndex(device, &stringIndex) == kIOReturnSuccess)
		retrieveString(device, stringIndex, product, sizeof(product));
	
	if ((*device)->USBGetSerialNumberStringIndex(device, &stringIndex) == kIOReturnSuccess)
		retrieveString(device, stringIndex, serial, sizeof(serial));
	
	printf("[%04x:%04x]: %s v%d \"%s\" by \"%s\"\n",
		   vendorId,
		   productId,
		   serial,
		   releaseVersion,
		   product,
		   manufacturer);
}

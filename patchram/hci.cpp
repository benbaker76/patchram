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

#include <mach/mach.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <stdio.h>
extern "C"
{
#include "hci.h"
}

#define FORCE_UPDATE	1
#define BUFFER_SIZE		0x200

static int hci_timeout = 5000;  // 5 seconds - default

const char *usb_dir[] =
{
	"out",
	"in",
	"none",
	"any"
};

const char *usb_ctrl[] =
{
	"control",
	"isoc",
	"bulk",
	"interrupt",
	"any"
};

static DeviceHskSupport hskSupport[] =
{
	{ 0x0a5c, 0x216f },
	{ 0x0a5c, 0x21ec },
	{ 0x0a5c, 0x6412 },
	{ 0x0a5c, 0x6414 },
	{ 0x0489, 0xe07a },
	{ 0x0,    0x0    }
};

struct bcm_subver_table
{
	uint16_t subver;
	const char *name;
};

static const struct bcm_subver_table bcm_usb_subver_table[] =
{
	{ 0x2105, "BCM20703A1"	},	/* 001.001.005 */
	{ 0x210b, "BCM43142A0"	},	/* 001.001.011 */
	{ 0x2112, "BCM4314A0"	},	/* 001.001.018 */
	{ 0x2118, "BCM20702A0"	},	/* 001.001.024 */
	{ 0x2126, "BCM4335A0"	},	/* 001.001.038 */
	{ 0x220e, "BCM20702A1"	},	/* 001.002.014 */
	{ 0x230f, "BCM4356A2"	},	/* 001.003.015 */
	{ 0x4106, "BCM4335B0"	},	/* 002.001.006 */
	{ 0x410e, "BCM20702B0"	},	/* 002.001.014 */
	{ 0x6109, "BCM4335C0"	},	/* 003.001.009 */
	{ 0x610c, "BCM4354"		},	/* 003.001.012 */
	{ 0x6607, "BCM4350C5"	},	/* 003.006.007 */
	{ }
};

// Standard HCI commands
uint8_t HCI_READ_LOCAL_VERSION[] = { 0x01, 0x10, 0x00 };
uint8_t HCI_READ_LOCAL_COMMANDS[] = { 0x02, 0x10, 0x00 };
uint8_t HCI_READ_FEATURES[] = { 0x03, 0x10, 0x00 };
uint8_t HCI_READ_LOCAL_FEATURES[] = { 0x04, 0x10, 0x00 };
uint8_t HCI_READ_LOCAL_NAME[] = { 0x14, 0x0c, 0x00 };
uint8_t HCI_RESET[] = { 0x03, 0x0c, 0x00 };

// Broadcom vendor specific commands

// Vendor Specific: Read chip-id and other Broadcom specific configuration variables
uint8_t HCI_VSC_READ_VERBOSE_CONFIG[] = { 0x79, 0xfc, 0x00 };

// Vendor Specific: Read controller features
uint8_t HCI_VSC_READ_CONTROLLER_FEATURES[] = { 0x6e, 0xfc, 0x00 };

// Vendor Specific: Read USB product
uint8_t HCI_VSC_READ_USB_PRODUCT[] = { 0x5a, 0xfc, 0x00 };

// Vendor Specific: Download mini driver
uint8_t HCI_VSC_DOWNLOAD_MINIDRIVER[] = { 0x2e, 0xfc, 0x00 };

// Vendor Specific: End of Record
uint8_t HCI_VSC_END_OF_RECORD[] = { 0x4e, 0xfc, 0x04, 0xff, 0xff, 0xff, 0xff };

// Vendor Specific: Wake up
uint8_t HCI_VSC_WAKEUP[] = { 0x53, 0xfc, 0x01, 0x13 };

#ifdef DEBUG
const char* getState(enum DeviceState deviceState)
{
	static const IONamedValue state_values[] = {
		{ kUnknown,            "Unknown"              },
		{ kPreInitialize,      "PreInitialize"        },
		{ kLocalVersion,       "Local version"        },
		{ kUSBProduct,         "USB product"          },
		{ kFirmwareVersion,    "Firmware version"     },
		{ kDownloadMiniDriver, "Download Mini-driver" },
		{ kMiniDriverComplete, "Mini-driver complete" },
		{ kInstructionWrite,   "Instruction write"    },
		{ kInstructionWritten, "Instruction written"  },
		{ kFirmwareWritten,    "Firmware written"     },
		{ kResetWrite,         "Reset write"          },
		{ kResetComplete,      "Reset complete"       },
		{ kUpdateComplete,     "Update complete"      },
		{ kUpdateNotNeeded,    "Update not needed"    },
		{ kUpdateAborted,      "Update aborted"       },
		{ 0,                   NULL                   }
	};
	
	for(int i = 0; state_values[i].name; i++)
	{
		if (state_values[i].value == deviceState)
			return state_values[i].name;
	}
	
	return NULL;
}
#endif // DEBUG

const char* stringFromReturn(IOReturn rtn)
{
	static const IONamedValue IOReturn_values[] = {
		{ kIOReturnIsoTooOld,          "Isochronous I/O request for distant past"     },
		{ kIOReturnIsoTooNew,          "Isochronous I/O request for distant future"   },
		{ kIOReturnNotFound,           "Data was not found"                           },
		// REVIEW: new error identifiers?
#ifndef TARGET_ELCAPITAN
		{ kIOUSBUnknownPipeErr,        "Pipe ref not recognized"                      },
		{ kIOUSBTooManyPipesErr,       "Too many pipes"                               },
		{ kIOUSBNoAsyncPortErr,        "No async port"                                },
		{ kIOUSBNotEnoughPowerErr,     "Not enough power for selected configuration"  },
		{ kIOUSBEndpointNotFound,      "Endpoint not found"                           },
		{ kIOUSBConfigNotFound,        "Configuration not found"                      },
		{ kIOUSBTransactionTimeout,    "Transaction timed out"                        },
		{ kIOUSBTransactionReturned,   "Transaction has been returned to the caller"  },
		{ kIOUSBPipeStalled,           "Pipe has stalled, error needs to be cleared"  },
		{ kIOUSBInterfaceNotFound,     "Interface reference not recognized"           },
		{ kIOUSBLowLatencyBufferNotPreviouslyAllocated,
			"Attempted to user land low latency isoc calls w/out calling PrepareBuffer" },
		{ kIOUSBLowLatencyFrameListNotPreviouslyAllocated,
			"Attempted to user land low latency isoc calls w/out calling PrepareBuffer" },
		{ kIOUSBHighSpeedSplitError,   "Error on hi-speed bus doing split transaction"},
		{ kIOUSBSyncRequestOnWLThread, "Synchronous USB request on workloop thread."  },
		{ kIOUSBDeviceNotHighSpeed,    "The device is not a high speed device."       },
		{ kIOUSBClearPipeStallNotRecursive,
			"IOUSBPipe::ClearPipeStall should not be called rescursively"               },
		{ kIOUSBLinkErr,               "USB link error"                               },
		{ kIOUSBNotSent2Err,           "Transaction not sent"                         },
		{ kIOUSBNotSent1Err,           "Transaction not sent"                         },
		{ kIOUSBNotEnoughPipesErr,     "Not enough pipes in interface"                },
		{ kIOUSBBufferUnderrunErr,     "Buffer Underrun (Host hardware failure)"      },
		{ kIOUSBBufferOverrunErr,      "Buffer Overrun (Host hardware failure"        },
		{ kIOUSBReserved2Err,          "Reserved"                                     },
		{ kIOUSBReserved1Err,          "Reserved"                                     },
		{ kIOUSBWrongPIDErr,           "Pipe stall, Bad or wrong PID"                 },
		{ kIOUSBPIDCheckErr,           "Pipe stall, PID CRC error"                    },
		{ kIOUSBDataToggleErr,         "Pipe stall, Bad data toggle"                  },
		{ kIOUSBBitstufErr,            "Pipe stall, bitstuffing"                      },
		{ kIOUSBCRCErr,                "Pipe stall, bad CRC"                          },
#endif
		{ 0,                           NULL                                           }
	};
	
	for(int i = 0; IOReturn_values[i].name; i++)
	{
		if (IOReturn_values[i].value == rtn)
			return IOReturn_values[i].name;
	}
	
	return NULL;
}

bool supportsHandshake(UInt16 vid, UInt16 pid)
{
	UInt32 i;
	
	for (i = 0; hskSupport[i].vid != 0; i++) {
		if ((hskSupport[i].vid == vid) && (hskSupport[i].pid == pid))
			return true;
	}
	
	return false;
}

IOReturn findInterfaces(IOUSBDeviceInterface300 **device)
{
	IOReturn                    kr;
	IOUSBFindInterfaceRequest   request;
	io_iterator_t               iterator;
	io_service_t                usbInterface;
	IOCFPlugInInterface         **plugInInterface = NULL;
	IOUSBInterfaceInterface     **interface = NULL;
	HRESULT                     result;
	SInt32                      score;
	UInt8                       interfaceClass;
	UInt8                       interfaceSubClass;
	UInt8                       interfaceNumEndpoints;
	int                         pipeRef;
	// Placing the constant kIOUSBFindInterfaceDontCare into the following
	// fields of the IOUSBFindInterfaceRequest structure will allow you
	// to find all the interfaces
	request.bInterfaceClass = kIOUSBFindInterfaceDontCare;
	request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
	request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
	request.bAlternateSetting = kIOUSBFindInterfaceDontCare;
	// Get an iterator for the interfaces on the device
	kr = (*device)->CreateInterfaceIterator(device,
										&request, &iterator);
	while ((usbInterface = IOIteratorNext(iterator)))
	{
		// Create an intermediate plug-in
		kr = IOCreatePlugInInterfaceForService(usbInterface,
							kIOUSBInterfaceUserClientTypeID,
							kIOCFPlugInInterfaceID,
							&plugInInterface, &score);
		// Release the usbInterface object after getting the plug-in
		kr = IOObjectRelease(usbInterface);
		if ((kr != kIOReturnSuccess) || !plugInInterface)
		{
			fprintf(stderr, "Unable to create a plug-in (%08x)\n", kr);
			break;
		}
		// Now create the device interface for the interface
		result = (*plugInInterface)->QueryInterface(plugInInterface,
					CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
					(LPVOID *) &interface);
		// No longer need the intermediate plug-in
		(*plugInInterface)->Release(plugInInterface);
		if (result || !interface)
		{
			fprintf(stderr, "Couldn’t create a device interface for the interface (%08x)\n", (int) result);
			break;
		}
		// Get interface class and subclass
		kr = (*interface)->GetInterfaceClass(interface,
													&interfaceClass);
		kr = (*interface)->GetInterfaceSubClass(interface,
												&interfaceSubClass);
		printf("Interface class %d, subclass %d\n", interfaceClass,
													interfaceSubClass);
		// Now open the interface. This will cause the pipes associated with
		// the endpoints in the interface descriptor to be instantiated
		kr = (*interface)->USBInterfaceOpen(interface);
		if (kr != kIOReturnSuccess)
		{
			fprintf(stderr, "Unable to open interface (%08x)\n", kr);
			(void) (*interface)->Release(interface);
			break;
		}
		// Get the number of endpoints associated with this interface
		kr = (*interface)->GetNumEndpoints(interface,
										&interfaceNumEndpoints);
		if (kr != kIOReturnSuccess)
		{
			fprintf(stderr, "Unable to get number of endpoints (%08x)\n", kr);
			(void) (*interface)->USBInterfaceClose(interface);
			(void) (*interface)->Release(interface);
			break;
		}
		printf("Interface has %d endpoints\n", interfaceNumEndpoints);
		// Access each pipe in turn, starting with the pipe at index 1
		// The pipe at index 0 is the default control pipe and should be
		// accessed using (*usbDevice)->DeviceRequest() instead
		for (pipeRef = 1; pipeRef <= interfaceNumEndpoints; pipeRef++)
		{
			IOReturn        kr2;
			UInt8           direction;
			UInt8           number;
			UInt8           transferType;
			UInt16          maxPacketSize;
			UInt8           interval;
			kr2 = (*interface)->GetPipeProperties(interface,
										pipeRef, &direction,
										&number, &transferType,
										&maxPacketSize, &interval);
			if (kr2 != kIOReturnSuccess && kr2 != kIOReturnNotOpen)
				fprintf(stderr, "Unable to get properties of pipe %d (%08x)\n", pipeRef, kr2);
			else
			{
				printf("PipeRef %d: ", pipeRef);
				printf("direction %s, ", usb_dir[direction]);
				printf("transfer type %s, maxPacketSize %d\n", usb_ctrl[direction], maxPacketSize);
			}
		}
		
		(void) (*interface)->USBInterfaceClose(interface);
		(void) (*interface)->Release(interface);
	}
	
	return kr;
}

int findPipe(IOUSBInterfaceInterface300** interface, UInt8 type, UInt8 direction)
{
	IOReturn kr;
	UInt8 interfaceNumEndpoints, findDirection, number, transferType, interval;
	UInt16 maxPacketSize;
	
	if ((*interface)->GetNumEndpoints(interface, &interfaceNumEndpoints) != KERN_SUCCESS)
		return 0;
	
	for (UInt8 pipeRef = 1; pipeRef <= interfaceNumEndpoints; pipeRef++)
	{
		kr = (*interface)->GetPipeProperties(interface, pipeRef, &findDirection, &number, &transferType, &maxPacketSize, &interval);
		
		if (kr != kIOReturnSuccess)
		{
			fprintf(stderr, "GetPipeProperties Failure (0x%08x)\n", kr);
			
			return 0;
		}
		
#ifdef DEBUG
		printf("pipeRef: %d direction: %d number: %d transferType: %d maxPacketSize: %d interval: %d\n", pipeRef, findDirection, number, transferType, maxPacketSize, interval);
#endif
		
		if (type == transferType && direction == findDirection)
		{
#ifdef DEBUG
			printf("Found matching endpoint\n");
#endif
			
			return pipeRef;
		}
	}
	
	return 0;
}

IOReturn hciCommand(IOUSBInterfaceInterface300** interface, void* command, UInt16 length)
{
	IOUSBDevRequestTO request;
	request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBClass, kUSBDevice);
	request.bRequest = 0;
	request.wValue = 0;
	request.wIndex = 0;
	request.wLength = length;
	request.pData = command;
	request.completionTimeout = hci_timeout;
	request.noDataTimeout = hci_timeout;
	IOReturn result = (*interface)->ControlRequestTO(interface, 0, &request);
	
	if (result != kIOReturnSuccess)
	{
		fprintf(stderr, "hciCommand failed ('%s' 0x%08x).\n", stringFromReturn(result), result);
	}
#ifdef DEBUG
	else
	{
		printf("hciCommand success (%d bytes sent).\n", request.wLenDone);
	}
#endif
	
	return result;
}

IOReturn getDeviceStatus(IOUSBInterfaceInterface300** interface, USBStatus *status)
{
	uint16_t stat = 0;
	IOUSBDevRequestTO request;
	request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
	request.bRequest = kUSBRqGetStatus;
	request.wValue = 0;
	request.wIndex = 0;
	request.wLength = sizeof(stat);
	request.pData = &stat;
	request.completionTimeout = hci_timeout;
	request.noDataTimeout = hci_timeout;
	IOReturn result = (*interface)->ControlRequestTO(interface, 0, &request);
	*status = stat;
	return result;
}

IOReturn hciParseResponse(IOUSBInterfaceInterface300** interface, void* response, UInt16 length, bool useHandshake, void* output, UInt32* outputLength, enum DeviceState *deviceState)
{
	HCI_RESPONSE* header = (HCI_RESPONSE*)response;
	IOReturn result = kIOReturnSuccess;
	
	switch (header->eventCode)
	{
		case HCI_EVENT_COMMAND_COMPLETE:
		{
			struct HCI_COMMAND_COMPLETE* event = (struct HCI_COMMAND_COMPLETE*)response;
			
			switch (event->opcode)
			{
				case HCI_OPCODE_READ_LOCAL_VERSION:
				{
#ifdef DEBUG
					printf("READ LOCAL VERSION complete (status: 0x%02x, length: %d bytes).\n", event->status, header->length);
#endif
					
					HCI_RP_READ_LOCAL_VERSION* ver = (HCI_RP_READ_LOCAL_VERSION*)((char*)response + 5);
					const char *hw_name = NULL;

					for (int i = 0; bcm_usb_subver_table[i].name; i++)
					{
						if (ver->lmp_subver == bcm_usb_subver_table[i].subver)
						{
							hw_name = bcm_usb_subver_table[i].name;
							break;
						}
					}
					
					printf("Local Version: %s_%3.3u.%3.3u.%3.3u.%4.4u\n", hw_name ? hw_name : "BCM", (ver->lmp_subver & 0x7000) >> 13, (ver->lmp_subver & 0x1f00) >> 8, (ver->lmp_subver & 0x00ff), ver->hci_rev & 0x0fff);
					
					*deviceState = kUSBProduct;
					
					break;
				}
				case HCI_OPCODE_READ_USB_PRODUCT:
				{
#ifdef DEBUG
					printf("READ USB PRODUCT complete (status: 0x%02x, length: %d bytes).\n", event->status, header->length);
#endif
					
					// Read USB Product Info
					UInt16 vid = *(UInt16*)((char*)response + 6);
					UInt16 pid = *(UInt16*)((char*)response + 8);

					printf("USB Product VendorId: 0x%04x ProductId: 0x%04x\n", vid, pid);
					
					*deviceState = kFirmwareVersion;
					
					break;
				}
				case HCI_OPCODE_READ_VERBOSE_CONFIG:
				{
#ifdef DEBUG
					printf("READ VERBOSE CONFIG complete (status: 0x%02x, length: %d bytes).\n", event->status, header->length);
#endif
					
					UInt8 chipsetID = *((char*)response + 6);
					UInt16 build = *(UInt16*)((char*)response + 10);
					UInt16 firmwareVersion = build + 0x1000;
					
					printf("ChipsetID: %d Build: %4.4u Firmware: v%d\n", chipsetID, build, firmwareVersion);
					
					// Device does not require a firmware patch at this time
#if (FORCE_UPDATE == 0)
					if (build > 0)
					{
						printf("Update Not Needed.\nDone.\n");
						
						*deviceState = kUpdateNotNeeded;
					}
					else
#endif
					{
						*deviceState = kDownloadMiniDriver;
					}
					break;
				}
				case HCI_OPCODE_DOWNLOAD_MINIDRIVER:
				{
#ifdef DEBUG
					printf("DOWNLOAD MINIDRIVER complete (status: 0x%02x, length: %d bytes).\n", event->status, header->length);
#endif
					
					*deviceState = kMiniDriverComplete;
					break;
				}
				case HCI_OPCODE_LAUNCH_RAM:
				{
#ifdef DEBUG
					printf("LAUNCH RAM complete (status: 0x%02x, length: %d bytes).\n", event->status, header->length);
#endif
					
					*deviceState = kInstructionWritten;
					break;
				}
				case HCI_OPCODE_END_OF_RECORD:
				{
#ifdef DEBUG
					printf("END OF RECORD complete (status: 0x%02x, length: %d bytes).\n", event->status, header->length);
#endif
					
					*deviceState = kFirmwareWritten;
					break;
				}
				case HCI_OPCODE_RESET:
				{
#ifdef DEBUG
					printf("RESET complete (status: 0x%02x, length: %d bytes).\n", event->status, header->length);
#endif

					*deviceState = *deviceState == kPreInitialize ? kLocalVersion : kResetComplete;
					break;
				}
				default:
				{
#ifdef DEBUG
					printf("Event COMMAND COMPLETE (opcode 0x%04x, status: 0x%02x, length: %d bytes).\n", event->opcode, event->status, header->length);
#endif
					break;
				}
			}
			
			if (output && outputLength)
			{
				bzero(output, *outputLength);
				
				// Return the received data
				if (*outputLength >= length)
				{
#ifdef DEBUG
					printf("Returning output data %d bytes.\n", length);
#endif
					
					*outputLength = length;
					memcpy(output, response, length);
				} else
				{
					// Not enough buffer space for data
					result = kIOReturnMessageTooLarge;
				}
			}
			break;
		}
			
		case HCI_EVENT_NUM_COMPLETED_PACKETS:
#ifdef DEBUG
			printf("Number of completed packets.\n");
#endif
			break;
			
		case HCI_EVENT_CONN_COMPLETE:
#ifdef DEBUG
			printf("Connection complete event.\n");
#endif
			break;
			
		case HCI_EVENT_DISCONN_COMPLETE:
#ifdef DEBUG
			printf("Disconnection complete event.\n");
#endif
			break;
			
		case HCI_EVENT_HARDWARE_ERROR:
			fprintf(stderr, "Hardware error.\n");
			break;
			
		case HCI_EVENT_MODE_CHANGE:
#ifdef DEBUG
			printf("Mode change event.\n");
#endif
			break;
			
		case HCI_EVENT_LE_META:
#ifdef DEBUG
			printf("Low-Energy meta event.\n");
#endif
			break;
			
		case HCI_EVENT_VENDOR:
#ifdef DEBUG
			printf("Vendor specific event. Ready to reset device.\n");
#endif
			
			if (useHandshake)
			{
				// Device is ready for reset.
				*deviceState = kResetWrite;
			}
			break;
			
		default:
			fprintf(stderr, "Unknown event code (0x%02x).\n", header->eventCode);
			break;
	}
	
	return result;
}

bool bulkWrite(IOUSBInterfaceInterface300** interface, UInt8 pipeRef, const void* data, UInt32 length)
{
	IOReturn kr;
	kr = (*interface)->WritePipeTO(interface, pipeRef, (void*)data, length, hci_timeout, hci_timeout);
	
	if (kr != kIOReturnSuccess)
	{
		fprintf(stderr, "WritePipeTO failed (0x%02x).\n", kr);
		
		return false;
	}
	
	return true;
}

bool performUpgrade(IOUSBInterfaceInterface300** interface, CFMutableArrayRef instructions, int initialDelay, int preResetDelay, int postResetDelay, bool useHandshake)
{
	static unsigned char buffer[BUFFER_SIZE];
	UInt32 dataIndex = 0, length = 0;
	CFMutableDataRef data = (CFMutableDataRef) CFArrayGetValueAtIndex(instructions, dataIndex);
	CFIndex dataLength = CFDataGetLength(data);
#ifdef DEBUG
	enum DeviceState previousState = kUnknown;
#endif
	
	enum DeviceState deviceState = kPreInitialize;
	
	UInt8 pipeIn = findPipe(interface, kUSBInterrupt, kUSBIn);
	UInt8 pipeOut = findPipe(interface, kUSBBulk, kUSBOut);
	
	if (pipeIn == 0 || pipeOut == 0)
	{
		fprintf(stderr, "Couldn't find pipes.\n");
		return false;
	}

	while (true)
	{
#ifdef DEBUG
		if (deviceState != kInstructionWrite && deviceState != kInstructionWritten)
			printf("State '%s' --> '%s'.\n", getState(previousState), getState(deviceState));
		
		previousState = deviceState;
#endif
		
		// Break out when done
		if (deviceState == kUpdateAborted || deviceState == kUpdateComplete || deviceState == kUpdateNotNeeded)
			break;
		
		// Note on following switch/case:
		//   use 'break' when a response from io completion callback is expected
		//   use 'continue' when a change of state with no expected response (loop again)
		
		switch (deviceState)
		{
			case kPreInitialize:
				// Reset the device to put it in a defined state.
				if (hciCommand(interface, &HCI_RESET, sizeof(HCI_RESET)) != kIOReturnSuccess)
				{
					fprintf(stderr, "HCI_RESET failed, aborting.\n");
					deviceState = kUpdateAborted;
					continue;
				}
				break;
				
			case kLocalVersion:
				// Wait for device to become ready after reset.
				usleep(postResetDelay * 1000);
				
				if (hciCommand(interface, &HCI_READ_LOCAL_VERSION, sizeof(HCI_READ_LOCAL_VERSION)) != kIOReturnSuccess)
				{
					fprintf(stderr, "HCI_READ_LOCAL_VERSION failed, aborting.\n");
					deviceState = kUpdateAborted;
					continue;
				}
				break;
				
			case kUSBProduct:
				if (hciCommand(interface, &HCI_VSC_READ_USB_PRODUCT, sizeof(HCI_VSC_READ_USB_PRODUCT)) != kIOReturnSuccess)
				{
					fprintf(stderr, "HCI_VSC_READ_USB_PRODUCT failed, aborting.\n");
					deviceState = kUpdateAborted;
					continue;
				}
				break;
				
			case kFirmwareVersion:
				if (hciCommand(interface, &HCI_VSC_READ_VERBOSE_CONFIG, sizeof(HCI_VSC_READ_VERBOSE_CONFIG)) != kIOReturnSuccess)
				{
					fprintf(stderr, "HCI_VSC_READ_VERBOSE_CONFIG failed, aborting.\n");
					deviceState = kUpdateAborted;
					continue;
				}
				break;

			case kDownloadMiniDriver:
				// Initiate firmware upgrade
				if (hciCommand(interface, &HCI_VSC_DOWNLOAD_MINIDRIVER, sizeof(HCI_VSC_DOWNLOAD_MINIDRIVER)) != kIOReturnSuccess)
				{
					fprintf(stderr, "HCI_VSC_DOWNLOAD_MINIDRIVER failed, aborting.\n");
					deviceState = kUpdateAborted;
					continue;
				}
				break;
				
			case kMiniDriverComplete:
				// If this usleep is not issued, the device is not ready to receive
				// the firmware instructions and we will deadlock due to lack of
				// responses.
				usleep(initialDelay * 1000);
				
				// Write first instruction to trigger response
				if (dataIndex < dataLength)
				{
					data = (CFMutableDataRef)CFArrayGetValueAtIndex(instructions, dataIndex++);
					bulkWrite(interface, pipeOut, (const void*)CFDataGetBytePtr(data), (UInt32)CFDataGetLength(data));
				}
				break;
				
			case kInstructionWrite:
				if (dataIndex < dataLength)
				{
					data = (CFMutableDataRef)CFArrayGetValueAtIndex(instructions, dataIndex++);
					bulkWrite(interface, pipeOut, (const void*)CFDataGetBytePtr(data), (UInt32)CFDataGetLength(data));
				}
				else
				{
					// Firmware data fully written
					if (hciCommand(interface, &HCI_VSC_END_OF_RECORD, sizeof(HCI_VSC_END_OF_RECORD)) != kIOReturnSuccess)
					{
						fprintf(stderr, "HCI_VSC_END_OF_RECORD failed, aborting.\n");
						deviceState = kUpdateAborted;
						continue;
					}
				}
				break;
				
			case kInstructionWritten:
				deviceState = kInstructionWrite;
				continue;
				
			case kFirmwareWritten:
				if (!useHandshake)
				{
					usleep(preResetDelay * 1000);

					if (hciCommand(interface, &HCI_RESET, sizeof(HCI_RESET)) != kIOReturnSuccess)
					{
						fprintf(stderr, "HCI_RESET failed, aborting.\n");
						deviceState = kUpdateAborted;
						continue;
					}
				}
				break;

			case kResetWrite:
				if (hciCommand(interface, &HCI_RESET, sizeof(HCI_RESET)) != kIOReturnSuccess)
				{
					fprintf(stderr, "HCI_RESET failed, aborting.\n");
					deviceState = kUpdateAborted;
					continue;
				}
				break;
				
			case kResetComplete:
				usleep(postResetDelay * 1000);
				USBStatus status;
				getDeviceStatus(interface, &status);
#ifdef DEBUG
				printf("Reset Complete (0x%08x)\n", status);
#endif
				printf("Done.\n");
				deviceState = kUpdateComplete;
				continue;
				
			case kUnknown:
			case kUpdateNotNeeded:
			case kUpdateComplete:
			case kUpdateAborted:
				fprintf(stderr, "Error: kUnkown/kUpdateComplete/kUpdateAborted cases should be unreachable.\n");
				break;
		}
		
		// Read pipe
		memset(buffer, 0, BUFFER_SIZE);
		length = BUFFER_SIZE-1;
		IOReturn status = (*interface)->ReadPipe(interface, pipeIn, buffer, &length);
		
		switch (status)
		{
			case kIOReturnSuccess:
				hciParseResponse(interface, buffer, length, useHandshake, NULL, NULL, &deviceState);
				break;
			case kIOReturnAborted:
				fprintf(stderr, "Return aborted (0x%08x)\n", status);
				deviceState = kUpdateAborted;
				break;
			case kIOReturnNoDevice:
				fprintf(stderr, "No such device (0x%08x)\n", status);
				deviceState = kUpdateAborted;
				break;
			case kIOUSBTransactionTimeout:
				fprintf(stderr, "Transaction timeout (0x%08x)\n", status);
				break;
			case kIOUSBPipeStalled:
				fprintf(stderr, "Pipe stalled (0x%08x)\n", status);
				(*interface)->ClearPipeStall(interface, pipeIn);
				deviceState = kUpdateAborted;
				break;
			case kIOReturnNotResponding:
				fprintf(stderr, "Not responding - Delaying next read (0x%08x)\n", status);
				(*interface)->ClearPipeStall(interface, pipeIn);
				deviceState = kUpdateAborted;
				break;
			default:
				fprintf(stderr, "Unknown error (0x%08x)\n", status);
				deviceState = kUpdateAborted;
				break;
		}
	}
	
	(*interface)->AbortPipe(interface, pipeIn);
	(*interface)->AbortPipe(interface, pipeOut);
	
	return deviceState == kUpdateComplete || deviceState == kUpdateNotNeeded;
}

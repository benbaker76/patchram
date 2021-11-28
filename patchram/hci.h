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

#ifndef hci_h
#define hci_h

#include <IOKit/usb/IOUSBLib.h>

enum DeviceState
{
	kUnknown,
	kPreInitialize,
	kLocalVersion,
	kUSBProduct,
	kFirmwareVersion,
	kDownloadMiniDriver,
	kMiniDriverComplete,
	kInstructionWrite,
	kInstructionWritten,
	kFirmwareWritten,
	kResetWrite,
	kResetComplete,
	kUpdateComplete,
	kUpdateNotNeeded,
	kUpdateAborted,
};

typedef struct DeviceHskSupport
{
	UInt16 vid;
	UInt16 pid;
} DeviceHskSupport;

typedef enum
{
	HCI_COMMAND = 0x01,
	HCI_ACL     = 0x02,
	HCI_SCL     = 0x03,
	HCI_EVENT   = 0x04
} HCI_PACKET_TYPE;

typedef enum
{
	HCI_EVENT_CONN_COMPLETE = 0x03,
	HCI_EVENT_DISCONN_COMPLETE = 0x05,
	HCI_EVENT_COMMAND_COMPLETE = 0x0e,
	HCI_EVENT_HARDWARE_ERROR = 0x10,
	HCI_EVENT_NUM_COMPLETED_PACKETS = 0x13,
	HCI_EVENT_MODE_CHANGE = 0x14,
	HCI_EVENT_LE_META = 0x3e,
	HCI_EVENT_VENDOR = 0xff
} HCI_EVENT_TYPE;

typedef struct __attribute__((packed))
{
	union
	{
		uint16_t opcode;
		struct
		{
			uint16_t ocf: 10;
			uint16_t ogf: 6;
		} op;
	};
	unsigned char length;
} HCI_PACKET;

typedef struct __attribute__((packed))
{
	unsigned char eventCode;
	unsigned char length;
} HCI_RESPONSE;

struct __attribute__((packed)) HCI_COMMAND_COMPLETE: HCI_RESPONSE
{
	unsigned char numCommands;
	union
	{
		uint16_t opcode;
		struct
		{
			uint16_t ocf: 10;
			uint16_t ogf: 6;
		} op;
	};
	unsigned char status;
};

struct __attribute__((packed)) HCI_RP_READ_LOCAL_VERSION
{
	uint8_t status;
	uint8_t hci_ver;
	uint16_t hci_rev;
	uint8_t lmp_ver;
	uint16_t manufacturer;
	uint16_t lmp_subver;
};

#define HCI_OPCODE_RESET 0x0c03
#define HCI_OPCODE_READ_VERBOSE_CONFIG 0xfc79
#define HCI_OPCODE_READ_CONTROLLER_FEATURES 0xfc6e
#define HCI_OPCODE_READ_USB_PRODUCT 0xfc5a
#define HCI_OPCODE_READ_LOCAL_NAME 0x0c14
#define HCI_OPCODE_READ_LOCAL_VERSION 0x1001
#define HCI_OPCODE_DOWNLOAD_MINIDRIVER 0xfc2e
#define HCI_OPCODE_LAUNCH_RAM 0xfc4c
#define HCI_OPCODE_END_OF_RECORD 0xfc4e
#define HCI_OPCODE_WAKEUP 0xfc53

// Standard HCI commands
extern uint8_t HCI_READ_LOCAL_VERSION[3];
extern uint8_t HCI_READ_LOCAL_COMMANDS[3];
extern uint8_t HCI_READ_FEATURES[3];
extern uint8_t HCI_READ_LOCAL_FEATURES[3];
extern uint8_t HCI_READ_LOCAL_NAME[3];
extern uint8_t HCI_RESET[3];

// Broadcom vendor specific commands

// Vendor Specific: Read chip-id and other Broadcom specific configuration variables
extern uint8_t HCI_VSC_READ_VERBOSE_CONFIG[3];

extern uint8_t HCI_VSC_READ_CONTROLLER_FEATURES[3];

extern uint8_t HCI_VSC_READ_USB_PRODUCT[3];

// Vendor Specific: Download mini driver
extern uint8_t HCI_VSC_DOWNLOAD_MINIDRIVER[3];

// Vendor Specific: End of Record
extern uint8_t HCI_VSC_END_OF_RECORD[7];

// Vendor Specific: Wake up
extern uint8_t HCI_VSC_WAKEUP[4];

bool supportsHandshake(UInt16 vid, UInt16 pid);
IOReturn findInterfaces(IOUSBDeviceInterface300 **device);
int findPipe(IOUSBInterfaceInterface300** interface, UInt8 type, UInt8 direction);
IOReturn hciCommand(IOUSBInterfaceInterface300** interface, void* command, UInt16 length);
IOReturn getDeviceStatus(IOUSBInterfaceInterface300** interface, USBStatus *status);
bool performUpgrade(IOUSBInterfaceInterface300** interface, CFMutableArrayRef instructions, int initialDelay, int preResetDelay, int postResetDelay, bool useHandshake);

#endif

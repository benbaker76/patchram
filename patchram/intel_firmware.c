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

#include "intel_firmware.h"
#include <dirent.h>
#include <stdio.h>
#include <zlib.h>

static void* z_alloc(void*, u_int items, u_int size);
static void z_free(void*, void *ptr);

typedef struct z_mem
{
	UInt32 alloc_size;
	UInt8 data[0];
} z_mem;

// Space allocation and freeing routines for use by zlib routines.
void* z_alloc(void* notused __unused, u_int num_items, u_int size)
{
	void* result = NULL;
	z_mem* zmem = NULL;
	UInt32 total = num_items * size;
	UInt32 allocSize =  total + sizeof(zmem);
	
	zmem = (z_mem*)malloc(allocSize);
	
	if (zmem)
	{
		zmem->alloc_size = allocSize;
		result = (void*)&(zmem->data);
	}
	
	return result;
}

void z_free(void* notused __unused, void* ptr)
{
	UInt32* skipper = (UInt32 *)ptr - 1;
	z_mem* zmem = (z_mem*)skipper;
	free((void*)zmem);
}

// Decompress the firmware using zlib inflate (If not compressed, return data normally)
bool decompressFirmware(void *inBuffer, void *outBuffer, uint32_t inBufferSize, uint32_t *outBufferSize)
{
	z_stream zstream;
	int zlib_result;

	// Verify if the data is compressed
	UInt16* magic = (UInt16*)inBuffer;
	
	if (*magic != 0x0178     // Zlib no compression
		&& *magic != 0x9c78  // Zlib default compression
		&& *magic != 0xda78) // Zlib maximum compression
	{
		// Return the data as-is
		return false;
	}
	
	bzero(&zstream, sizeof(zstream));
	
	zstream.next_in   = (unsigned char*)inBuffer;
	zstream.avail_in  = inBufferSize;
	
	zstream.next_out  = (unsigned char*)outBuffer;
	zstream.avail_out = BUFFER_SIZE;
	
	zstream.zalloc    = z_alloc;
	zstream.zfree     = z_free;
	
	zlib_result = inflateInit(&zstream);
	
	if (zlib_result != Z_OK)
		return false;
	
	zlib_result = inflate(&zstream, Z_FINISH);
	
	if (zlib_result != Z_STREAM_END && zlib_result != Z_OK)
		return false;
	
	*outBufferSize = (uint32_t)zstream.total_out;
	
	inflateEnd(&zstream);
	
	return true;
}

// Validate if the current character is a valid hexadecimal character
static inline bool validHexChar(UInt8 hex)
{
	return (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F') || (hex >= '0' && hex <= '9');
}

// Convert char '0-9,A-F' to hexadecimal values
static inline void hexNibble(UInt8 hex, UInt8 *output)
{
	*output <<= 4;
	
	if (hex >= 'a')
		*output |= (0x0A + (hex - 'a')) & 0x0F;
	if (hex >= 'A')
		*output |= (0x0A + (hex - 'A')) & 0x0F;
	else
		*output |= (hex - '0') & 0x0F;
}

// Two's complement checksum
static char checkSum(const UInt8* data, UInt16 len)
{
	UInt32 crc = 0;
	
	for (int i = 0; i < len; i++)
		crc += *(data + i);
	
	return (~crc + 1) & 0xFF;
}

UInt16 swapNibbles(UInt16 x)
{
	return ((x & 0x0F) << 4 | (x & 0xF0) >> 4);
}

CFMutableArrayRef parseFirmware(const UInt8* data, UInt32 len, UInt16 vendorId, UInt16 productId)
{
	CFMutableArrayRef instructions = CFArrayCreateMutable(kCFAllocatorDefault, 1, NULL);
	// Vendor Specific: Launch RAM
	UInt8 HCI_VSC_LAUNCH_RAM[] = { 0x4c, 0xfc };
	
	UInt32 address = 0;
	UInt8 binary[0x110];
	
	if (*data != HEX_LINE_PREFIX)
	{
		fprintf(stderr, "parseFirmware: Invalid firmware data.\n");
		goto exit_error;
	}
	
	while (*data == HEX_LINE_PREFIX)
	{
		bzero(binary, sizeof(binary));
		data++;
		
		int offset = 0;
		
		// Read all hex characters for this line
		while (validHexChar(*data))
		{
			hexNibble(*data++, &binary[offset]);
			hexNibble(*data++, &binary[offset++]);
		}
		
		// Parse line data
		UInt8 length = binary[0];
		UInt16 addr = binary[1] << 8 | binary[2];
		UInt8 record_type = binary[3];
		UInt8 checksum = binary[HEX_HEADER_SIZE + length];
		
#ifdef DEBUG
		for (int i = HEX_HEADER_SIZE; i < sizeof(binary) - 1; i++)
		{
			if (*((UInt16 *)&binary[i]) == swapNibbles(vendorId))
				printf("[%04x:%04x]: Found vendorId @ %04x type %02x checksum %02x\n", vendorId, productId, addr, record_type, checksum);
			if (*((UInt16 *)&binary[i]) == swapNibbles(productId))
				printf("[%04x:%04x]: Found productId @ %04x type %02x checksum %02x\n", vendorId, productId, addr, record_type, checksum);
		}
#endif
		
		UInt8 calc_checksum = checkSum(binary, HEX_HEADER_SIZE + length);
		
		if (checksum != calc_checksum)
		{
			fprintf(stderr, "parseFirmware: Invalid firmware, checksum mismatch.\n");
			goto exit_error;
		}
		
		// ParseFirmware class only supports I32HEX format
		switch (record_type)
		{
				// Data
			case REC_TYPE_DATA:
			{
				address = (address & 0xFFFF0000) | addr;
				
				// Reserved 4 bytes for the address
				length += HEX_HEADER_SIZE;
				
				// Allocate instruction (Opcode - 2 bytes, length - 1 byte)
				CFMutableDataRef instruction = CFDataCreateMutable(kCFAllocatorDefault, 3 + length);
				CFDataAppendBytes(instruction, HCI_VSC_LAUNCH_RAM, sizeof(HCI_VSC_LAUNCH_RAM));
				CFDataAppendBytes(instruction, (UInt8 *)&length, sizeof(length));
				CFDataAppendBytes(instruction, (UInt8 *)&address, sizeof(address));
				CFDataAppendBytes(instruction, (UInt8 *)&binary[HEX_HEADER_SIZE], length - HEX_HEADER_SIZE);
				CFArrayAppendValue(instructions, instruction);
				break;
			}
				// End of File
			case REC_TYPE_EOF:
				return instructions;
				// Extended Segment Address
			case REC_TYPE_ESA:
				// Segment address multiplied by 16
				address = binary[4] << 8 | binary[5];
				address <<= 4;
				break;
				// Start Segment Address
			case REC_TYPE_SSA:
				// Set CS:IP register for 80x86
				fprintf(stderr, "parseFirmware: Invalid firmware, unsupported start segment address instruction.\n");
				goto exit_error;
				// Extended Linear Address
			case REC_TYPE_ELA:
				// Set new higher 16 bits of the current address
				address = binary[4] << 24 | binary[5] << 16;
				break;
				// Start Linear Address
			case REC_TYPE_SLA:
				// Set EIP of 80386 and higher
				fprintf(stderr, "parseFirmware: Invalid firmware, unsupported start linear address instruction.\n");
				goto exit_error;
			default:
				fprintf(stderr, "parseFirmware: Invalid firmware, unknown record type encountered: 0x%02x.\n", record_type);
				goto exit_error;
		}
		
		// Skip over any trailing newlines / whitespace
		while (!validHexChar(*data) && !(*data == HEX_LINE_PREFIX))
			data++;
	}
	
	fprintf(stderr, "parseFirmware: Invalid firmware.\n");
	
exit_error:
	return NULL;
}

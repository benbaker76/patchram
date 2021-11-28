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

#ifndef intel_firmware_h
#define intel_firmware_h

#include <IOKit/IOKitLib.h>
#include <stdio.h>

// IntelHex firmware parsing
#define HEX_LINE_PREFIX ':'
#define HEX_HEADER_SIZE 4

#define REC_TYPE_DATA 0 // Data
#define REC_TYPE_EOF 1  // End of File
#define REC_TYPE_ESA 2  // Extended Segment Address
#define REC_TYPE_SSA 3  // Start Segment Address
#define REC_TYPE_ELA 4  // Extended Linear Address
#define REC_TYPE_SLA 5  // Start Linear Address

#define BUFFER_SIZE		1024 * 100

bool decompressFirmware(void *inBuffer, void *outBuffer, uint32_t inBufferSize, uint32_t *outBufferSize);
CFMutableArrayRef parseFirmware(const UInt8* data, UInt32 len, UInt16 vendorId, UInt16 productId);

#endif


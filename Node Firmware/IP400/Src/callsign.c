/*---------------------------------------------------------------------------
	Project:	      IP400

	Module:		      Compress and expand a callsign

	File Name:	      callsign.c

	Author:		      MartinA

	Creation Date:	  Jan 9, 2025

	Description:      Comresses an ASCII string of callsign characters. A callsign can
					  have up to 6 characters to fit into the four byte field, if longer
					  the rest are placed in the payload of the data frame. Callsigns can
					  be extended with a '-' character, in this case the call is removed
					  and padded before conversion, and the remainder is also in the
					  frame payload.

					  This program is free software: you can redistribute it and/or modify
					  it under the terms of the GNU General Public License as published by
					  the Free Software Foundation, either version 2 of the License, or
					  (at your option) any later version, provided this copyright notice
					  is included.

					  Copyright (c) Alberta Digital Radio Communications Society
					  All rights reserved.


	Revision History:

---------------------------------------------------------------------------*/

#include <string.h>

#include "types.h"
#include "frame.h"

#define		RADIX_40		40			// alphabet radix

// Radix 40 callsign alphabet
char alphabet[RADIX_40] = {
//		 0    1    2    3    4    5    6    7    8    9
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',

//		10   11   12   13   14   15   16   17   18   19
		' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',

//		20   21   22   23   24   25   26   27   28   29
		'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',

//		30   31   32   33   34   35   36   37   38   39
		'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '(', ')', '-'
};

// encode a char into the alphabet
uint32_t alphaEncode(char byte)
{
	byte = islower(byte) ? toupper(byte) : byte;

	for(uint32_t i=0;i<RADIX_40;i++)	{
		if(alphabet[i] == byte)
			return i;
	}
	return 0;
}

// decode a byte back into ASCII
char alphaDecode(uint32_t alpha)
{
	// numeric
	if((alpha >= 0) && (alpha <= 9))
		return '0' + alpha;

	// special cases: see alphabet table

	switch(alpha)	{

	case 10:
		return ' ';

	case 37:
		return '(';

	case 38:
		return ')';

	case 39:
		return '@';

	default:
		return 'A' + (alpha - 11);
	}
	return ' ';
}

void EncodeChunk(char *src, int len, uint32_t *enc)
{
	uint32_t chunk=alphaEncode(src[0]);

	// less than 2 characters
	if(len < 2)	{
		*enc = chunk;
		return;
	}

	// 2 or more
	for(int i=1;i<len;i++)	{
		uint32_t current = alphaEncode(src[i]);
		chunk = current + chunk*RADIX_40;
	}
	*enc = chunk;
}

// encode a callsign: if less than max_call, then just encode into the frame,
// else continue in the data portion of the frame
uint8_t callEncode(char *callsign, uint16_t ipAddr, IP400_FRAME *frame, uint8_t dest, uint8_t offset)
{
	int len = strlen(callsign);

	uint32_t encChunk;
	uint32_t *p = (uint32_t *)frame->buf;
	p += offset;

	if(dest == DEST_CALLSIGN)
		frame->dest.vpnBytes.encvpn = ipAddr;
	else
		frame->source.vpnBytes.encvpn  = ipAddr;

	// broadcast address
	if(!strcmp(callsign, "FFFF"))	{
		if(dest == DEST_CALLSIGN)
			frame->dest.callbytes.encoded = 0xFFFFFFFF;
		else
			frame->source.callbytes.encoded = 0xFFFFFFFF;
		return 0;
	}

	// ensure the callsign is padded out to at least 6 characters
	char paddedCall[50];
	strcpy(paddedCall, callsign);
	strcat(paddedCall, "      ");

	// non-broadcast: break it up into chunks of 6 characters
	int nChunks = len/MAX_CALL;
	if ((len % MAX_CALL) > 0)
		nChunks++;

	char *cll = paddedCall;

	for(int k=0;k<nChunks;k++)		{
		EncodeChunk(cll, MAX_CALL, &encChunk);
		if(k==0)	{
			if(dest == DEST_CALLSIGN)
				frame->dest.callbytes.encoded = encChunk;
			else
				frame->source.callbytes.encoded = encChunk;
			// callsign less than or equal to MAX_CALL
			if(nChunks == 1)	{
				return 0;
			}
		} else {
			*p++ = encChunk;
			cll += MAX_CALL;
			if(dest == DEST_CALLSIGN)
				frame->flagfld.flags.destExt = TRUE;
			else
				frame->flagfld.flags.srcExt = TRUE;
		}
	}
	*p++ = 0xFF000000;
	// return the offset in the buffer
	return (p-(uint32_t *)frame->buf) + sizeof(uint32_t);
}

// decode a callsign
BOOL callDecode(IP400_MAC *encCall, char *callsign, uint16_t *ipAddr)
{
	char tmpBuf[10], *p = tmpBuf;
	int i;

	uint32_t encoded = encCall->callbytes.encoded;
	for(i=0;i<MAX_CALL;i++)		{
		*p++ = alphaDecode(encoded % RADIX_40);
		encoded /= RADIX_40;
	}
	*p = '\0';
	for(i=strlen(tmpBuf)-1;i>=0;i--)
		*callsign++ = tmpBuf[i];

	*callsign = '\0';
	if(ipAddr != NULL)
		*ipAddr = encCall->vpnBytes.encvpn;

	return TRUE;
}

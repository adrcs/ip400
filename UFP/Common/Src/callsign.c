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
#include "kiss.h"

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

/*
 * Encode an AX25 format callsign
 * Only 6 bytes are used
 */
void ax25Encode(AX25_ADDR *ax25addr, IP400_FRAME *frame, CallSignSource dest)
{
	char asciiCall[20];

	for(int i=0;i<N_AX25_CALL;i++)
		asciiCall[i] = ax25addr->callFields.call.callsign[i].ascii;
	asciiCall[N_AX25_CALL] = '\0';

	uint16_t vpnAddr = AX25_VPN_BASE | ax25addr->callFields.ssid.ssidField.ssid;
	vpnAddr |= ax25addr->callFields.ssid.ssidField.c_h_bit << 4;

	callEncode(asciiCall, vpnAddr, frame, dest);
}

/*
 * Encode a callsign of up to 12 characters
 * First 6 go into MAC address, second 6 at the end of the header
 */
BOOL callEncode(char *callsign, uint16_t vpnAddr, IP400_FRAME *frame, CallSignSource dest)
{
	int len = strlen(callsign);

	// broadcast address: finish here..
	if(!strcmp(callsign, "FFFF"))	{
		if(dest == DEST_CALLSIGN)	{
			frame->dest.callbytes.callsign.encoded = 0xFFFFFFFF;
			frame->dest.vpnBytes.encvpn = 0xFFFF;
		}
		else if(dest == SRC_CALLSIGN)	{
			frame->source.callbytes.callsign.encoded = 0xFFFFFFFF;
			frame->source.vpnBytes.encvpn = 0xFFFF;
		}
		return 0;
	}

	char paddedCall[50];
	strcpy(paddedCall, callsign);
	strcat(paddedCall, "      ");

	// encode the chunks
	uint32_t encChunk = 0, encExt = 0;
	uint8_t extcall = FALSE;
	EncodeChunk(paddedCall, MAX_CALL, &encChunk);

	if(len > MAX_CALL)	{
		EncodeChunk(&paddedCall[MAX_CALL], MAX_CALL, &encExt);
		extcall = TRUE;
	}

	HOPTABLE *hopFrame = (HOPTABLE *)frame;

	switch(dest)	{

	case DEST_CALLSIGN:
		frame->dest.callbytes.callsign.encoded = encChunk;
		frame->dest.vpnBytes.encvpn = vpnAddr;
		frame->destExt.callsign.encoded = encExt;
		frame->flagfld.flags.destExt = extcall;
		break;

	case SRC_CALLSIGN:
		frame->source.callbytes.callsign.encoded = encChunk;
		frame->source.vpnBytes.encvpn = vpnAddr;
		frame->srcExt.callsign.encoded = encExt;
		frame->flagfld.flags.srcExt = extcall;
		break;

	case RPTR_SLOT1:
		hopFrame->rptCalls[0].callbytes.callsign.encoded = encChunk;
		hopFrame->rptCalls[0].vpnBytes.encvpn = vpnAddr;
		hopFrame->hopflags[0].flags.valid = TRUE;
		hopFrame->hopflags[0].flags.hbr = FALSE;
		break;

	case RPTR_SLOT2:
		hopFrame->rptCalls[1].callbytes.callsign.encoded = encChunk;
		hopFrame->rptCalls[1].vpnBytes.encvpn = vpnAddr;
		hopFrame->hopflags[1].flags.valid = TRUE;
		hopFrame->hopflags[1].flags.hbr = FALSE;
		break;

	}

	return TRUE;
}

// decode a callsign
BOOL callDecode(IP400_FRAME *frame, char *callsign, uint16_t *ipAddr, CallSignSource source)
{
	char tmpBuf[20], *p = tmpBuf;
	int i;
	uint32_t encoded=0, encExt=0;
	uint8_t isExt = 0;

	HOPTABLE *hopTable = (HOPTABLE *)frame;

	switch(source)	{

		case DEST_CALLSIGN:
			encoded = frame->dest.callbytes.callsign.encoded;
			encExt = frame->destExt.callsign.encoded;
			isExt = frame->flagfld.flags.destExt;
			if(ipAddr != NULL)
				*ipAddr = frame->dest.vpnBytes.encvpn;
			break;

		case SRC_CALLSIGN:
			encoded = frame->source.callbytes.callsign.encoded;
			encExt = frame->srcExt.callsign.encoded;
			isExt = frame->flagfld.flags.srcExt;
			if(ipAddr != NULL)
				*ipAddr = frame->source.vpnBytes.encvpn;
			break;

		case RPTR_SLOT1:
			encoded = hopTable->rptCalls[0].callbytes.callsign.encoded;
			hopTable->hopflags[0].flags.valid = TRUE;
			hopTable->hopflags[0].flags.hbr = FALSE;
			if(ipAddr != NULL)
				*ipAddr = hopTable->rptCalls[0].vpnBytes.encvpn;
			break;

		case RPTR_SLOT2:
			encoded = hopTable->rptCalls[1].callbytes.callsign.encoded;
			hopTable->hopflags[0].flags.valid = TRUE;
			hopTable->hopflags[0].flags.hbr = FALSE;
			if(ipAddr != NULL)
				*ipAddr = hopTable->rptCalls[1].vpnBytes.encvpn;
			break;

	}

	// decode the first 6 characters
	for(i=0;i<MAX_CALL;i++)		{
		*p++ = alphaDecode(encoded % RADIX_40);
		encoded /= RADIX_40;
	}

	// decode the next
	if(isExt)		{
		for(i=0;i<MAX_CALL;i++)		{
			*p++ = alphaDecode(encExt % RADIX_40);
			encExt /= RADIX_40;
		}
	}

	*p = '\0';
	for(i=strlen(tmpBuf)-1;i>=0;i--)
		*callsign++ = tmpBuf[i];

	*callsign = '\0';

	return TRUE;
}

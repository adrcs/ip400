/*---------- -----------------------------------------------------------------
	Project:	    IP400 mini node

	File Name:	    kiss.c

	Author:		    MartinA

	Description:	This module handles the basic KISS interface

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History:

---------------------------------------------------------------------------*/
#include <stdint.h>
#include <string.h>
#include <config.h>

#include "frame.h"			// include frame.h before kiss.h to resolve IP400_FRAME type
#include "kiss.h"
#include "types.h"
#include "usart.h"
#include "setup.h"
#include "memory.h"
#include "xcvr.h"
#ifdef FPGA_FLASH_HANDLE
#include "platform.h"
#endif

// local definitions
#define	KISS_VER_PORT		0xE0		// port number for version request command
#define	MAX_PORT_NUMBER		N_XCVRS		// max allowable port number

#define	FLASH_UPDATE_CMD	0xE8		// flash update command

// message processor states
typedef enum msg_proc_e {
	KISS_STATE_START=0,				// waiting for frame start
	KISS_STATE_TYPE,				// frame type
	KISS_STATE_DATA,				// processing frame data
	KISS_STATE_FLUSH,				// flushing the frame data
	KISS_STATE_DISABLED				// KISS is not enabled
} KISS_STATE;

// inbound kiss frame
uint8_t 	argCnt;					// argument count
uint8_t 	argNum;					// argument number
KISS_STATE 	kissState;				// state machine
BOOL		escapedMode;			// in escaped mode
uint16_t	dataLength;				// length of data frame
uint8_t		returnState;			// return state to restart for a new frame
uint8_t		portNum;				// port number in last message

// header for parsed data frame
struct data_frame_hdr_t {
	uint8_t			cmd;			// command
	uint8_t  		*buffer;		// buffer address
	uint16_t 		length;			// data length
} KissFrame;

// outbound data frame
union 	{
	AX25_ADDR	Address;
	uint8_t		bytes[sizeof(AX25_ADDR)];
} KISS_ADDRESS;

AX25_VPNADDR	vpnAddress;			// VPN address in AX.25 format

// fwd refs this module
BOOL processKiss2IP400Frame(void *rawKissFrame, uint8_t port);
void processKissCommand(void);
void SendKissChar(uint8_t c, BOOL xlate);
void SendKissCall(char *callsign, uint16_t vpnAddr, BOOL last);
void processVerRequest(void);
void sendSerial(void);

// tweaks for uart type
#if KISS_ON_LPUART
#define	BYTES_IN_BUFFER		DBUART_databuffer_bytesInBuffer
#define DATABUFFER_GET		DBUART_databuffer_get
#define	SEND_STRING			DBUART_Send_String
#define	SEND_CHAR			DBUART_Send_Char
#else
#define	BYTES_IN_BUFFER		USART_databuffer_bytesInBuffer
#define DATABUFFER_GET		USART_databuffer_get
#define	SEND_STRING			USART_Send_String
#define	SEND_CHAR			USART_Send_Char
#endif

//  init
void KissInit(void)
{
	// allocate a data buffer: if kiss is enabled
	if(isAX25Enabled())		{
		KissFrame.buffer=nodeMemAlloc(KISS, PAYLOAD_MAX);
#if	KISS_ON_LPUART
		kissState = returnState = KISS_STATE_START;
#else
		kissState = returnState = KISS_STATE_TYPE;
#endif
	} else {
		kissState = returnState = KISS_STATE_DISABLED;
	}

}

/*
 * KISS Receiver: process an IL2P frame into an IP400 encapsulated frame
 * Send it using the SendDataFrame facility
 * returns TRUE to remain in mode, else FALSE
 */
BOOL processKissFrame(void)
{
	int nBytesinBuff =0;
	static uint8_t *payload;
	USART_ELEMENT rawChar;
	uint8_t c;
	static GENERIC_KISS_FRAME *cmdFrame = NULL;

	if((nBytesinBuff=BYTES_IN_BUFFER()) == 0)
		return TRUE;

	for(int i=0;i<nBytesinBuff;i++)		{
		if((rawChar=DATABUFFER_GET(0)) != BUFFER_NO_DATA)	{
			c = (uint8_t)(rawChar & 0xff);

			// in escape mode
			if(escapedMode)			{

				switch(c)	{

				case KISS_TFEND:
					c = KISS_FEND;
					break;

				case KISS_TFESC:
					c = KISS_FESC;
					break;
				}
				escapedMode = FALSE;

			// not in escape mode
			} else {
				if(c == KISS_FESC)	{
					escapedMode = TRUE;
					return TRUE;
				}
			}

			// process char by state
			switch(kissState)		{

			// only in LPUART mode
			case KISS_STATE_START:
				if(c == KISS_FEND)	{
					kissState = KISS_STATE_TYPE;
				}
				break;

			// process type and port number
			case KISS_STATE_TYPE:
				KissFrame.cmd = c;
				KissFrame.length = 0;
				payload = KissFrame.buffer;
				cmdFrame = (GENERIC_KISS_FRAME *)&KissFrame.cmd;
				// flush frame if not def port number or unrecognizable frame type
				portNum = cmdFrame->command.port ;
				if((portNum > MAX_PORT_NUMBER)	|| (cmdFrame->command.type >= N_KISS_COMMANDS)) {
					kissState = KISS_STATE_FLUSH;
				}
				kissState = KISS_STATE_DATA;
				break;

			// process data field
			case KISS_STATE_DATA:
				if(c == KISS_FEND)	{
					kissState = returnState;
					processKissCommand();
					return FALSE;
				}
				*payload++ = c;
				KissFrame.length++;
				break;

			// flush the rest of the frame
			case KISS_STATE_FLUSH:
				if(c == KISS_FEND)	{
					kissState = returnState;			// start depends on which UART
					return FALSE;
				}

			case KISS_STATE_DISABLED:
				return FALSE;
			}
		}
	}
	return TRUE;
}

/*
 * process the kiss frame header
 */
void processKissCommand(void)
{
	GENERIC_KISS_FRAME *frame = (GENERIC_KISS_FRAME *)&KissFrame.cmd;

	KissCommandType cmdtype = (KissCommandType)frame->command.type;

	switch(cmdtype)	{

	// 00:  KISS Data Frame
	case KISS_TYPE_DATA:	// 00: Data frame
		processKiss2IP400Frame(KissFrame.buffer, frame->command.port);
		break;

	// 01-05: not processed (yet)
	case KISS_TYPE_TX_DELAY:
	case KISS_TYPE_P_PERSISTENCE:
	case KISS_TYPE_SLOT_TIME:
	case KISS_TYPE_TX_TAIL:
	case KISS_TYPE_FULL_DUPLEX:
		break;

	// 06: set power and squelch setting
	case KISS_TYPE_SET_HARDWARE:
		SetHardware(frame->data[0], frame->data[1]);
		break;

	// 07: set configuration
	case KISS_TYPE_SET_CONFIGURATION:
		break;

	// 08: get version number
	case KISS_GET_VERSION:
		processVerRequest();
		break;

	// 09: test mode
	case KISS_TEST_MODE:
		uint8_t testNum = frame->data[0];
		runXcvrTest(portNum, testNum);
		break;

	// 0A, 0B
	case KISS_SET_SERIAL:					// 0A: set serial number
	case KISS_GET_ALL:						// 0B: get all params
		break;


	// 0D: Flash update
	case KISS_FLASH_UPDATE:					// 0D: update flash memory: not implemented
#if _HAS_FPGA
		if(KissFrame.buffer[0] == FLASH_UPDATE_CMD)
			FPGALoader(UART_MAIN);
#endif
		break;

	// 0E: get serial number
	case KISS_GET_SERIAL:
		sendSerial();
		break;

	default:
		break;
	}
}

/*
 * Process a version request
 */
void processVerRequest(void)
{

	char version[50], versionString[150];

	strcpy(versionString, "Version: ");

	version[0] = def_params.params.FirmwareVerMajor + '0';
	version[1] = '.';
	version[2] = def_params.params.FirmwareVerMinor + '0';
	version[3] = ' ';
	version[4] = '\0';
	strcat(versionString, version);

	strcpy(version, getRevID());
	version[strlen(version)-1] = '\0';
	strcat(versionString, version);

	strcpy(version, getDateID());
	version[strlen(version)-1] = '\0';
	strcat(versionString, version);

	// send the frame header
	SendKissChar(KISS_FEND, FALSE);						// frame start delimiter
	SendKissChar(KISS_VER_PORT, FALSE);					// type: data frame

	for(int i=0;i<strlen(versionString);i++)
		SendKissChar(versionString[i], TRUE);

	SendKissChar(KISS_FEND, FALSE);						// frame end

}

/*
 * Send the serial number
 */
void sendSerial(void)
{
	USART_Print_string("%c%c%08X%08X%c", KISS_FEND, KISS_VER_PORT, GetDevID0(), GetDevID1(), KISS_FEND);
}

/*
 * process a kiss data frame
 * extract the required fields and use SendDataFrame to send it
 * BOOL SendDataFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, uint8_t *buf, uint16_t length, uint8_t coding, BOOL repeat)
 */
BOOL processKiss2IP400Frame(void *rawKissFrame, uint8_t port)
{
	uint8_t nHops = 0;

	// figure out which frame type it is: assume 2 repeater field
	AX25_TWO_RPT_FRAME *frame = (AX25_TWO_RPT_FRAME *)rawKissFrame;
	if(frame->frame_header.address[AX25_SOURCE_ADDRESS].callFields.ssid.ssidField.final == 0)		{
		// frame contains at least one repeater field
		if(frame->frame_header.address[AX25_RPT1_ADDRESS].callFields.ssid.ssidField.final == 0)	{
			nHops = 2;
		} else {
			nHops = 1;
		}
	}

	// allocate an IP400 frame (and buffer) (and hop table)
	IP400_FRAME *ip400Frame = nodeMemAlloc(KISS, sizeof(IP400_FRAME));
	if(ip400Frame == NULL)
		return FALSE;

	ip400Frame->buf = nodeMemAlloc(KISS, PAYLOAD_MAX);
	if(ip400Frame->buf == NULL)		{
		nodeMemFree(KISS, ip400Frame);
		return FALSE;
	}

	if(nHops)		{
		if((ip400Frame->hopTable = nodeMemAlloc(KISS, sizeof(HOPTABLE))) == NULL)	{
			DeleteFrame(ip400Frame);
			return FALSE;
		}
	} else {
		ip400Frame->hopTable = NULL;
	}

	HOPTABLE *hopAddr = (HOPTABLE *)ip400Frame->hopTable;

	// start with all flags cleared
	ip400Frame->flagfld.allflags = 0;

	// encode the to/from fields
	ax25Encode(&frame->frame_header.address[AX25_DEST_ADDRESS], ip400Frame, DEST_CALLSIGN);
	ax25Encode(&frame->frame_header.address[AX25_SOURCE_ADDRESS], ip400Frame, SRC_CALLSIGN);

	// fill in the rest of the frame
	switch(nHops)	{

	// no repeat fields
	case 0:
		AX25_NON_RPT_FRAME *nrFrame = (AX25_NON_RPT_FRAME *)rawKissFrame;
		ip400Frame->length = KissFrame.length-N_NO_RPT*sizeof(AX25_ADDR);
		memcpy(ip400Frame->buf, nrFrame->data, ip400Frame->length);
		ip400Frame->flagfld.flags.hoptable = FALSE;
		break;

	case 1:
		AX25_ONE_RPT_FRAME *orFrame = (AX25_ONE_RPT_FRAME *)rawKissFrame;
		ax25Encode(&orFrame->frame_header.address[AX25_RPT1_ADDRESS], (IP400_FRAME *)hopAddr, RPTR_SLOT1);
		ip400Frame->length = KissFrame.length-N_ONE_RPT*sizeof(AX25_ADDR);
		memcpy(ip400Frame->buf, orFrame->data, ip400Frame->length);
		memset(&hopAddr->rptCalls[1].callbytes, 0, sizeof(IP400_MAC));
		hopAddr->hopflags[1].flagbyte = 0;
		ip400Frame->flagfld.flags.hoptable = TRUE;
		break;

	case 2:
		AX25_TWO_RPT_FRAME *trFrame = (AX25_TWO_RPT_FRAME *)rawKissFrame;
		ax25Encode(&trFrame->frame_header.address[AX25_RPT1_ADDRESS], (IP400_FRAME *)hopAddr, RPTR_SLOT1);
		ax25Encode(&trFrame->frame_header.address[AX25_RPT2_ADDRESS], (IP400_FRAME *)hopAddr, RPTR_SLOT2);
		ip400Frame->length = KissFrame.length-N_TWO_RPT*sizeof(AX25_ADDR);
		memcpy(ip400Frame->buf, trFrame->data, ip400Frame->length);
		ip400Frame->flagfld.flags.hoptable = TRUE;
		break;

	}

	SendKissFrame(ip400Frame, port);

	return TRUE;				// more data to go
}

/*
 * And now the opposite...
 * Send an IP400 frame out as a KISS frame
 */
void ProcessRxKissFrame(IP400_FRAME *rFrame)
{
	uint16_t vpnAddr;
	char callsign[20];
	HOPTABLE *hoptable = rFrame->hopTable;

	// send the frame header
	SendKissChar(KISS_FEND, FALSE);						// frame start delimiter
	SendKissChar(KISS_TYPE_DATA, FALSE);				// type: data frame

	// reformat the destination address
	callDecode(rFrame, callsign, &vpnAddr, DEST_CALLSIGN);
	SendKissCall(callsign, vpnAddr, FALSE);

	// now source address
	callDecode(rFrame, callsign, &vpnAddr, SRC_CALLSIGN);
	SendKissCall(callsign, vpnAddr, rFrame->flagfld.flags.hoptable == 0);

	uint8_t hopCount = 0;

	for(int i=0;i<MAX_HOPS;i++)
		if(hoptable->hopflags[i].flags.valid)
			hopCount++;

	if(hopCount != 0)	{
		callDecode((IP400_FRAME *)hoptable, callsign, &vpnAddr, RPTR_SLOT1);
		SendKissCall(callsign, vpnAddr, hopCount == 1);
		if(hopCount == 2)	{
			callDecode((IP400_FRAME *)hoptable, callsign, &vpnAddr, RPTR_SLOT2);
			SendKissCall(callsign, vpnAddr, TRUE);
		}
	}

	// send out the data portion
	uint8_t *buf = (uint8_t *)rFrame->buf;
	for(int i=0;i<rFrame->length;i++)
		SendKissChar(*buf++, TRUE);

	SendKissChar(KISS_FEND, FALSE);						// frame end

	// deallocate the frame
	DeleteFrame(rFrame);

}
/*
 * Format and send out a kiss callsign
 */
void SendKissCall(char *callsign, uint16_t vpnAddr, BOOL last)
{
	union {
		AX25_ADDR ax25Addr;
		uint8_t bytes[sizeof(AX25_ADDR)];
	} addr;

	union	{
		uint16_t vpnBytes;
		AX25_VPNADDR ax25vpn;
	} vpn;

	// format the callsign
	for(int i=0;i<N_AX25_CALL;i++)	{
		addr.ax25Addr.callFields.call.callsign[i].ascii = callsign[i];
		addr.ax25Addr.callFields.call.callsign[i].res = 0;
	}

	// format the SSID field
	vpn.vpnBytes = vpnAddr;
	addr.ax25Addr.callFields.ssid.ssidField.c_h_bit = vpn.ax25vpn.c_h_bit;
	addr.ax25Addr.callFields.ssid.ssidField.ssid = vpn.ax25vpn.ssid;
	addr.ax25Addr.callFields.ssid.ssidField.res = 3;
	addr.ax25Addr.callFields.ssid.ssidField.final = last;

	for(int i=0;i<N_ADDR;i++)
		SendKissChar(addr.bytes[i], TRUE);

}

/*
 * Send a frame character:xlate if needed
 */
void SendKissChar(uint8_t c, BOOL xlate)
{
	if(xlate)		{

		// xlate chars if needed
		switch(c)	{

		case KISS_FEND:
			SEND_CHAR(KISS_FESC);
			SEND_CHAR(KISS_TFEND);
			break;

		case KISS_FESC:
			SEND_CHAR(KISS_FESC);
			SEND_CHAR(KISS_TFESC);
			break;

		default:
			SEND_CHAR(c);
			break;
		}
		return;
	}
	// no translation
	SEND_CHAR(c);
}


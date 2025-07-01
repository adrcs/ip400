/*---------------------------------------------------------------------------
	Project:	      IP400

	Module:		      Frame transmit and receive tasks

	File Name:	      frame.c

	Author:		      MartinA

	Creation Date:	  Jan 8, 2025

	Description:      Handle the transmission and reception of frames

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.


	Revision History:

---------------------------------------------------------------------------*/
#include <cmsis_os2.h>
#include <stdlib.h>
#include <string.h>
#include <config.h>

#include <stm32wl3x_hal_mrsubg.h>
#if _BOARD_TYPE == NUCLEO_BOARD
#include <stm32wl3x_nucleo.h>
#endif

#include "frame.h"
#include "dataq.h"
#include "setup.h"
#include "ip.h"
#include "spi.h"
#include "tasks.h"

// local defn's
uint32_t  nextSeq;		// next frame sequence number

void Frame_task_init(void)
{
	nextSeq = 0xFFFFFFFF;
}

/*
 * ------------------------------------------------------------------------
 * 	Frame Transmission handlers
 * ------------------------------------------------------------------------
 */

/*
 * Send a text frame. Buffer is not malloc'ed, can be static in ram
 * This function was deprecated and uses SendDataFrame instead,
 * complete with the correct coding type
 */
BOOL SendTextFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, char *buf, uint16_t length, BOOL repeat)
{

	return SendDataFrame(srcCall, srcIPAddr, destCall, dstIPAddr, (uint8_t *)buf, length, UTF8_TEXT_PACKET, repeat);

}

/*
 * compose and send a beacon frame (ping frame with broadcast destination)
 * Same comment about SendDataFrame.
 */
BOOL SendBeaconFrame(uint8_t *payload, int bcnlen)
{
	char *destCall = "FFFF";
	uint16_t destIP = 0xFFFF;					// broadcast destination address

	uint16_t srcIPAddr = GetVPNLowerWord();	// get my IP lower bits

	return SendDataFrame(setup_memory.params.setup_data.stnCall, srcIPAddr, destCall, destIP, payload, bcnlen, BEACON_PACKET, TRUE);
}

/*
 * Send an echo request frame
 */
BOOL SendEchoReqFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, char *buf, uint16_t length, BOOL repeat)
{
	return SendDataFrame(srcCall, srcIPAddr, destCall, dstIPAddr, (uint8_t *)buf, length, ECHO_REQUEST, FALSE);
}

/*
 * send an echo response
 */
BOOL SendEchoRespFrame(IP400_FRAME *reqFrame)
{
	IP400_FRAME *echoFrame;

	uint16_t length = reqFrame->length;

	if((echoFrame=malloc(sizeof(IP400_FRAME))) == NULL)
		return FALSE;

	if((echoFrame->buf=malloc(length + MAX_CALL_BUFFER)) == NULL)
		return FALSE;

	// swap source/destination
	echoFrame->source.callbytes.encoded = reqFrame->dest.callbytes.encoded;
	echoFrame->source.vpnBytes.encvpn = reqFrame->dest.vpnBytes.encvpn;

	echoFrame->dest.callbytes.encoded = reqFrame->source.callbytes.encoded;
	echoFrame->dest.vpnBytes.encvpn = reqFrame->source.vpnBytes.encvpn;

	// copy the payload in
	uint8_t *f = (uint8_t *)echoFrame->buf;
	memcpy(f, (const uint8_t *)reqFrame->buf, length);

	echoFrame->length = length;
	echoFrame->flagfld.allflags = 0;		// start with all flags cleared
	echoFrame->flagfld.flags.hop_count = 0;
	echoFrame->flagfld.flags.coding = ECHO_RESPONSE;
	echoFrame->flagfld.flags.repeat = FALSE;
	echoFrame->flagfld.flags.hoptable = 0;
	echoFrame->hopTable = NULL;
	echoFrame->seqNum = nextSeq++;

	QueueTxFrame(echoFrame);

	return TRUE;
}


/*
 * Send a generic data frame
 */
BOOL SendDataFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, uint8_t *buf, uint16_t length, uint8_t coding, BOOL repeat)
{
	IP400_FRAME *txFrame;

	if((txFrame=malloc(sizeof(IP400_FRAME))) == NULL)
		return FALSE;

	if((txFrame->buf=malloc(length + MAX_CALL_BUFFER)) == NULL)
		return FALSE;

	txFrame->flagfld.allflags = 0;		// start with all flags cleared

	// format the header
	uint8_t offset = callEncode(srcCall, srcIPAddr, txFrame, SRC_CALLSIGN, 0);
	offset += callEncode(destCall, dstIPAddr, txFrame, DEST_CALLSIGN, offset);

	// copy the payload in
	uint8_t *f = (uint8_t *)txFrame->buf;
	f += offset*sizeof(uint32_t);
	memcpy(f, (const uint8_t *)buf, length);

	txFrame->length = length + offset;
	txFrame->flagfld.flags.hop_count = 0;
	txFrame->flagfld.flags.coding = coding;
	txFrame->flagfld.flags.repeat = repeat;
	txFrame->flagfld.flags.hoptable = 0;
	txFrame->hopTable = NULL;
	txFrame->seqNum = nextSeq++;

	QueueTxFrame(txFrame);

	return TRUE;
}

/*
 * Send a frame received on the SPI
 * NB: input frame has a different format
 */
void SendSPIFrame(void *spi, uint8_t *payload, int len)
{
	IP400_FRAME *spiFrame;

	SPI_HEADER *spiHdr = (SPI_HEADER *)spi;

	// validate the frame type
	switch(spiHdr->coding)	{

	// beacon packets are not sent
	case BEACON_PACKET:
		return;

	// all others are
	}

	// allocate memory for the SPI frame and buffer
	if((spiFrame=malloc(sizeof(IP400_FRAME))) == NULL)
		return;
	if((spiFrame->buf=malloc(len)) == NULL)	{
		free(spiFrame);
		return;
	}

	// hop table is in the first part of the payload buffer
	uint16_t nHops = spiHdr->hopCount;
	uint16_t htblSize = nHops * sizeof(HOPTABLE);
	HOPTABLE *hTable;
	if(nHops)		{
		if((hTable=malloc(htblSize)) == NULL)	{
			free(spiFrame->buf);
			free(spiFrame);
			return;
		}
		memcpy(hTable, payload, htblSize);
		payload += htblSize;
		len -= htblSize;
		spiFrame->hopTable = (void *)hTable;
	}

	// add in source and destination
	uint8_t *srcCall;
	IP400_MAC *myMac;
	GetMyMAC(&myMac);
	srcCall = myMac->callbytes.bytes;

	memcpy(spiFrame->source.callbytes.bytes, srcCall, N_CALL);
	memcpy(spiFrame->source.vpnBytes.vpn, spiHdr->fromIP, N_IPBYTES);

	memcpy(spiFrame->dest.callbytes.bytes, spiHdr->toCall, N_CALL);
	memcpy(spiFrame->dest.vpnBytes.vpn, spiHdr->toIP, N_IPBYTES);

	spiFrame->flagfld.allflags = (uint16_t)(spiHdr->hopCount) + (uint16_t)(spiHdr->coding<<4) + (uint16_t)(spiHdr->flags<<8);
	if(spiFrame->flagfld.flags.hop_count)
		spiFrame->flagfld.flags.hoptable = TRUE;
	spiFrame->seqNum = nextSeq++;

	memcpy(spiFrame->buf, payload, len);

	QueueTxFrame(spiFrame);

}

// check to see if a frame came from me
// returns true if I originated the frame
BOOL FrameisMine(IP400_FRAME *frame)
{
	char decCall[30];

	// check if I am the originator call sign
	uint16_t myIPAddr = GetVPNLowerWord();
	callDecode(&frame->source, decCall, NULL);
	if(CompareToMyCall(decCall) && (frame->source.vpnBytes.encvpn == myIPAddr))
		return TRUE;

	// now check to see if I repeated this frame
	if(frame->flagfld.flags.hoptable == 0)
		return FALSE;

	// check my call is in the table
	HOPTABLE *htable = (HOPTABLE *)frame->hopTable;
	IP400_MAC *myMac;
	GetMyMAC(&myMac);
	for(int i=0;i<frame->flagfld.flags.hop_count; i++)
		if((htable[i].hopAddr.callbytes.encoded == myMac->callbytes.encoded) && (htable[i].hopAddr.vpnBytes.encvpn == myIPAddr))
			return TRUE;

	return FALSE;
}

/*
 * Repeat a frame. Do not send it if we were the originator
 * Inbound frame is static, not malloc'd
 */
void RepeatFrame(IP400_FRAME *frame)
{
	IP400_FRAME *rptFrame;

	// copy the frame
	if((rptFrame=malloc(sizeof(IP400_FRAME))) == NULL)
		return;
	memcpy(rptFrame, frame, sizeof(IP400_FRAME));

	// copy the data
	rptFrame->length = frame->length;
	if((rptFrame->buf=malloc(frame->length)) == NULL)	{
		free(rptFrame);
		return;
	}
	memcpy(rptFrame->buf, frame->buf, frame->length);

	// allocate a new hop table
	uint8_t hopCount= frame->flagfld.flags.hop_count;
	if((rptFrame->hopTable=malloc(sizeof(HOPTABLE)*(hopCount+1))) == NULL)	{
		free(rptFrame->buf);
		free(rptFrame);
	}

	// copy the existing one and add me to the end of it
	if(frame->flagfld.flags.hoptable)		{
		memcpy(rptFrame->hopTable, frame->hopTable, sizeof(HOPTABLE)*hopCount);
	}

	// build a new entry
	HOPTABLE *table = (HOPTABLE *)rptFrame->hopTable;
	IP400_MAC *myMac;
	GetMyMAC(&myMac);
	table[hopCount].hopAddr.callbytes.encoded = myMac->callbytes.encoded;
	table[hopCount].hopAddr.vpnBytes.encvpn = myMac->vpnBytes.encvpn;
	rptFrame->flagfld.flags.hoptable = TRUE;
	rptFrame->flagfld.flags.hop_count = hopCount + 1;

	FRAME_STATS *stats = GetFrameStats();
	stats->nRepeated++;

	QueueTxFrame(rptFrame);
}

/*
 * ------------------------------------------------------------------------
 * 	Frame Reception handlers
 * ------------------------------------------------------------------------
 */

/*
 * Process a received frame
 */
void ProcessRxFrame(IP400_FRAME *rFrame, int rawLength)
{
	FRAME_STATS *stats = GetFrameStats();

	// find a reason to reject a frame...
	BOOL isMine = FrameisMine(rFrame);

	// process the frame if it is not mine and unique
	// do a sanity check on the length
	if(!isMine && (rFrame->length < rawLength))		{

		IP400FrameType frameType = rFrame->flagfld.flags.coding;

		switch(frameType)	{

		// process a beacon frame
		case BEACON_PACKET:
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				Mesh_ProcessBeacon((void *)rFrame, stats->lastRSSI);
#if __DUMP_BEACON
				EnqueChatFrame((void *)&rFrame);
#endif
				EnqueSPIFrame(rFrame);
				stats->nBeacons++;
			}
			break;

		// process a local chat frame
		case UTF8_TEXT_PACKET:
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				EnqueChatFrame((void *)rFrame);
				stats->framesOK++;
			}
			break;

		// frames passed on to the host
		case COMPRESSED_AUDIO:		// compressed audio packet
		case COMPREESSD_VIDEO:		// compressed video packet
		case DATA_PACKET:			// data packet
		case IP_ENCAPSULATED:		// IP encapsulated packet
		case AX_25_PACKET:			// AX.25 encapsulated packet
		case RFC4733_DTMF:			// DTMF packet
		case DMR_FRAME:				// DMR Frame
		case DSTAR_FRAME:			// Dstar Frame
		case P25_FRAME:				// TIA project 25
		case NXDN_FRAME:			// NXDN
		case M17_FRAME:				// M17
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				EnqueSPIFrame((void *)rFrame);
				stats->framesOK++;
			}
			break;

		// echo request frame
		case ECHO_REQUEST:
			SendEchoRespFrame(rFrame);
			break;

		// echo response: treat it like a chat frame
		case ECHO_RESPONSE:
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				EnqueChatFrame((void *)rFrame);
				stats->framesOK++;
			}
			break;

	    //reserved for future use
		case LOCAL_COMMAND:			// local command frame
			break;

		default:
			stats->dropped++;
			logger(LOG_ERROR, "Frame Received with unknown coding: %d\r\n", rFrame->flagfld.flags.coding);
			break;
		}
	}

	// repeat the frame if the repeat flag is set and the hop count is not exhausted
	if(!isMine)	{
		if(rFrame->flagfld.flags.repeat && (rFrame->flagfld.flags.hop_count < MAX_HOP_COUNT))
			RepeatFrame(rFrame);
	}

	if(isMine)
		stats->dropped++;
}


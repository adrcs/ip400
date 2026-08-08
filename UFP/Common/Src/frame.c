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

#ifdef __NUCLEO_BOARD
#include "wl33.h"
#include <stm32wl3x_nucleo.h>
#include <stm32wl3x_hal_mrsubg.h>
#endif

#include "frame.h"
#include "dataq.h"
#include "setup.h"
#include "ip.h"
#include "spi.h"
#include "tasks.h"
#include "kiss.h"
#include "setup.h"
#include "memory.h"
#include "xcvr.h"

// local debug
#define	__DUMP_BEACON		0		// dump a beacon frame
#define	__BEACON2SPI		1		// beacon on spi

// local defn's
typedef enum call_stat_e	{
	CALLSIGN_NOT_FOUND=0,			// callsign not found
	CALLSIGN_IN_ADDRESS,			// callsign in address
	FRAME_NEEDS_REPEATING			// frame needs repeating
}CallsignStatus;					// call sign status

uint32_t  nextSeq;		// next frame sequence number
FRAME_STATS frStats;	// frame stats

FRAME_STATS *GetFrameStats(void)
{
	return &frStats;
}

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

	uint8_t *allocBuf;

	if((allocBuf = nodeMemAlloc(FRAME,length)) == NULL)			// cannot allocate heap memory
			return FALSE;
	memcpy(allocBuf, buf, length);

	return SendDataFrame(srcCall, srcIPAddr, destCall, dstIPAddr, allocBuf, length, UTF8_TEXT_PACKET, repeat);

}

/*
 * compose and send a beacon frame (ping frame with broadcast destination)
 * Same comment about SendDataFrame.
 * Frame is in static memory, copy to the heap
 */
BOOL SendBeaconFrame(uint8_t *payload, int bcnlen)
{
	//return FALSE;										// avoid beacon on startup

	char *destCall = "FFFF";
	uint16_t destIP = 0xFFFF;					// broadcast destination address
	uint8_t *allocBuf;

	uint16_t srcIPAddr = GetVPNLowerWord();			// get my IP lower bits
	if((allocBuf = nodeMemAlloc(FRAME,bcnlen)) == NULL)			// cannot allocate heap memory
			return FALSE;
	memcpy(allocBuf, payload, bcnlen);

	return SendDataFrame(setup_memory.params.setup_data.stnCall, srcIPAddr, destCall, destIP, allocBuf, bcnlen, BEACON_PACKET, TRUE);
}

/*
 * Send an echo request frame
 * Request frame is in static memory, copy it to the heap
 */
BOOL SendEchoReqFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, char *buf, uint16_t length, BOOL repeat)
{

	uint8_t *allocBuf;

	if((allocBuf = nodeMemAlloc(FRAME,length)) == NULL)			// cannot allocate heap memory
			return FALSE;
	memcpy(allocBuf, buf, length);

	return SendDataFrame(srcCall, srcIPAddr, destCall, dstIPAddr, allocBuf, length, ECHO_REQUEST, FALSE);
}

/*
 * send an echo response
 */
BOOL SendEchoRespFrame(IP400_FRAME *reqFrame)
{
	IP400_FRAME *echoFrame;

	if((echoFrame=nodeMemAlloc(FRAME,sizeof(IP400_FRAME))) == NULL)
		return FALSE;

	// swap source/destination
	echoFrame->source.callbytes.callsign.encoded = reqFrame->dest.callbytes.callsign.encoded;
	echoFrame->source.vpnBytes.encvpn = reqFrame->dest.vpnBytes.encvpn;

	echoFrame->dest.callbytes.callsign.encoded = reqFrame->source.callbytes.callsign.encoded;
	echoFrame->dest.vpnBytes.encvpn = reqFrame->source.vpnBytes.encvpn;

	// copy the payload in: already in alloc'd memory
	echoFrame->buf = reqFrame->buf;
	echoFrame->length = reqFrame->length;
	echoFrame->hopTable = reqFrame->hopTable;
	echoFrame->flagfld.flags.hoptable = reqFrame->flagfld.flags.hoptable;
	nodeMemFree(FRAME,reqFrame);

	echoFrame->flagfld.allflags = 0;		// start with all flags cleared
	echoFrame->flagfld.flags.coding = ECHO_RESPONSE;
	echoFrame->hopTable = NULL;
	echoFrame->seqNum = nextSeq++;

	QueueTxFrame(echoFrame, DEFAULT_MODEM);

	return TRUE;
}

/*
 * send a reformatted KISS Frame
 */
BOOL SendKissFrame(IP400_FRAME *kissFrame, uint8_t port)
{
	kissFrame->flagfld.flags.coding = AX_25_PACKET;
	kissFrame->seqNum = nextSeq++;

	QueueTxFrame(kissFrame, port);

	return TRUE;
}

/*
 * Send a generic data frame
 * Frame is created in heap memory, payload is already there
 * TODO: Add in modulation, fec and constellation to flags
 */
BOOL SendDataFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, uint8_t *data, uint16_t length, uint8_t coding, BOOL repeat)
{
	IP400_FRAME *txFrame;


	if((txFrame=nodeMemAlloc(FRAME,sizeof(IP400_FRAME))) == NULL)
		return FALSE;

	// assume that the payload is already in alloc'd memory
	// avoid a second allocation for the same data
	txFrame->buf = data;

	txFrame->flagfld.allflags = 0;		// start with all flags cleared

	// format the header
	callEncode(srcCall, srcIPAddr, txFrame, SRC_CALLSIGN);
	callEncode(destCall, dstIPAddr, txFrame, DEST_CALLSIGN);

	txFrame->length = length;

	// flag fields
	RADIO_SETUP *setup = getRadioSetup(DEFAULT_MODEM);
	txFrame->flagfld.flags.fragmentation = FRAG_SELFCONTAINED;
	txFrame->flagfld.flags.coding = coding;
	txFrame->flagfld.flags.hoptable = FALSE;
	txFrame->flagfld.flags.payloadMSB = (length & 0x100) >> 8;
	txFrame->hopTable = NULL;
	txFrame->seqNum = nextSeq++;

	txFrame->flagfld.flags.bitsperCarr = setup->xModulationSelect;

#if	 __XCVR_OFDM_AB
	txFrame->flagfld.flags.bandwidth = setup->bandwidth;
	txFrame->flagfld.flags.FEC = (setup->defFEC != OFDM_FEC_NONE) ? TRUE : FALSE;
#else
	txFrame->flagfld.flags.bandwidth = 0;
	txFrame->flagfld.flags.FEC = FALSE;
#endif

	QueueTxFrame(txFrame, DEFAULT_MODEM);

	return TRUE;
}


/*
 * Send a frame received on the SPI
 * NB: input frame has a different format
 * direct copy from frame.c
 */
void SendSPIFrame(void *spi, uint8_t *payload, int len)
{
	/*
	 * new frame format: *spi = <header> *payload = <src ext><dest ext><hop Table><payload>
	 */

	IP400_FRAME *ip400Frame;

	SPI_HEADER *spiHdr = (SPI_HEADER *)spi;
	SPI_BYTE_FLAGS spiFlags;

	// allocate memory for the SPI frame and buffer
	if((ip400Frame=nodeMemAlloc(FRAME,sizeof(IP400_FRAME))) == NULL)
		return;

	// step 0: callsigns first
	memcpy(&ip400Frame->source.callbytes.callsign.bytes, &spiHdr->fromCall, N_CALL);
	memcpy(&ip400Frame->source.vpnBytes.vpn, &spiHdr->fromIP, N_IPBYTES);
	memcpy(&ip400Frame->dest.callbytes.callsign.bytes, &spiHdr->toCall, N_CALL);
	memcpy(&ip400Frame->dest.vpnBytes.vpn, &spiHdr->toIP, N_IPBYTES);
	spiFlags.bytedefs[1] = spiHdr->flagsLSB;
	spiFlags.bytedefs[0] = spiHdr->flagsMSB;

	if(spiFlags.bitdefs.srcExt)	{
		memcpy(&ip400Frame->srcExt, payload, N_CALL);
		payload += N_CALL;
		len -= N_CALL;
	}
	if(spiFlags.bitdefs.destExt)	{
		memcpy(&ip400Frame->destExt, payload, N_CALL);
		payload += N_CALL;
		len -= N_CALL;
	}

	// step 2: process hop table entry next
	HOPTABLE *hTable;
	SPI_HOPTABLE *spiHop = (SPI_HOPTABLE *)payload;
	if(spiFlags.bitdefs.hoptable)		{
		if((hTable=nodeMemAlloc(FRAME,sizeof(HOPTABLE))) == NULL)	{
			nodeMemFree(FRAME,ip400Frame);
			return;
		}
		for(int i=0;i<MAX_HOPS;i++)		{
			hTable->rptCalls[i].callbytes.callsign.encoded = spiHop->hopEntry->callentry.callsign.encoded;
			hTable->hopflags[i].flags = spiHop->hopEntry->flags;
		}
		payload += sizeof(SPI_HOPTABLE);
		len -= sizeof(SPI_HOPTABLE);
		ip400Frame->hopTable = hTable;
	} else {
		ip400Frame->hopTable = (void *)NULL;
		ip400Frame->flagfld.flags.hoptable = FALSE;
	}

	// Step 2: the rest is the payload
	if((ip400Frame->buf=nodeMemAlloc(FRAME,len)) == NULL)	{
		if(ip400Frame->flagfld.flags.hoptable)
			nodeMemFree(FRAME,hTable);
		nodeMemFree(FRAME,ip400Frame);
		return;
	}
	memcpy(ip400Frame->buf, payload, len);

	// step 4: header flags
	ip400Frame->flagfld.flagBytes[0] = spiFlags.bytedefs[0];
	ip400Frame->flagfld.flagBytes[1] = spiFlags.bytedefs[1];

	// fill in the flag fields
	ip400Frame->flagfld.flags.fragmentation = spiHdr->spiStat - SINGLE_FRAME;
	ip400Frame->flagfld.flags.coding = spiHdr->coding & 0xf;
	ip400Frame->flagfld.flags.bandwidth = spiFlags.bitdefs.bandwidth;
	ip400Frame->flagfld.flags.FEC = (spiFlags.bitdefs.FECMethod > 0) ? TRUE : FALSE;
	ip400Frame->flagfld.flags.bitsperCarr = spiFlags.bitdefs.bitsperCarr;
	ip400Frame->flagfld.flags.payloadMSB = (len & 0x100) >> 8;

	ip400Frame->seqNum = nextSeq++;
	ip400Frame->length = len & 0xFF;

	uint8_t modemAddr = spiFlags.bitdefs.modem;
	QueueTxFrame(ip400Frame, modemAddr);

}

// check to see if a frame came from me
// returns a value if I originated the frame
// or am in the hop table
// check both VPN and AX25 addressing modes
CallsignStatus FindCallinFrame(IP400_FRAME *frame)
{
	char decCall[30];

	// check if I am the originator call sign
	uint16_t myVPNAddr = GetVPNLowerWord();
	STN_PARAMS *stn_params = GetStationParams();			// get the station params

	callDecode(frame, decCall, NULL, SRC_CALLSIGN);
	if(CompareToMyCall(decCall)) 	{
		// check the VPN address
		if(frame->source.vpnBytes.encvpn == myVPNAddr)
				return CALLSIGN_IN_ADDRESS;
		// check the AX.25 compatibility mode as well
		if(isAX25Enabled())	{
			if((frame->source.vpnBytes.encvpn&AX25_VPN_MASK) == AX25_VPN_BASE)	{
				if((frame->source.vpnBytes.encvpn&AX25_SSID_MASK) == stn_params->setup_data.flags.SSID)
					return CALLSIGN_IN_ADDRESS;
			}
		}
	}

	// now check to see if I am in the hop table already
	if(frame->flagfld.flags.hoptable == 0)
		return CALLSIGN_NOT_FOUND;

	// check my call is in the table
	// two different meanings:
	HOPTABLE *htable = (HOPTABLE *)frame->hopTable;
	IP400_MAC *myMac;
	GetMyMAC(&myMac);
	for(int i=0;i<MAX_HOPS; i++)	{
		if(htable->rptCalls[i].callbytes.callsign.encoded == myMac->callbytes.callsign.encoded)	{
			// if my call is the hop table in ax25 mode, and the 'h' bit is clear, repeat it
			if(isAX25Enabled())	{
				if((htable->rptCalls[i].vpnBytes.ax25vpn.ssid == stn_params->setup_data.flags.SSID) &&
						!htable->rptCalls[i].vpnBytes.ax25vpn.c_h_bit) {
					htable->rptCalls[i].vpnBytes.ax25vpn.c_h_bit = TRUE;
					return FRAME_NEEDS_REPEATING;
				}
			} else {
				if(htable->rptCalls[i].vpnBytes.encvpn == myVPNAddr)
					return CALLSIGN_IN_ADDRESS;
			}
		}
	}
	return CALLSIGN_NOT_FOUND;
}

/*
 * Repeat a frame in ip400 mode
 * copy the existing hop table (if any), and add me to the end of it
 * the rest of the frame remains intact
 */
void RepeatIP400Frame(IP400_FRAME *frame, BOOL addHopEntry)
{
	IP400_FRAME *rptFrame;

	BOOL hasHopTable = frame->flagfld.flags.hoptable;

	// create a new frame from the one to repeat
	if((rptFrame=nodeMemAlloc(FRAME,sizeof(IP400_FRAME))) == NULL)
		return;
	memcpy(rptFrame, frame, sizeof(IP400_FRAME));

	// copy the payload data
	if((rptFrame->buf=nodeMemAlloc(FRAME,frame->length)) == NULL)	{
		nodeMemFree(FRAME,rptFrame);
		return;
	}
	memcpy(rptFrame->buf, frame->buf, frame->length);

	if(hasHopTable)	{
		// allocate a new hop table
		if((rptFrame->hopTable=nodeMemAlloc(FRAME,sizeof(HOPTABLE))) == NULL)	{
			if(rptFrame->hopTable)
				nodeMemFree(FRAME,rptFrame->buf);
			nodeMemFree(FRAME,rptFrame);
			return;
		}
		memcpy(rptFrame->hopTable, frame->hopTable, sizeof(HOPTABLE));
		nodeMemFree(FRAME,frame->hopTable);
	}

	QueueTxFrame(rptFrame, DEFAULT_MODEM);
}

/*
 * Delete a frame in allocated memory
 */
void DeleteFrame(IP400_FRAME *fr)
{
	if(fr->hopTable != NULL)
		nodeMemFree(FRAME, fr->hopTable);
	if(fr->buf)
		nodeMemFree(FRAME, fr->buf);
	nodeMemFree(FRAME, fr);
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
	// find a reason to reject a frame...
	CallsignStatus callStat = FindCallinFrame(rFrame);

	/*
	 * Handle frame repeating first...
	 */
	switch(callStat)	{

	// not my callsign and not in the hop table: process normally
	case CALLSIGN_NOT_FOUND:
		break;

	// I originated the frame or repeated it already, so discard it
	case CALLSIGN_IN_ADDRESS:
		DeleteFrame(rFrame);
		frStats.nWereMine++;
		return;

	// AX.25 frame needs to be repeated
	case FRAME_NEEDS_REPEATING:
		RepeatIP400Frame(rFrame, FALSE);
		frStats.nRepeated++;
		DeleteFrame(rFrame);
		return;
	}

	frStats.nProcessed++;

#if	__XCVR_AT86					// has a AT86RF215
	RADIO_STATS *stats = GetRadioStats(XCVR_AT86_SUBG);
#endif
#if	__XCVR_OFDM_AB				// has an OFDM transceiver
	RADIO_STATS *stats = GetRadioStats(XCVR_OFDM);
#endif
#if	__XCVR_WL33					// has a WL33
	RADIO_STATS *stats = GetRadioStats(XCVR_WL33);
#endif


	// the only one to drop through is CALLSIGN_NOT_FOUND
	IP400DataType frameType = rFrame->flagfld.flags.coding;

	// process the frame if it is not mine and unique
	// do a sanity check on the length
	if(rFrame->length <= rawLength)		{

		switch(frameType)	{

		// process a local chat frame
		case UTF8_TEXT_PACKET:
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				EnqueChatFrame((void *)rFrame);
				frStats.nChat++;
			} else frStats.nRejected++;
			break;

		// process a beacon frame
		case BEACON_PACKET:
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				Mesh_ProcessBeacon((void *)rFrame, stats->lastRSSI);
#if __DUMP_BEACON
				EnqueChatFrame((void *)&rFrame);
#endif
#if __BEACON2SPI
				EnqueSPIFrame(rFrame);
#else
				DeleteFrame(rFrame);
#endif
				frStats.nBeacons++;
			} else {
				frStats.nRejected++;
			}
			break;


		// use kiss mode output if enabled, else send it out the SPI
		case AX_25_PACKET:			// AX.25 encapsulated packet
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
#if __INCLUDE_KISS
				if(isAX25Enabled())
					ProcessRxKissFrame(rFrame);					// send out as a kiss frame
				else
					EnqueSPIFrame((void *)rFrame);				// duplicate on SPI as well
#else
				EnqueSPIFrame((void *)rFrame);
#endif
				frStats.nKiss++;
			} else frStats.nRejected++;
			break;

		// echo request frame
		case ECHO_REQUEST:
			SendEchoRespFrame(rFrame);
			frStats.nEchoReq++;
			break;

		// echo response: treat it like a chat frame
		case ECHO_RESPONSE:
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				EnqueChatFrame((void *)rFrame);
				frStats.nEchoResp++;
			} else frStats.nRejected++;
			break;

	    //reserved for future use
		case LOCAL_COMMAND:			// local command frame
			break;

		default:			// user defined frame
			if(Mesh_Accept_Frame((void *)rFrame, stats->lastRSSI))	{
				EnqueSPIFrame((void *)rFrame);
				frStats.nUndecoded++;
			} else frStats.nRejected++;
			break;

		}
	} else {
		frStats.Unknown++;
		DeleteFrame(rFrame);
	}
}

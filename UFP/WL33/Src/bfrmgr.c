/*---------------------------------------------------------------------------
	Project:	      IP400 Unified Firmware Platform

	Module:		      Buffer Manager

	File Name:	      bfrmgr.c

	Date Created:	  Mar 17, 2026

	Author:			  MartinA

	Description:      Manage buffering for WL33 Mode A based transceivers

					  Copyright © 2024-26, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/
#include <string.h>
#include <stdlib.h>

#include "frame.h"
#include "bfrmgr.h"
#include "memory.h"
#include "tasks.h"
#include "dataq.h"
#include "usart.h"

// local defines
#define	XMIT_INTERVAL		160				// 160 ms transmit interval
#define	TIMER_VAL 			(XMIT_INTERVAL/XCVR_TASK_SCHED)

// transmit buffers
BUFFER_STATUS txBufferStatus =	{
		BUFFER_UNALLOC, NULL, 0
};

// receive buffer
RAWBUFFER	*rxBuffer;			// rx buffer
char *bfrHeader = "IP4CV2";

// frame queues
FRAME_QUEUE			rawFrameQ;	// raw frame queue

// local constants
uint8_t txTimer;				// tx interval timer

// forward refs
void IP4002Buf(IP400_FRAME *tFrame);

/*
 * Initialize the buffer task
 */
BOOL BufferTask_init(void)
{

	// init rx queue
	rawFrameQ.q_forw = &rawFrameQ;
	rawFrameQ.q_back = &rawFrameQ;

	// allocate rx buffers
	if((rxBuffer = (RAWBUFFER *)nodeMemAlloc(BUFFERS,BFR_SIZE)) == NULL)
		return FALSE;

	if((txBufferStatus.addr = (RAWBUFFER *)nodeMemAlloc(BUFFERS,BFR_SIZE)) == NULL)	{
		nodeMemFree(BUFFERS,rxBuffer);
		return FALSE;
	}
	txBufferStatus.state = BUFFER_EMPTY;
	txBufferStatus.length = 0;
	txBufferStatus.tmrState = TMR_NOTRUNNING;
	txBufferStatus.txSize = BFR_SIZE;

	txTimer = TIMER_VAL;

	return TRUE;
}

/*
 * Run the task
 */
void BufferTask_Exec(void)
{
	// check the tx buffer state
	if(txBufferStatus.tmrState == TMR_RUNNING)	{
		// not ready to transmit yet
		if(txBufferStatus.tmrValue != 0)	{
			txBufferStatus.tmrValue--;
		} else {
			txBufferStatus.tmrState = TMR_EXPIRED;
		}
	}
}

/*
 * Tx API for transceiver state machines
 */

BOOL TxHasRoom(int length)
{
	BOOL hasRoom = (length < BFR_SIZE - txBufferStatus.length);

	// empty buffers always have room...
	if(txBufferStatus.state == BUFFER_EMPTY)
		return TRUE;

	// filling up..
	if(hasRoom && (txBufferStatus.state == BUFFER_ACTIVE))
		return TRUE;

	return FALSE;
}

// is tx ready to roll?
BOOL IsTxReady(void)
{
	if(txBufferStatus.tmrState == TMR_EXPIRED)	{
		RAWBUFFER *buf = txBufferStatus.addr;
		buf += strlen(bfrHeader);
		memcpy(buf, &txBufferStatus.length, sizeof(uint16_t));
		txBufferStatus.nXmitted++;
		return TRUE;
	}

	return FALSE;
}

// put a frame in the buffer: TxHas room must be called first
BOOL PutTxBuffer(IP400_FRAME *fr)
{

	// if the buffer is empty, add in the header..
	if(txBufferStatus.state == BUFFER_EMPTY)	{
		strcpy((char *)txBufferStatus.addr, bfrHeader);
		txBufferStatus.length = strlen(bfrHeader) + sizeof(uint16_t);
		txBufferStatus.state = BUFFER_ACTIVE;
		txBufferStatus.tmrState = TMR_RUNNING;
		txBufferStatus.tmrValue = TIMER_VAL;
	}

	// add this to the buffer if it is active
	if(txBufferStatus.state == BUFFER_ACTIVE)		{
		IP4002Buf(fr);
		txBufferStatus.nFrames++;
		return TRUE;
	}

	return FALSE;
}

// get the tx buffer length
uint16_t GetTxBufferLength(void)
{
	uint16_t pktLen = txBufferStatus.length;
	uint16_t rounddown = pktLen - (pktLen % 4);
	if(rounddown < pktLen)
		pktLen = rounddown + sizeof(uint32_t);
	else
		pktLen = rounddown;
 	return pktLen;
}

// get buffer addresses
void *GetTxBufferAddr(void)
{
	return (void *)txBufferStatus.addr;
}

// return buffer half-way address
void *GetTxBufferAltAddr(void)
{
	return (void *)txBufferStatus.addr + (BFR_SIZE/2);
}

// set tx done
void SetTxBufferDone(void)
{
	txBufferStatus.state = BUFFER_EMPTY;
	txBufferStatus.tmrState = TMR_NOTRUNNING;
	txBufferStatus.length = 0;
}

// put a frame in the buffer
void IP4002Buf(IP400_FRAME *tFrame)
{
	/*
	 * buffer has consecutive frames up to the max frame length.
	 * Each frame starts with a length, and a null on the end
	 */
	RAWBUFFER *cpyDest = txBufferStatus.addr + txBufferStatus.length;					// Save start pointer for length calculation

	// first put in the overall frame length
	uint16_t frameLen = 2*(IP_400_MAC_SIZE) +IP_400_FLAG_SIZE + sizeof(uint16_t) + sizeof(uint32_t) + 2*IP_400_CALL_SIZE;
	if(tFrame->flagfld.flags.hoptable)
		frameLen += MAX_HOPS*(IP_400_MAC_SIZE+sizeof(uint8_t));
	uint16_t payloadLen = (uint16_t)tFrame->flagfld.flags.payloadMSB;
	payloadLen = (payloadLen << 8) + (uint16_t)tFrame->length;
	frameLen += payloadLen;
	memcpy(cpyDest, (uint8_t *)&frameLen, sizeof(uint16_t));
	cpyDest += sizeof(uint16_t);

	txBufferStatus.length += frameLen + sizeof(uint16_t);		// add in the length bytes..

	/*
	 * Build the raw frame bytes: see IP400_FRAME struct (28 bytes)
	 */
	// Source call + VPN (6 bytes)
	memcpy(cpyDest, (uint8_t *)&tFrame->source, IP_400_MAC_SIZE);
	cpyDest += IP_400_MAC_SIZE;

	// Dest call + VPN (6 bytes)
	memcpy(cpyDest, (uint8_t *)&tFrame->dest, IP_400_MAC_SIZE);
	cpyDest += IP_400_MAC_SIZE;

	// flag byte (2 byte)
	memcpy(cpyDest, (uint8_t *)tFrame->flagfld.flagBytes, IP_400_FLAG_SIZE);
	cpyDest += IP_400_FLAG_SIZE;

	// frame data length (2 bytes)
	memcpy(cpyDest, (uint8_t *)&payloadLen, sizeof(uint16_t));
	cpyDest += sizeof(uint16_t);

	// frame sequence number (4 bytes)
	memcpy(cpyDest, (uint32_t *)&tFrame->seqNum, sizeof(uint32_t));
	cpyDest += sizeof(uint32_t);

	// source call extension (4 bytes)
	memcpy(cpyDest, (uint8_t *)&tFrame->srcExt, IP_400_CALL_SIZE);
	cpyDest += IP_400_CALL_SIZE;

	// dest call extension (4 bytes)
	memcpy(cpyDest, (uint8_t *)&tFrame->destExt, IP_400_CALL_SIZE);
	cpyDest += IP_400_CALL_SIZE;

	// add in the hop table
	if(tFrame->flagfld.flags.hoptable)	{
		HOPTABLE *hTable = (HOPTABLE *)tFrame->hopTable;
		int copysize = MAX_HOPS;
		for(int k=0;k<copysize;k++)	{
			memcpy(cpyDest, hTable->rptCalls[k].callbytes.callsign.bytes, IP_400_MAC_SIZE);
			cpyDest += IP_400_MAC_SIZE;
		}
		for(int k=0;k<copysize;k++)	{
			*cpyDest++ = hTable->hopflags[k].flagbyte;
		}
		nodeMemFree(FRAME,tFrame->hopTable);
	}

	// and now the data...
	if((tFrame->buf != NULL) && (payloadLen != 0)) {
		memcpy(cpyDest, tFrame->buf, payloadLen);
		cpyDest += payloadLen;
	}

	// free the allocations in the reverse order...
	if(tFrame->buf != NULL)
		nodeMemFree(FRAME,tFrame->buf);

	nodeMemFree(FRAME,tFrame);


}


/*
 * Rx API for transceiver state machines
 */

// process a completed receive buffer
// break up the buffer into individual raw frames
// Queue for later processing
void SetRxDone(void)
{
	uint16_t pktlen;
	int rxLength;
	RAWBUFFER *bfrAddr = rxBuffer;
	RAWBUFFER *frame;

	// if it is not this version; drop it
	if(strncmp((char *)rxBuffer, bfrHeader, strlen(bfrHeader)))
		return;

	size_t hdrSize = strlen(bfrHeader);
	bfrAddr += hdrSize;

	// get the overall buffer length
	uint16_t bfrLen;
	memcpy(&bfrLen, bfrAddr, sizeof(uint16_t));
	bfrAddr += sizeof(uint16_t);
	rxLength = (int)bfrLen - strlen(bfrHeader) - sizeof(uint16_t);

	// get the packet head address
	RAWBUFFER *pktHead = bfrAddr;

	while(rxLength > 0)		{

		// get the packet data length
		memcpy(&pktlen, pktHead, sizeof(uint16_t));
		bfrAddr = pktHead + sizeof(uint16_t);

		uint16_t occupiedLen = pktlen + sizeof(uint16_t);

		// leave here if memory exhausted
		if((frame = nodeMemAlloc(BUFFERS, pktlen)) == NULL)
			return;

		// copy the packet data
		memcpy(frame, bfrAddr, pktlen);
		enqueFrame(&rawFrameQ, (IP400_FRAME *)frame, pktlen);

		pktHead += occupiedLen;
		rxLength -= occupiedLen;
	}
}


// return true if rx has data
BOOL RxHasData(void)
{
	return quehasData(&rawFrameQ);
}

// get the rx buffer address
uint8_t *GetRxBufferAddr(void)
{
	return rxBuffer;
}

// get next frame from the buffer
RAWBUFFER *getRxBufferFrame(void)
{
	return (RAWBUFFER *)dequeFrame(&rawFrameQ);
}

/*
 * Do the opposite of the transmitter...
 * frame must be alloc'd, just like tx frame
 */
IP400_FRAME *Buf2IP400(void *RawFrame)
{

	uint8_t *cpyDest;
	IP400_FRAME *rFrame;
	RAWBUFFER *RxRaw = RawFrame;
	HOPTABLE *hTable;

	if((rFrame = nodeMemAlloc(FRAME,sizeof(IP400_FRAME))) == NULL)
		return NULL;

	// Source call + VPN (6 bytes)
	cpyDest = (uint8_t *)&rFrame->source;
	memcpy(cpyDest, RxRaw, IP_400_MAC_SIZE);
	RxRaw += IP_400_MAC_SIZE;

	// Dest call + VPN (6 bytes)
	cpyDest = (uint8_t *)&rFrame->dest;
	memcpy(cpyDest, RxRaw, IP_400_MAC_SIZE);
	RxRaw += IP_400_MAC_SIZE;

	// flag byte (2 byte)
	cpyDest = (uint8_t *)rFrame->flagfld.flagBytes;
	memcpy(cpyDest, RxRaw, IP_400_FLAG_SIZE);
	RxRaw += IP_400_FLAG_SIZE;

	// frame length (2 bytes)
	uint16_t frameLen;
	memcpy(&frameLen, RxRaw, sizeof(uint16_t));
	rFrame->length = (uint8_t)(frameLen & 0xff);
	rFrame->flagfld.flags.payloadMSB = frameLen>>8;
	RxRaw += sizeof(uint16_t);

	// frame sequence number (4 bytes)
	cpyDest = (uint8_t *)&rFrame->seqNum;
	memcpy(cpyDest, RxRaw, sizeof(uint32_t));
	RxRaw += sizeof(uint32_t);

	// source call extension
	cpyDest = (uint8_t *)&rFrame->srcExt;
	memcpy(cpyDest, RxRaw, IP_400_CALL_SIZE);
	RxRaw += IP_400_CALL_SIZE;

	// dest call extension
	cpyDest = (uint8_t *)&rFrame->destExt;
	memcpy(cpyDest, RxRaw, IP_400_CALL_SIZE);
	RxRaw += IP_400_CALL_SIZE;

	// copy the hop table

	rFrame->hopTable = NULL;
	// add in the hop table
	if(rFrame->flagfld.flags.hoptable)	{
		if((rFrame->hopTable = nodeMemAlloc(FRAME,sizeof(HOPTABLE))) != NULL)	{
			hTable = (HOPTABLE *)rFrame->hopTable;
			int copysize = MAX_HOPS;
			for(int k=0;k<copysize;k++)	{
				memcpy(hTable->rptCalls[k].callbytes.callsign.bytes, RxRaw, IP_400_MAC_SIZE);
				RxRaw += IP_400_MAC_SIZE;
			}
			for(int k=0;k<copysize;k++)
				hTable->hopflags[k].flagbyte = *RxRaw++;
		}
	}

	// allocate the data buffer
	if((rFrame->buf=nodeMemAlloc(FRAME,frameLen)) == NULL)	{
		if(rFrame->hopTable != NULL)
			nodeMemFree(FRAME,rFrame->hopTable);
		nodeMemFree(FRAME,rFrame);
		return NULL;
	}

	memcpy(rFrame->buf, RxRaw, frameLen);

	return rFrame;
}

/*
 * Status monitoring
 */
BUFFER_STATUS *getBufferStatus(void)
{
	return &txBufferStatus;
}



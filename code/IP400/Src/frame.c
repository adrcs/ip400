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
#if _BOARD_TYPE==NUCLEO_BOARD
#include <stm32wl3x_nucleo.h>
#endif


#include <cmsis_os2.h>
#include <FreeRTOS.h>
#include <semphr.h>

#include "frame.h"
#include "dataq.h"
#include "setup.h"
#include "tasks.h"
#include "led.h"

// local defines
#define	RX_TIMEOUT		2		// return to OS after 5 ms timeout

// conditionals
#define	__DUMP_BEACON	0		// dump a beacon frame to console using chat

// transmit states
enum	{
		TX_IDLE=0,		// idle - waiting for work
		TX_SENDING,		// sending a frame
		TX_DONE			// done
};

// locals
uint8_t	  txState;		// transmitter state
uint8_t	  radioCmd;		// current radio command
uint8_t	  prevCmd;		// previous state
FRAME_QUEUE	txQueue;	// transmitter frame queue
uint32_t subgIRQStatus;	// interrupt status
BOOL	  txDone;		// transmitter is done
BOOL	  rxReady;		// rx is ready...
FRAME_STATS Stats;		// collected stats
uint32_t  nextSeq;		// next frame sequence number

// from setup...
int16_t  rxSquelch;		// rx squlech

// internal fwd references
void QueueTxFrame(IP400_FRAME *txframe);		// send a frame
void EnableRx(void);							// enable the rx

// receive raw frame: we are only interested
static uint8_t rawTxFrame[MAX_FRAME_SIZE];
static uint8_t rawRxFrame[MAX_FRAME_SIZE];

// processed frames
static IP400_FRAME rFrame;

// intialize the transmit task
void Frame_task_init(void)
{
	txState = TX_IDLE;
	txQueue.q_forw = &txQueue;
	txQueue.q_back = &txQueue;

	// init stats and counters
	memset(&Stats, 0, sizeof(FRAME_STATS));
	nextSeq = 0xFFFFFFFF;

	// set Rx threshold
	STN_PARAMS *params = GetStationParams();
	rxSquelch = params->radio_setup.rxSquelch;
	HAL_MRSubG_SetRSSIThreshold(rxSquelch);

	// enable the interrupt
	__HAL_MRSUBG_SET_RFSEQ_IRQ_ENABLE(
			MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_OK_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_TX_DONE_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_TIMEOUT_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_CRC_ERROR_E
	);
    HAL_NVIC_EnableIRQ(MR_SUBG_IRQn);

    // enable the Rx
    rxReady = FALSE;
    EnableRx();
}

/*
 * return the stats
 */
FRAME_STATS *GetFrameStats(void)
{
	return &Stats;
}
// return the radio status
uint32_t GetRadioStatus(void)
{
	uint32_t radioStats = READ_REG(MR_SUBG_GLOB_STATUS->RFSEQ_STATUS_DETAIL);
	return radioStats;
}
// get the FSM state
uint8_t GetFSMState(void)
{
	uint32_t fsmState = READ_REG(MR_SUBG_GLOB_STATUS->RADIO_FSM_INFO);
	return (uint8_t)(fsmState & MR_SUBG_GLOB_STATUS_RADIO_FSM_INFO_RADIO_FSM_STATE_Msk);
}
/*
 * Send a text frame. Buf is not malloc'ed, can be static in ram
 */
BOOL SendTextFrame(char *srcCall, uint16_t srcPort, char *destCall, uint16_t dstPort, char *buf, uint16_t length, BOOL repeat)
{
	IP400_FRAME *txFrame;

	if((txFrame=malloc(sizeof(IP400_FRAME))) == NULL)
		return FALSE;

	if((txFrame->buf=malloc(length + MAX_CALL_BUFFER)) == NULL)
		return FALSE;

	txFrame->flagfld.allflags = 0;		// start with all flags cleared

	// format the header
	uint8_t offset = callEncode(srcCall, srcPort, txFrame, SRC_CALLSIGN, 0);
	offset = callEncode(destCall, dstPort, txFrame, DEST_CALLSIGN, offset);

	// copy the payload in
	uint8_t *f = (uint8_t *)txFrame->buf;
	f += offset*sizeof(uint32_t);
	memcpy(f, (const uint8_t *)buf, length);

	txFrame->length = length + offset;
	txFrame->flagfld.flags.hop_count = 0;
	txFrame->flagfld.flags.coding |= UTF8_TEXT_PACKET;
	txFrame->flagfld.flags.repeat = repeat;
	txFrame->seqNum = nextSeq++;

	QueueTxFrame(txFrame);

	return TRUE;
}

/*
 * compose and send a beacon frame (ping frame with broadcast destination
 */
void SendBeaconFrame(char *srcCall, uint8_t *payload, int bcnlen)
{
	IP400_FRAME *bcnFrame;

	if((bcnFrame=malloc(sizeof(IP400_FRAME))) == NULL)
		return;

	if((bcnFrame->buf=malloc(bcnlen + MAX_CALL_BUFFER)) == NULL)
		return;

	bcnFrame->flagfld.allflags = 0;		// start with all flags cleared

	// broadcast frame
	uint8_t offset = callEncode(srcCall, ICMP_TYPE, bcnFrame, SRC_CALLSIGN, 0);
	callEncode("FFFF", 0, bcnFrame, DEST_CALLSIGN, 0);

	// adjust starting data point, add payload
	uint8_t *f = (uint8_t *)bcnFrame->buf;
	f += offset*sizeof(uint32_t);
	memcpy(f, payload, bcnlen);

	bcnFrame->length = bcnlen + offset;
	bcnFrame->flagfld.flags.hop_count = 0;
	bcnFrame->flagfld.flags.coding |= PING_PACKET;
	bcnFrame->flagfld.flags.repeat = TRUE;
	bcnFrame->seqNum = nextSeq++;

	QueueTxFrame(bcnFrame);
}

// check to see if a frame came from me
// returns true if I originated the frame
BOOL FrameisMine(IP400_FRAME *frame)
{
	char decCall[30];
	uint16_t port;;

	// check the originator call sign
	callDecode(&frame->source, decCall, &port);
	return(CompareToMyCall(decCall));
}

/*
 * Repeat a frame. Do not send it if we were the originator
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
	if((rptFrame->buf=malloc(frame->length)) == NULL)
		return;
	memcpy(rptFrame->buf, frame->buf, frame->length);

	// tick the hop count
	rptFrame->flagfld.flags.hop_count += 1;

	Stats.nRepeated++;
	QueueTxFrame(rptFrame);
}

/*
 * queue a frame for transmission by the tx task
 */
void QueueTxFrame(IP400_FRAME *txframe)
{
	enqueFrame(&txQueue, txframe);
}

/*
 * Enable the receiver
 */
void EnableRx(void)
{
	__HAL_MRSUBG_SET_RX_MODE(RX_NORMAL);
	__HAL_MRSUBG_SET_DATABUFFER_SIZE(MAX_FRAME_SIZE);
    MR_SUBG_GLOB_STATIC->DATABUFFER0_PTR = (uint32_t)&rawRxFrame;

	// set the command
	radioCmd = CMD_RX;
	__HAL_MRSUBG_STROBE_CMD(radioCmd);

	SetLEDMode(BICOLOR_GREEN);
}

/*
 * main entry for tx task. Pick frames from the transmit queue
 */
void Frame_Txtask_exec(void)
{
	static IP400_FRAME *tFrame;
	int frameLen = 0;

	switch(txState)	{

	// idle: waiting for work
	case TX_IDLE:

		tFrame = dequeFrame(&txQueue);
		uint8_t *rawFrame = (uint8_t *)rawTxFrame;

		if(tFrame != NULL)	{
			frameLen = tFrame->length;

			/*
			 * Build the raw frame bytes: see IP400_FRAME struct
			 */
			// Source call + port (6 bytes)
			memcpy(rawFrame, (uint8_t *)&tFrame->source, IP_400_CALL_SIZE);
			rawFrame += IP_400_CALL_SIZE;
			// Dest call + port (6 bytes)
			memcpy(rawFrame, (uint8_t *)&tFrame->dest, IP_400_CALL_SIZE);
			rawFrame += IP_400_CALL_SIZE;
			// flag byte (2 byte)
			memcpy(rawFrame, (uint8_t *)&tFrame->flagfld, IP_400_FLAG_SIZE);
			rawFrame += IP_400_FLAG_SIZE;
			// frame sequence number (4 bytes)
			memcpy(rawFrame, (uint32_t *)&tFrame->seqNum, sizeof(uint32_t));
			rawFrame += sizeof(uint32_t);
			// frame length (2 bytes)
			memcpy(rawFrame, (uint8_t *)&tFrame->length, sizeof(uint16_t));
			rawFrame += IP_400_LEN_SIZE;

			// and now the data...
			if((tFrame->buf != NULL) && (tFrame->length != 0))
				memcpy(rawFrame, tFrame->buf, tFrame->length);
		}

		// free the allocations in the reverse order...
		if(tFrame != NULL)	{
			if(tFrame->buf != NULL)
				free(tFrame->buf);
			free(tFrame);
		} else {
			return;				// nothing to send...
		}

		int pktLen = rawFrame - rawTxFrame;
		HAL_MRSubG_PktBasicSetPayloadLength(frameLen + pktLen);

		// abort the current rx operation
		if(radioCmd == CMD_RX)	{
		    __HAL_MRSUBG_STROBE_CMD(CMD_SABORT);
		    uint32_t reject=0, abortDone=0;
		    do {
		    	subgIRQStatus = READ_REG(MR_SUBG_GLOB_STATUS->RFSEQ_IRQ_STATUS);
		    	reject = subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_COMMAND_REJECTED_F;
		    	abortDone = subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_SABORT_DONE_F;
		    }  while ((abortDone == 0) && (reject == 0));
		    if(abortDone)
		    	__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_SABORT_DONE_F);
		    if(reject)
		    	__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_COMMAND_REJECTED_F);
		}

		__HAL_MRSUBG_SET_DATABUFFER0_POINTER((uint32_t)rawTxFrame);
		__HAL_MRSUBG_SET_TX_MODE(TX_NORMAL);

		txDone = FALSE;
		prevCmd = radioCmd;
		radioCmd = CMD_TX;
		__HAL_MRSUBG_STROBE_CMD(radioCmd);

		// set tx indication: bicolor off and Tx on
		SetLEDMode(BICOLOR_OFF);
		SetLEDMode(TX_LED_ON);

		txState = TX_SENDING;
		break;

	// sending a frame
	case TX_SENDING:
		// still busy sending
		if(!txDone)
			return;

		txState = TX_DONE;
		break;

	// done
	case TX_DONE:
		// restart the receiver, if needed
		EnableRx();

		SetLEDMode(TX_LED_OFF);
		txState = TX_IDLE;
		break;
	}
}

/*
 * Main entry of the rx task
 */
void Frame_Rxtask_exec(void)
{
	// wait for completion..
	if(!rxReady)
			return;
	rxReady = FALSE;

	uint8_t *RxRaw = rawRxFrame;
	uint8_t *cpyDest;

	/*
	 * Do the opposite of the transmitter...
	 */
	// Source call + port (6 bytes)
	cpyDest = (uint8_t *)&rFrame.source.callbytes.bytes;
	memcpy(cpyDest, RxRaw, IP_400_CALL_SIZE);
	RxRaw += IP_400_CALL_SIZE;

	// Dest call + port (6 bytes)
	cpyDest = (uint8_t *)&rFrame.dest.callbytes.bytes;
	memcpy(cpyDest, RxRaw, IP_400_CALL_SIZE);
	RxRaw += IP_400_CALL_SIZE;

	// flag byte (2 byte)
	cpyDest = (uint8_t *)&rFrame.flagfld.allflags;
	memcpy(cpyDest, RxRaw, IP_400_FLAG_SIZE);
	RxRaw += IP_400_FLAG_SIZE;

	// frame sequence number (4 bytes)
	cpyDest = (uint8_t *)&rFrame.seqNum;
	memcpy(cpyDest, RxRaw, sizeof(uint32_t));
	RxRaw += sizeof(uint32_t);

	// frame length (2 bytes)
	cpyDest = (uint8_t *)&rFrame.length;
	memcpy(cpyDest, RxRaw, sizeof(uint16_t));
	RxRaw += IP_400_LEN_SIZE;

	rFrame.buf = RxRaw;

	// only interested in the 'to' port at this point...
	uint32_t length = __HAL_MRSUBG_GET_DATABUFFER_SIZE();

	// find a reason to reject a frame...
	BOOL isMine, isUniqueFrame;
	isMine = FrameisMine(&rFrame);
	if(!isMine)
		isUniqueFrame = Mesh_Accept_Frame((void *)&rFrame, Stats.lastRSSI);
	else
		isUniqueFrame = FALSE;

	// process the frame if it is not mine and unique
	if(isUniqueFrame && !isMine)		{

		switch(rFrame.source.port)	{

		// process a beacon frame
		case ICMP_TYPE:
			Mesh_ProcessBeacon((void *)&rFrame, Stats.lastRSSI);
#if __DUMP_BEACON
			EnqueChatFrame((void *)&rFrame, length);
#endif
			Stats.nBeacons++;
			break;

		case TEXT_TYPE:
			EnqueChatFrame((void *)&rFrame, length);
			Stats.framesOK++;
			break;

			// these will be supported later....
		case VIDEO_TYPE:
		case ENC_AX25_TYPE:
		case ENC_IP_TYPE:
		case AUDIO_TYPE:
		default:
			Stats.dropped++;
			logger(LOG_ERROR, "Frame Received for unknown port: %d\r\n", rFrame.source.port);
			break;
		}
	}

	// repeat the frame if the repeat flag is set and the hop count is not exhausted
	if(!isMine)	{
		if(rFrame.flagfld.flags.repeat && (rFrame.flagfld.flags.hop_count < MAX_HOP_COUNT))
			RepeatFrame(&rFrame);
	}

	// update stats
	if(!isUniqueFrame)
		Stats.duplicates++;
	if(isMine)
		Stats.dropped++;

    // restart the receiver
    EnableRx();

}
// frame interrupt callback
void HAL_MRSubG_IRQ_Callback(void)
{
	subgIRQStatus = READ_REG(MR_SUBG_GLOB_STATUS->RFSEQ_IRQ_STATUS);

	// Process transmitter interrupts
	if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_TX_DONE_F)	{
		__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_TX_DONE_F);
		Stats.TxFrameCnt++;
		txDone = TRUE;
	}

	// process receiver interrupts
    if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F) {
        Stats.CRCErrors++;
      __HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F);
    }

    if (subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F) {
        Stats.TimeOuts++;
      __HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F);
    }

    if (subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_OK_F ) {
        Stats.RxFrameCnt++;
    	__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_OK_F);
    	Stats.lastRSSI = READ_REG_FIELD(MR_SUBG_GLOB_STATUS->RX_INDICATOR, MR_SUBG_GLOB_STATUS_RX_INDICATOR_RSSI_LEVEL_ON_SYNC);
    	// shut off Rx until we are done with the frame
	    //__HAL_MRSUBG_STROBE_CMD(CMD_NOP);

    	rxReady = TRUE;
    }

    EnableRx();
}

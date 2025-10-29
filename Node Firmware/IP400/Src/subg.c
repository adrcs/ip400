/*---------------------------------------------------------------------------
	Project:	      IP400

	Module:		      Frame transmit and receive tasks

	File Name:	      subg.c

	Author:		      MartinA

	Creation Date:	  Jan 8, 2025

	Description:      Manage the STMWL33 Sub-GHz radio physical connection

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

#include <cmsis_os2.h>
#include <FreeRTOS.h>
#include <semphr.h>

#include "frame.h"
#include "dataq.h"
#include "led.h"
#include "usart.h"
#include "setup.h"

// conditionals
#define	__DUMP_BEACON	0		// dump a beacon frame to console using chat

// SubG states
typedef enum	{
		IDLE=0,			// idle - waiting for work
		RX_ACTIVE,		// rx is active
		RX_ABORTING,	// stopping Rx
		TX_READY,		// activated, no data yet
		TX_SENDING,		// sending a frame
		TX_TESTSETUP,	// test mode setup
		TX_TEST,		// test mode on
		TX_DONE			// done
} SubGRxTxState;
// character representations
char *subGStates[] = {
		"IDLE",
		"RX_ACTIVE",
		"RX_ABORTING",
		"TX_READY",
		"TX_SENDING",
		"TX_TESTSETUP",
		"TX_TEST",
		"TX_DONE"
};

// locals
uint32_t 			subgIRQStatus;	// interrupt status
int16_t  			rxSquelch;		// rx squlech
uint8_t				subGCmd;		// current command
SubGTestMode		testMode;		// transmit test mode

SubGRxTxState	 	subGState;		// radio state
FRAME_QUEUE			txQueue;		// transmitter frame queue
FRAME_STATS 		Stats;			// collected stats

// processed frames
static IP400_FRAME rFrame;
static HOPTABLE rxHopTable[MAX_HOP_COUNT];

/*
 * Buffer management
 */
typedef	uint8_t		RAWBUFFER;
// raw buffer memory
#if USE_BUFFER_RAM
static RAWBUFFER Buffer0[MAX_FRAME_SIZE] __attribute__((section("BUFFERS"), aligned(4)));
static RAWBUFFER Buffer1[MAX_FRAME_SIZE] __attribute__((section("BUFFERS"), aligned(4)));
#else
static RAWBUFFER Buffer0[MAX_FRAME_SIZE];
static RAWBUFFER Buffer1[MAX_FRAME_SIZE];
#endif
// definitions
#define	SUBG_BUFFER_0	0	// buffer 0
#define SUBG_BUFFER_1	1	// buffer 1
#define	N_SUBG_BUFFERS	2	// number of buffers
typedef enum	{
		SUBG_BUF_READY,		// ready to tx/rx
		SUBG_BUF_ACTIVE,	// active: rx or tx
		SUBG_BUF_FULL,		// full: has RX data
		SUBG_BUF_EMPTY		// empty: data sent
} BufferState;

// subg buffer state
typedef struct subg_bug_stat_t {
	BufferState	state;		// subg buffer state
	RAWBUFFER	 *addr;		// address of local buffer
	uint32_t	length;		// rx length
} SUBG_BUF_STATUS;
SUBG_BUF_STATUS subgBufState[N_SUBG_BUFFERS] = {
		{ SUBG_BUF_READY, Buffer0 },
		{ SUBG_BUF_READY, Buffer1 }
};
#define	SUBG_BUF_STATUS(c,s)	(subgBufState[c].state == s)
uint8_t	activeTxBuffer;			// active transmitter buffer

/*
 * Initialize the task
 */
void SubG_Task_init(void)
{
	subGState = IDLE;
	txQueue.q_forw = &txQueue;
	txQueue.q_back = &txQueue;

	// init stats and counters
	memset(&Stats, 0, sizeof(FRAME_STATS));

	// set comamnd to NOP
	subGCmd = CMD_NOP;

	// CW mode off
	testMode = SUBG_TEST_OFF;

	// set Rx threshold
	STN_PARAMS *params = GetStationParams();
	rxSquelch = params->radio_setup.rxSquelch;

	// enable the interrupt
	__HAL_MRSUBG_SET_RFSEQ_IRQ_ENABLE(
			MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_OK_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_TX_DONE_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_TIMEOUT_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_CRC_ERROR_E
	);
#if __CUBEIDE_19
    HAL_NVIC_EnableIRQ(MRSUBG_IRQn);
#else
    HAL_NVIC_EnableIRQ(MR_SUBG_IRQn);
#endif
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
SubGFSMState GetFSMState(void)
{
	uint32_t fsmState = READ_REG(MR_SUBG_GLOB_STATUS->RADIO_FSM_INFO);
	return (uint8_t)(fsmState & MR_SUBG_GLOB_STATUS_RADIO_FSM_INFO_RADIO_FSM_STATE_Msk);
}
// 	get the subg state
char *getSubGState(void)
{
	return subGStates[subGState];
}

/*
 * queue a frame for transmission by the tx task
 */
void QueueTxFrame(IP400_FRAME *txframe)
{
	enqueFrame(&txQueue, txframe);
}

/*
 * IP400 to buffer handlers
 * By Ed Tink, VA3EJT
 */
int IP4002Buf(IP400_FRAME *tFrame, RAWBUFFER *rawFrame)
{
 	int frameLen = tFrame->length;
	RAWBUFFER *cpyDest = rawFrame;

	// fix from Cam
	RAWBUFFER *rawFrameStart = rawFrame;	// Save start pointer for length calculation

	/*
	 * Build the raw frame bytes: see IP400_FRAME struct
	 */
	// Source call + port (6 bytes)
	memcpy(cpyDest, (uint8_t *)&tFrame->source, IP_400_CALL_SIZE);
	cpyDest += IP_400_CALL_SIZE;
	// Dest call + port (6 bytes)
	memcpy(cpyDest, (uint8_t *)&tFrame->dest, IP_400_CALL_SIZE);
	cpyDest += IP_400_CALL_SIZE;
	// flag byte (2 byte)
	memcpy(cpyDest, (uint8_t *)&tFrame->flagfld, IP_400_FLAG_SIZE);
	cpyDest += IP_400_FLAG_SIZE;
	// frame sequence number (4 bytes)
	memcpy(cpyDest, (uint32_t *)&tFrame->seqNum, sizeof(uint32_t));
	cpyDest += sizeof(uint32_t);
	// frame length (2 bytes)
	memcpy(cpyDest, (uint8_t *)&tFrame->length, sizeof(uint16_t));
	cpyDest += IP_400_LEN_SIZE;

	// add in the hop table
	if(tFrame->flagfld.flags.hoptable)	{
		uint16_t hopLen = (uint16_t)(tFrame->flagfld.flags.hop_count) * sizeof(HOPTABLE);
		memcpy(cpyDest, (uint8_t *)(tFrame->hopTable), hopLen);
		cpyDest += hopLen;
		free(tFrame->hopTable);
	}

	// and now the data...
	if((tFrame->buf != NULL) && (tFrame->length != 0)) {
		memcpy(cpyDest, tFrame->buf, tFrame->length);
		cpyDest += tFrame->length;
	}

	// free the allocations in the reverse order...
	if(tFrame->buf != NULL)
		free(tFrame->buf);

	free(tFrame);

	// Calculate total packet length: IP400 header + payload
	// rawFrame has been advanced past all header fields and payload
	int pktLen = (rawFrame - rawFrameStart) + frameLen;

	// Ensure packet length is a multiple of 4 bytes
 	pktLen += (pktLen % 4);
 	return pktLen;

}

/*
 * Do the opposite of the transmitter...
 */
void Buf2IP400(IP400_FRAME *rframe, RAWBUFFER *RxRaw)
{

	uint8_t *cpyDest;

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

	// copy the hop table
	uint8_t nHops = rFrame.flagfld.flags.hop_count;
	if(nHops != 0)		{
		uint16_t hopLen = (uint16_t)nHops*sizeof(HOPTABLE);
		memcpy(rxHopTable, RxRaw, hopLen);
		RxRaw += hopLen;
		rFrame.hopTable = rxHopTable;
	} else {
		rFrame.hopTable = NULL;
	}

	rFrame.buf = RxRaw;
}

/*
 *  diagnostic modes
 */
// set a diagnostic mode
void setSubgTestMode(SubGTestMode mode)
{
	testMode = mode;
}
// generate a PRBS sequence X7 + X6 + 1
void genPRBS(uint8_t *buffer)
{
	uint8_t val = 0x02;				// starting value
	for(int i=0;i<PRBS_FRAME_SIZE;i++)	{
		uint8_t nxt = (((val >> 6) ^ (val >> 5)) & 1);
		val = ((val<<1) | nxt) & 0x7f;
		*buffer++ = val;
	}
}

/*
 * main entry for subg task. Pick frames from the transmit queue
 */
void SubG_Task_exec(void)
{
	static RAWBUFFER *rawFrame;
	IP400_FRAME *tFrame;
	uint8_t actBuffer;
	int frameLen = 0;
	uint32_t pointer0Addr, pointer1Addr;

	SubGFSMState fsmState = GetFSMState();

	switch(subGState)	{

	// idle: enable the receiver
	case IDLE:

		// finish last abort command from Tx
		if(subGCmd == CMD_SABORT)
			subGCmd = CMD_NOP;

		// if the receiver is active, then go there...
		if((fsmState ==  FSM_RX) && (subGCmd == CMD_RX))
			subGState = RX_ACTIVE;

		// ensure we are idle when entering here...
		if((fsmState != FSM_IDLE) && (subGCmd == CMD_NOP))
			return;

		HAL_MRSubG_SetRSSIThreshold(rxSquelch);
		  __HAL_MRSUBG_SET_CS_BLANKING();

	    subgBufState[SUBG_BUFFER_0].state = SUBG_BUF_ACTIVE;
	    subgBufState[SUBG_BUFFER_0].addr = Buffer0;

	    subgBufState[SUBG_BUFFER_1].state = SUBG_BUF_ACTIVE;
	    subgBufState[SUBG_BUFFER_1].addr = Buffer1;

		__HAL_MRSUBG_SET_RX_MODE(RX_NORMAL);
		__HAL_MRSUBG_SET_DATABUFFER_SIZE(MAX_FRAME_SIZE);
		__HAL_MRSUBG_SET_DATABUFFER0_POINTER((uint32_t) subgBufState[SUBG_BUFFER_0].addr);
		__HAL_MRSUBG_SET_DATABUFFER1_POINTER((uint32_t) subgBufState[SUBG_BUFFER_1].addr);

		subGCmd = CMD_RX;
		__HAL_MRSUBG_STROBE_CMD(subGCmd);

		SetLEDMode(BICOLOR_GREEN);

		break;

	// receiver is active
	case RX_ACTIVE:
		if((SUBG_BUF_STATUS(SUBG_BUFFER_0, SUBG_BUF_FULL) || SUBG_BUF_STATUS(SUBG_BUFFER_1, SUBG_BUF_FULL)))	{
			if(SUBG_BUF_STATUS(SUBG_BUFFER_0, SUBG_BUF_FULL))
					actBuffer = SUBG_BUFFER_0;
			else	actBuffer = SUBG_BUFFER_1;
		} else {
			// no data: check the transmitter for data or diag mode
			if(quehasData(&txQueue) || testMode)	{
				subGCmd = CMD_SABORT;
				__HAL_MRSUBG_STROBE_CMD(subGCmd);
				subGState = RX_ABORTING;
			}
			return;
		}
		// process a received frame
		rawFrame = subgBufState[actBuffer].addr;
		frameLen = subgBufState[actBuffer].length;

		// process the frame fields
		Buf2IP400(&rFrame, rawFrame);
		ProcessRxFrame(&rFrame, frameLen);

	    subgBufState[actBuffer].state = SUBG_BUF_ACTIVE;
	    break;

	// shutting down receiver: ready to tx
	case RX_ABORTING:
		// finish last abort command from Tx
		if(subGCmd == CMD_SABORT)
			subGCmd = CMD_NOP;

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

		// set buffer status
		subgBufState[SUBG_BUFFER_0].state = SUBG_BUF_EMPTY;
		subgBufState[SUBG_BUFFER_1].state = SUBG_BUF_EMPTY;

		SetLEDMode(BICOLOR_OFF);

		if(fsmState == FSM_IDLE)	{
			// initiate diag mode
			if(testMode)	{
				if(testMode == SUBG_TEST_PRBS)	{
					genPRBS(Buffer0);
					genPRBS(Buffer1);
				}

				pointer0Addr = (uint32_t)&Buffer0;
				pointer1Addr = (uint32_t)&Buffer1;

				MODIFY_REG_FIELD(MR_SUBG_GLOB_DYNAMIC->PCKTLEN_CONFIG, MR_SUBG_GLOB_DYNAMIC_PCKTLEN_CONFIG_PCKTLEN, 0);
				__HAL_MRSUBG_SET_DATABUFFER0_POINTER(pointer0Addr);
				__HAL_MRSUBG_SET_DATABUFFER1_POINTER(pointer1Addr);
				__HAL_MRSUBG_SET_DATABUFFER_SIZE(PRBS_FRAME_SIZE);
				__HAL_MRSUBG_SET_TX_MODE(TX_DIRECT_BUFFERS);
				__HAL_MRSUBG_STROBE_CMD(CMD_LOCKTX);
				subGState = TX_TESTSETUP;
			}
			else
				subGState = TX_READY;
		}
		break;

	// ready to start the tx
	case TX_READY:
		// try to fill both tx buffers
		if((tFrame = dequeFrame(&txQueue)) != NULL)	{
			subgBufState[SUBG_BUFFER_0].length = IP4002Buf(tFrame, subgBufState[SUBG_BUFFER_0].addr);
			subgBufState[SUBG_BUFFER_0].state = SUBG_BUF_FULL;
		}
		if((tFrame = dequeFrame(&txQueue)) != NULL)	{
			subgBufState[SUBG_BUFFER_1].length = IP4002Buf(tFrame, subgBufState[SUBG_BUFFER_1].addr);
			subgBufState[SUBG_BUFFER_1].state = SUBG_BUF_FULL;
		}

		// Send buffer zero first
		HAL_MRSubG_SetModulation(setup_memory.params.radio_setup.xModulationSelect, 0);
		MODIFY_REG_FIELD(MR_SUBG_GLOB_DYNAMIC->PCKTLEN_CONFIG, MR_SUBG_GLOB_DYNAMIC_PCKTLEN_CONFIG_PCKTLEN, MAX_FRAME_SIZE);
		activeTxBuffer = SUBG_BUFFER_0;
		__HAL_MRSUBG_SET_DATABUFFER0_POINTER((uint32_t)subgBufState[activeTxBuffer].addr);
		__HAL_MRSUBG_SET_DATABUFFER_SIZE(MAX_FRAME_SIZE);
		__HAL_MRSUBG_SET_TX_MODE(TX_NORMAL);
		subGCmd = CMD_TX;
		__HAL_MRSUBG_STROBE_CMD(subGCmd);

		// set tx indication: bicolor off and Tx on
		SetLEDMode(TX_LED_ON);
		subGState = TX_SENDING;
		break;

	// actively transmitting
	case TX_SENDING:
		// see if a buffer is still waiting...
		if((SUBG_BUF_STATUS(SUBG_BUFFER_0, SUBG_BUF_FULL) || SUBG_BUF_STATUS(SUBG_BUFFER_1, SUBG_BUF_FULL)))	{
			activeTxBuffer = (SUBG_BUF_STATUS(SUBG_BUFFER_0, SUBG_BUF_FULL)) ?  SUBG_BUFFER_0: SUBG_BUFFER_1;
			__HAL_MRSUBG_SET_DATABUFFER0_POINTER((uint32_t)subgBufState[activeTxBuffer].addr);
			__HAL_MRSUBG_STROBE_CMD(subGCmd);
		}

		// see if we have an empty buffer to fill...
		if((SUBG_BUF_STATUS(SUBG_BUFFER_0, SUBG_BUF_EMPTY) || SUBG_BUF_STATUS(SUBG_BUFFER_1, SUBG_BUF_EMPTY)))	{
			actBuffer = (SUBG_BUF_STATUS(SUBG_BUFFER_0, SUBG_BUF_EMPTY)) ?  SUBG_BUFFER_0: SUBG_BUFFER_1;

			// is there a frame to send?
			if((tFrame = dequeFrame(&txQueue)) != NULL)	{
				subgBufState[actBuffer].state = SUBG_BUF_FULL;
				subgBufState[actBuffer].length = IP4002Buf(tFrame, subgBufState[SUBG_BUFFER_0].addr);
			} else {
				// nothing to send and both buffers empty
				if((SUBG_BUF_STATUS(SUBG_BUFFER_0, SUBG_BUF_EMPTY) && SUBG_BUF_STATUS(SUBG_BUFFER_1, SUBG_BUF_EMPTY)))
					subGState = TX_DONE;
			}
		}
		break;

	/*
	 * Tx diagnostic modes
	 */
	// setup tx on mode
	case TX_TESTSETUP:
		fsmState = GetFSMState();
		if(fsmState < FSM_LOCKONTX)
			return;

		if(testMode == SUBG_TEST_CW)
			HAL_MRSubG_SetModulation(MOD_CW, 0);

		// wait until ready to transmit..
		if(fsmState == FSM_LOCKONTX)	{
			subGCmd = CMD_TX;
			__HAL_MRSUBG_STROBE_CMD(subGCmd);
			SetLEDMode(TX_LED_ON);
		} else return;						// not in correct state yet

		subGState = TX_TEST;
		break;

	// wait state until turned off
	case TX_TEST:
		if(!testMode)
			subGState = TX_DONE;
		break;

	// all transmit mode exit
	case TX_DONE:
		subGCmd = CMD_SABORT;
		__HAL_MRSUBG_STROBE_CMD(subGCmd);
		SetLEDMode(TX_LED_OFF);
		subGState = IDLE;
		break;

	}
}

// subg interrupt callback
void HAL_MRSubG_IRQ_Callback(void)
{
	subgIRQStatus = READ_REG(MR_SUBG_GLOB_STATUS->RFSEQ_IRQ_STATUS);

	// check for an error: leave buffer in current state for re-use
	if(subgIRQStatus &	(
			MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F |
			MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F))
	{
		if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F)	{
			__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F);
			Stats.CRCErrors++;
		}
		if (subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F) {
			__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F);
			Stats.TimeOuts++;
		}

		// turn the Rx back on if still active
		if(subGCmd == CMD_RX)
			__HAL_MRSUBG_STROBE_CMD(subGCmd);
		return;
	}

	// Good Rx: mark the buffer full
    if (subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_OK_F ) {
        Stats.RxFrameCnt++;
    	Stats.lastRSSI = READ_REG_FIELD(MR_SUBG_GLOB_STATUS->RX_INDICATOR, MR_SUBG_GLOB_STATUS_RX_INDICATOR_RSSI_LEVEL_ON_SYNC);
    	__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_OK_F);
		if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_DATABUFFER0_USED_F )	{
			subgBufState[SUBG_BUFFER_0].state = SUBG_BUF_FULL;
			subgBufState[SUBG_BUFFER_0].length = __HAL_MRSUBG_GET_DATABUFFER_SIZE();
		}
		if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_DATABUFFER1_USED_F )	{
			subgBufState[SUBG_BUFFER_1].state = SUBG_BUF_FULL;
			subgBufState[SUBG_BUFFER_1].length = __HAL_MRSUBG_GET_DATABUFFER_SIZE();
		}
		// turn the Rx back on if still active
		if(subGCmd == CMD_RX)
			__HAL_MRSUBG_STROBE_CMD(subGCmd);
    }

    // TxDone: cannot do tx and rx at the same time
    else if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_TX_DONE_F)	{
    	__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_TX_DONE_F);
    	if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_DATABUFFER0_USED_F )	{
    		subgBufState[activeTxBuffer].state = SUBG_BUF_EMPTY;

    	}
    	// this will probably never happen, but we will check it anyway...
    	if(subgIRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_DATABUFFER1_USED_F )	{
    		subgBufState[activeTxBuffer].state = SUBG_BUF_EMPTY;
    	}
		Stats.TxFrameCnt++;
    }
}

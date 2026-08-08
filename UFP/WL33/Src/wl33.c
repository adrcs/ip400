/*---------------------------------------------------------------------------
	Project:	      IP400

	Module:		      Frame transmit and receive tasks

	File Name:	      wl33.c

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

#ifdef __NUCLEOCC2
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
#include "wl33.h"
#include "xcvr.h"
#include "memory.h"
#include "bfrmgr.h"
#include "wl33.h"

// Transceiver States
typedef enum	{
		IDLE=0,			// idle - waiting for work
		RX_ACTIVE,		// rx is active
		RX_ABORTING,	// stopping Rx
		TX_READY,		// activated, no data yet
		TX_SENDING,		// sending a frame
		TX_TESTSETUP,	// test mode setup
		TX_TEST,		// test mode on
		TX_DONE			// done
} wl33RxTxState;

// character representations
char *wl33CodeStates[] = {
		"IDLE",
		"RX_ACTIVE",
		"RX_ABORTING",
		"TX_READY",
		"TX_SENDING",
		"TX_TESTSETUP",
		"TX_TEST",
		"TX_DONE"
};

/*
 * Print the FSM state
 */
char *wl33_states[FSM_N_FSM_STATES] = {
		"Idle",
		"Enable RF registers",
		"Wait for active 2",
		"Active 2",
		"Enable current",
		"Synth setup",
		"VCO calibration",
		"Lock Rx and Rx",
		"Lock on Rx",
		"Enable PA",
		"Transmit",
		"Analog power down",
		"End transmit",
		"Lock on Rx",
		"Enable Rx",
		"Enable LNA",
		"Receive",
		"End rx",
		"Synth power down"
};
// locals
uint32_t 			wl33IRQStatus;	// interrupt status
int16_t  			rxSquelch;		// rx squlech
MRSubGCmd			wl33Cmd;		// current command
XcvrTestMode		testMode;		// transmit test mode
wl33RxTxState	 	wl33State;		// radio state
RADIO_STATS 		wl33Stats;		// collected stats
BOOL				TxDone;			// tx is done

// transmit queue
FRAME_QUEUE	wl33_TxQueue;			// transmitter frame queue

/*
 * Abstraction interface
 * These entries need to be defined here...
 		void	(*Process)(void);				// processing
		void *	(*GetSetup)(void);				// get the radio setup
		void 	(*ApplySetup)(void);			// apply setup
		void	(*QueTxFrame)(void *);			// queue a transmit frame
		void	(*SetTestMode)(uint8_t);		// set a test mode
		void *	(*GetStats)(void);				// get stats
 */
//
void wl33_Init(void);
void wl33_Process(void);
void *wl33_GetSetup(void);
void wl33_RadioSetup(void *);
void wl33_QTxFrame(void *txframe);
void wl33_TestMode(uint8_t  mode);
void *Getwl33Stats(void);

// initialize the callbacks in the xcvr struct
XCVR_ABS wl33_vectors = {
		.Init = &wl33_Init,
		.Process = &wl33_Process,
		.GetSetup = &wl33_GetSetup,
		.ApplySetup = &wl33_RadioSetup,
		.QueTxFrame = &wl33_QTxFrame,
		.SetTestMode = &wl33_TestMode,
		.GetStats = &Getwl33Stats,
		.QueRxFrame = &QueueRxFrameCallback
};
// required to initialize structs
XCVR_ABS *wl33_GetVectors(void)
{
	return &wl33_vectors;
}

// internals
void genPRBS(uint8_t *buffer);

/*
 * Radio Setup params: relocated from Setup.c
 */
// default parameters
RADIO_SETUP WL33_setup = {
		.lFrequencyBase = 445750000,
		.xModulationSelect = MOD_4FSK,
		.lDatarate = 100000,
		.lFreqDev = 25000,
		.lBandwidth = 200000,
		.dsssExp = 0,
		.outputPower = MAX_OUPUT_POWER,
		.PADrvMode = PA_DRIVE_MODE,
		.rxSquelch = -95,
};

void *wl33_GetSetup(void)
{
	return (void *)&WL33_setup;
}

void wl33_RadioSetup(void *setup)
{
	SMRSubGConfig_t MRSUBG_RadioInitStruct;
	MRSubG_802_15_4_PcktFields_t MRSUBG_PacketSettingsStruct;

	RADIO_SETUP *setupData = (RADIO_SETUP *)setup;

	MRSUBG_RadioInitStruct.lFrequencyBase = setupData->lFrequencyBase;
	MRSUBG_RadioInitStruct.xModulationSelect = setupData->xModulationSelect;
	MRSUBG_RadioInitStruct.lDatarate = setupData->lDatarate;
	MRSUBG_RadioInitStruct.lFreqDev = setupData->lFreqDev;
	MRSUBG_RadioInitStruct.lBandwidth = setupData->lBandwidth;
	MRSUBG_RadioInitStruct.dsssExp = setupData->dsssExp;
	MRSUBG_RadioInitStruct.outputPower = setupData->outputPower;
	MRSUBG_RadioInitStruct.PADrvMode = setupData->PADrvMode;
	HAL_MRSubG_Init(&MRSUBG_RadioInitStruct);

	/*
	 * Configures the packet parameters: these are fixed
	 */
	MRSUBG_PacketSettingsStruct.Modulation = MOD_4FSK;
	MRSUBG_PacketSettingsStruct.PreambleLength = 64;
	MRSUBG_PacketSettingsStruct.FCSType = FCS_32BIT;
	MRSUBG_PacketSettingsStruct.Whitening = DISABLE;
	MRSUBG_PacketSettingsStruct.FecType = FEC_15_4_G_RSC;
	MRSUBG_PacketSettingsStruct.FrameLength = BFR_SIZE;
	HAL_MRSubG_802_15_4_PacketInit(&MRSUBG_PacketSettingsStruct);
}


/*
 * Initialize the task
 * Address of abstraction struct is passed here..
 */
void wl33_Init(void)
{

	wl33State = IDLE;

	// init queues
	wl33_TxQueue.q_forw = &wl33_TxQueue;
	wl33_TxQueue.q_back = &wl33_TxQueue;

	// setup the radio
	wl33_RadioSetup(wl33_GetSetup());

	// init stats and counters
	memset(&wl33Stats, 0, sizeof(RADIO_STATS));

	// set comamnd to NOP
	wl33Cmd = CMD_NOP;

	// CW mode off
	testMode = XCVR_TEST_OFF;

	// set Rx threshold
	rxSquelch = WL33_setup.rxSquelch;

	/*
	 * Buffer init
	 */
	BufferTask_init();
	BUFFER_STATUS *wl33_bufferStatus = getBufferStatus();
	wl33Stats.bfrStatus = wl33_bufferStatus;

	// enable the interrupt
	__HAL_MRSUBG_SET_RFSEQ_IRQ_ENABLE(
			MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_OK_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_TX_DONE_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_TIMEOUT_E
		|	MR_SUBG_GLOB_DYNAMIC_RFSEQ_IRQ_ENABLE_RX_CRC_ERROR_E
	);
    HAL_NVIC_EnableIRQ(MRSUBG_IRQn);
}


/*
 * queue a frame for transmission by the tx task
 */
void wl33_QTxFrame(void *txframe)
{
	IP400_FRAME *fr = (IP400_FRAME *)txframe;
	uint16_t frLen = (uint16_t)fr->flagfld.flags.payloadMSB;
	frLen = (frLen <<8) + fr->length;
	enqueFrame(&wl33_TxQueue, fr, frLen);
}

/*
 *  diagnostic modes
 */
// set a diagnostic mode
void wl33_TestMode(uint8_t  mode)
{
	testMode = (XcvrTestMode)mode;
}

// get the FSM state
uint8_t wl33GetFSMState(void)
{
	uint32_t fsmState = READ_REG(MR_SUBG_GLOB_STATUS->RADIO_FSM_INFO);
	return (uint8_t)(fsmState & MR_SUBG_GLOB_STATUS_RADIO_FSM_INFO_RADIO_FSM_STATE_Msk);
}

/*
 * return the stats
 */
void *Getwl33Stats(void)
{
	wl33Stats.radioFSM = wl33GetFSMState();
	wl33Stats.fsmState = wl33_states[wl33Stats.radioFSM];
	wl33Stats.codeState = wl33CodeStates[wl33State];
	return (void *)&wl33Stats;
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
 * dump the header of an invalid frame
 */
void DumpHdr(uint8_t *buf)
{
	USART_Print_string("Out of memory for Frame: ");

	for(int i=0;i<2*IP_400_CALL_SIZE+IP_400_FLAG_SIZE+sizeof(uint32_t)+IP_400_LEN_SIZE;i++)
		USART_Print_string("%x ", (int)buf[i]);

	USART_Print_string("\r\n");
}

/*
 * main entry for wl33 task. Pick frames from the transmit queue
 */
void wl33_Process(void)
{
	static uint8_t *rawFrame;
	wl33FSMState fsmState = wl33GetFSMState();

	// run the buffer task first
	BufferTask_Exec();

	switch(wl33State)	{

	// idle: enable the receiver
	case IDLE:

		// finish last abort command from Tx
		if(wl33Cmd == CMD_SABORT)
			wl33Cmd = CMD_NOP;

		// if the receiver is active, then go there...
		if((fsmState ==  FSM_RX) && (wl33Cmd == CMD_RX))	{
			wl33State = RX_ACTIVE;
			break;
		}

		// ensure we are idle when entering here...
		if((fsmState != FSM_IDLE) && (wl33Cmd == CMD_NOP))
			return;

		HAL_MRSubG_SetRSSIThreshold(rxSquelch);
		  __HAL_MRSUBG_SET_CS_BLANKING();


		__HAL_MRSUBG_SET_RX_MODE(RX_NORMAL);
		__HAL_MRSUBG_SET_DATABUFFER_SIZE(BFR_SIZE);
		__HAL_MRSUBG_SET_DATABUFFER0_POINTER((uint32_t) GetRxBufferAddr());

		wl33Cmd = CMD_RX;
		__HAL_MRSUBG_STROBE_CMD(wl33Cmd);

		SetLEDMode(BICOLOR_GREEN);

		break;

	// receiver is active
	case RX_ACTIVE:
		// process a received raw frame
		while(RxHasData())	{
			rawFrame = getRxBufferFrame();

			// process the rx frame into an allocated structure
			IP400_FRAME *rFrame = Buf2IP400(rawFrame);
			if(rFrame != NULL)		{
				wl33Stats.dequeued++;
				(*wl33_vectors.QueRxFrame)(rFrame);
			} else {
				DumpHdr(rawFrame);
				wl33Stats.unprocessed++;
			}
			nodeMemFree(BUFFERS, rawFrame);
		}

		// see if we can buffer anything...
		while(quehasData(&wl33_TxQueue))	{
			int nextLen = getQlength(&wl33_TxQueue);
			if(TxHasRoom(nextLen))		{
				IP400_FRAME *f = dequeFrame(&wl33_TxQueue);
				PutTxBuffer(f);
			} else break;
		}

		// see if the tx wants to start up...
		if(IsTxReady() || (testMode != XCVR_TEST_OFF))	{
			wl33Cmd = CMD_SABORT;
			__HAL_MRSUBG_STROBE_CMD(wl33Cmd);
			wl33State = RX_ABORTING;
			__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_OK_F);
		}
		break;

	// shutting down receiver: ready to tx
	case RX_ABORTING:
		// finish last abort command from Tx
		if(wl33Cmd == CMD_SABORT)
			wl33Cmd = CMD_NOP;

		uint32_t reject=0, abortDone=0;
		do {
			wl33IRQStatus = READ_REG(MR_SUBG_GLOB_STATUS->RFSEQ_IRQ_STATUS);
			reject = wl33IRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_COMMAND_REJECTED_F;
			abortDone = wl33IRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_SABORT_DONE_F;
		}  while ((abortDone == 0) && (reject == 0));
		if(abortDone)
			__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_SABORT_DONE_F);
		if(reject)
			__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_COMMAND_REJECTED_F);

		SetLEDMode(BICOLOR_OFF);

		if(fsmState == FSM_IDLE)	{
			// initiate diag mode
			if(testMode)
				wl33State = TX_TESTSETUP;
			else
				wl33State = TX_READY;
		}
		break;

	// ready to start the tx
	case TX_READY:
		// Send buffer zero first
		uint16_t bfrLen = GetTxBufferLength();
		bfrLen = (bfrLen < MIN_ON_AIR_SIZE) ? MIN_ON_AIR_SIZE: bfrLen;
		uint32_t brfAddr = (uint32_t)GetTxBufferAddr();
		HAL_MRSubG_SetModulation(WL33_setup.xModulationSelect, 0);
		MODIFY_REG_FIELD(MR_SUBG_GLOB_DYNAMIC->PCKTLEN_CONFIG, MR_SUBG_GLOB_DYNAMIC_PCKTLEN_CONFIG_PCKTLEN, bfrLen);
		__HAL_MRSUBG_SET_DATABUFFER0_POINTER(brfAddr);
		__HAL_MRSUBG_SET_DATABUFFER_SIZE(bfrLen);
		__HAL_MRSUBG_SET_TX_MODE(TX_NORMAL);

		TxDone = FALSE;
		wl33Cmd = CMD_TX;
		__HAL_MRSUBG_STROBE_CMD(wl33Cmd);

		// set tx indication: bicolor off and Tx on
		SetLEDMode(TX_LED_ON);
		wl33State = TX_SENDING;
		break;

	// actively transmitting:
	case TX_SENDING:
		if(TxDone)
			wl33State = TX_DONE;
		SetTxBufferDone();
		break;

	/*
	 * Tx diagnostic modes
	 */
	// setup tx on mode
	case TX_TESTSETUP:

		genPRBS(GetTxBufferAddr());
		genPRBS(GetTxBufferAltAddr());

		MODIFY_REG_FIELD(MR_SUBG_GLOB_DYNAMIC->PCKTLEN_CONFIG, MR_SUBG_GLOB_DYNAMIC_PCKTLEN_CONFIG_PCKTLEN, 0);
		__HAL_MRSUBG_SET_DATABUFFER0_POINTER((uint32_t)GetTxBufferAddr());
		__HAL_MRSUBG_SET_DATABUFFER1_POINTER((uint32_t)GetTxBufferAltAddr());
		__HAL_MRSUBG_SET_DATABUFFER_SIZE(PRBS_FRAME_SIZE);
		__HAL_MRSUBG_SET_TX_MODE(TX_DIRECT_BUFFERS);
		__HAL_MRSUBG_STROBE_CMD(CMD_LOCKTX);

		do {
			fsmState = wl33GetFSMState();
		} while (fsmState < FSM_LOCKONTX);

		if(testMode == XCVR_TEST_CW)
			HAL_MRSubG_SetModulation(MOD_CW, 0);

		// start tx
		wl33Cmd = CMD_TX;
		__HAL_MRSUBG_STROBE_CMD(wl33Cmd);
		SetLEDMode(TX_LED_ON);

		wl33State = TX_TEST;
		break;

	// wait state until turned off
	case TX_TEST:
		if(!testMode)
			wl33State = TX_DONE;
		break;

	// all transmit mode exit
	case TX_DONE:
		wl33Cmd = CMD_SABORT;
		__HAL_MRSUBG_STROBE_CMD(wl33Cmd);
		SetLEDMode(TX_LED_OFF);
		wl33State = IDLE;
		break;

	}
}

/*
 * the schmegheads at ST changed the case in SubG for this release...
 * Ergo My interrupts did not work...Thanks for that.
 */
void HAL_MRSubG_IRQ_Callback(void)
{
	wl33IRQStatus = READ_REG(MR_SUBG_GLOB_STATUS->RFSEQ_IRQ_STATUS);

	// check for an error: leave buffer in current state for re-use
	if(wl33IRQStatus &	(
			MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F |
			MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F))
	{
		if(wl33IRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F)	{
			__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_CRC_ERROR_F);
			wl33Stats.CRCErrors++;
		}
		if (wl33IRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F) {
			__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_TIMEOUT_F);
			wl33Stats.TimeOuts++;
		}

		// turn the Rx back on if still active
		if(wl33Cmd == CMD_RX)
			__HAL_MRSUBG_STROBE_CMD(wl33Cmd);
		return;
	}

	// Good Rx
    if (wl33IRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_OK_F ) {
    	wl33Stats.lastRSSI = READ_REG_FIELD(MR_SUBG_GLOB_STATUS->RX_INDICATOR, MR_SUBG_GLOB_STATUS_RX_INDICATOR_RSSI_LEVEL_ON_SYNC);
    	__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_RX_OK_F);
		SetRxDone();
		wl33Stats.RxFrameCnt++;
		wl33Cmd = CMD_RX;
		__HAL_MRSUBG_STROBE_CMD(wl33Cmd);
    }

    // TxDone: cannot do tx and rx at the same time
    else if(wl33IRQStatus & MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_TX_DONE_F)	{
    	__HAL_MRSUBG_CLEAR_RFSEQ_IRQ_FLAG(MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_TX_DONE_F);
    	if(wl33IRQStatus & (MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_DATABUFFER0_USED_F ||  MR_SUBG_GLOB_STATUS_RFSEQ_IRQ_STATUS_DATABUFFER1_USED_F ))	{
    		TxDone = TRUE;
    	}
    	wl33Stats.TxFrameCnt++;

	}
}

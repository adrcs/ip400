/*---------------------------------------------------------------------------
	Project:	    NucleoCC2

	File Name:	    spi.c

	Author:		    Martin, VE6VH

	Description:	SPI task. responds to commands from the host, implementing a quasi-
					memory device

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
#include <FreeRTOS.h>
#include <semphr.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include <config.h>
#include "types.h"
#include "main.h"
#include "spi.h"
#include "dataq.h"
#include "tasks.h"

#define	SPI_MAX_TIME	200								// 200 ms max no activity timeout
#define	NO_SPI_TIMEOUT	(SPI_MAX_TIME/SPI_TASK_SCHED)	// timeout in schedule ticks

// transmitter states
enum {
	SPITXIDLE=0,				// idle
	SPITXFRAG					// next fragment
};

// receiver states
enum {
	SPIRXIDLE=0,				// idle
	SPIRXFRAG					// next fragment
};

uint8_t	SPITxState;				// transmitter state
uint8_t	SPIRxState;				// receiver state

// frame queue
FRAME_QUEUE spiTxQueue;			// queue for outbound

// frame buffers
#if USE_BUFFER_RAM
static SPI_BUFFER spiTxBuffer __attribute__((section("BUFFERS"), aligned(4)));
static SPI_BUFFER spiRxBuffer __attribute__((section("BUFFERS"), aligned(4)));
static uint8_t rxFrameBuffer[PAYLOAD_MAX] __attribute__((section("BUFFERS"), aligned(4)));
#else
static SPI_BUFFER spiTxBuffer;
static SPI_BUFFER spiRxBuffer;
static uint8_t rxFrameBuffer[PAYLOAD_MAX];
#endif

uint8_t					SPI_State;						// current state
HAL_StatusTypeDef 		spiXfer;		// last transfer status
BOOL 		spiExchangeComplete;		// spi exchange has been completed
BOOL 		spiErrorOccurred;
BOOL 		spiActive;
uint16_t	spiActivityTimer;			// no activity timer

// select the correct SPI
#if _BOARD_TYPE == NUCLEO_BOARD
#define	hSPI			hspi1
extern SPI_HandleTypeDef hspi1;			// spi handle
#else
	#if __INCLUDE_SPI
	#define	hSPI			hspi3
	extern SPI_HandleTypeDef hspi3;			// spi handle
	#else
	#define	hSPI			NULL			// not implemented
#endif

#endif


// validate an inbound frame
BOOL isIP400Frame(uint8_t *eye);

/*
 * place a frame on the queue frame content is copied
 * to alloc'd memory
 */
BOOL EnqueSPIFrame(void *ip400frame)
{
	IP400_FRAME *qFrame, *SrcFrame = (IP400_FRAME *)ip400frame;
	uint8_t *frameBuffer;

	// spi is not running...
	if(!spiActive)
		return FALSE;

	// allocate an IP400 frame
	if((qFrame=malloc(sizeof(IP400_FRAME)))== NULL)
		return FALSE;
	memcpy(qFrame, SrcFrame, sizeof(IP400_FRAME));

	int16_t hopCount = qFrame->flagfld.flags.hop_count;
	int16_t hTblSize = hopCount * sizeof(HOPTABLE);

	// hop table is in the first part of the payload
	// alloc enough for both
	if((frameBuffer=malloc(SrcFrame->length+hTblSize)) == NULL)	{
		free(qFrame);
		return FALSE;
	}

	if(hopCount)	{
		memcpy(frameBuffer, qFrame->hopTable, hTblSize);
		frameBuffer += hTblSize;
	}

	memcpy(frameBuffer, (uint8_t *)SrcFrame->buf, SrcFrame->length);
	qFrame->buf = frameBuffer;
	qFrame->length = SrcFrame->length + hTblSize;

	if(!enqueFrame(&spiTxQueue, qFrame))
		return FALSE;

	return TRUE;
}

/*
 * clean up any queued frames
 */
void EmptySPIFrameQ(void)
{
	while(dequeFrame(&spiTxQueue) != NULL);
	return;
}

/*
 * Initialize the task
 */
void SPI_Task_init(void)
{
	// create the rx completed semaphore
	spiExchangeComplete = FALSE;
	spiErrorOccurred = FALSE;

	spiTxBuffer.spiData.hdr.eye[0] = 'I';
	spiTxBuffer.spiData.hdr.eye[1] = 'P';
	spiTxBuffer.spiData.hdr.eye[2] = '4';
	spiTxBuffer.spiData.hdr.eye[3] = 'C';

	spiTxBuffer.spiData.hdr.status = NO_DATA;
	spiRxBuffer.spiData.hdr.status = NO_DATA;

	SPITxState = SPITXIDLE;
	SPIRxState = SPIRXIDLE;

	// tx (outbound) frame queue
	spiTxQueue.q_forw = &spiTxQueue;
	spiTxQueue.q_back = &spiTxQueue;

	spiActive = FALSE;					// no activity yet
	spiActivityTimer = 0;

#if __INCLUDE_SPI
	// start the ball rolling..
	if((spiXfer = HAL_SPI_TransmitReceive_DMA(&hSPI, spiTxBuffer.rawData, spiRxBuffer.rawData, SPI_RAW_LEN)) != HAL_OK)
		spiErrorOccurred = TRUE;
#endif

}

// execute the task
void SPI_Task_Exec(void)
{
	static IP400_FRAME *txFrame;
	static uint16_t txSegLength, txPayloadRoom, hopSize;
	static uint8_t *prxData, *ptxData, nHops;
	static uint16_t rxSegLen;

#if __INCLUDE_SPI
	// check the status first: repost Rx if an error occurred and it is now ready
	if(spiErrorOccurred)	{
		if(hSPI.State == HAL_SPI_STATE_READY)	{
			if((spiXfer = HAL_SPI_TransmitReceive_DMA(&hSPI, spiTxBuffer.rawData, spiRxBuffer.rawData, SPI_RAW_LEN)) == HAL_OK)	{
				spiErrorOccurred = FALSE;
			}
		}
	}
#endif

	/*
	 * Here we wait for an exchange to be completed
	 * If there is no activity for NO_SPI_TIMEOUT, then
	 * the other end is probably dead, so clean up
	 * any pending frames. If SPI is not enabled,
	 * the queue will also be cleaned up
	 */
	if(!spiExchangeComplete)		{
		spiActivityTimer += 1;
		if(spiActivityTimer >= NO_SPI_TIMEOUT)	{
			EmptySPIFrameQ();
			spiActive = FALSE;
			spiActivityTimer = 0;
		}
		return;
	}

	spiExchangeComplete = FALSE;		// reset exchange done
	spiActive = TRUE;					// indicate that the SPI is active..
	spiActivityTimer = 0;				// reset no activity timer

// scope trigger on nucleo board
#if	_BOARD_TYPE==NUCLEO_BOARD	// board type in use
    HAL_GPIO_TogglePin(SCOPE_GPIO_Port, SCOPE_Pin);
#endif

	/*
	 * process an outbound frame
	 * Fragment it if longer that 500 bytes
	 */
	switch(SPITxState)	{

	case SPITXIDLE:
		if((txFrame=dequeFrame(&spiTxQueue)) == NULL)	{
			spiTxBuffer.spiData.hdr.status = NO_DATA;
			break;
		}
		txSegLength = txFrame->length;
		nHops = txFrame->flagfld.flags.hop_count;
		hopSize = (uint16_t)nHops * sizeof(HOPTABLE);
		txPayloadRoom = SPI_BUFFER_LEN - hopSize;
		spiTxBuffer.spiData.hdr.status = SINGLE_FRAME;

		// fragment the frame if needed
		if(txFrame->length > txPayloadRoom)	{
			txSegLength = txPayloadRoom;
			spiTxBuffer.spiData.hdr.status = FRAGMENT;
			SPITxState = SPITXFRAG;
		}

		// copy the address fields
		memcpy(&spiTxBuffer.spiData.hdr.fromCall, txFrame->source.callbytes.bytes, N_CALL);
		memcpy(&spiTxBuffer.spiData.hdr.fromIP, txFrame->source.vpnBytes.vpn, N_IPBYTES);

		memcpy(spiTxBuffer.spiData.hdr.toCall, txFrame->dest.callbytes.bytes, N_CALL);
		memcpy(&spiTxBuffer.spiData.hdr.toIP, txFrame->dest.vpnBytes.vpn, N_IPBYTES);

		// flag fields
		spiTxBuffer.spiData.hdr.coding = txFrame->flagfld.flags.coding;
		spiTxBuffer.spiData.hdr.hopCount = txFrame->flagfld.flags.hop_count;
		spiTxBuffer.spiData.hdr.flags = (uint8_t)((txFrame->flagfld.allflags >> 8) & 0xFF);

		ptxData = spiTxBuffer.spiData.buffer;

		// hop table
		if(txFrame->flagfld.flags.hoptable)	{
			memcpy(ptxData, txFrame->hopTable, hopSize);
			ptxData += hopSize;
			free(txFrame->hopTable);
		}

		// and now the data
		memcpy(ptxData, txFrame->buf, txSegLength);
		spiTxBuffer.spiData.hdr.offset_hi = spiTxBuffer.spiData.hdr.offset_lo = 0;
		spiTxBuffer.spiData.hdr.length_hi = ((txSegLength + hopSize) >> 8);
		spiTxBuffer.spiData.hdr.length_lo = ((txSegLength + hopSize) & 0xFF);

		// release memory if only a single frame
		if(spiTxBuffer.spiData.hdr.status == SINGLE_FRAME)
		{
			free(txFrame->buf);
			free(txFrame);
		}
		break;

	case SPITXFRAG:
		uint16_t offset = ((uint16_t)(spiTxBuffer.spiData.hdr.offset_hi) << 8) + (uint16_t)spiTxBuffer.spiData.hdr.offset_lo;
		uint16_t prevLen = ((uint16_t)(spiTxBuffer.spiData.hdr.length_hi) << 8) + (uint16_t)spiTxBuffer.spiData.hdr.length_lo;
		offset += prevLen;
		txFrame->length -= prevLen;
		txSegLength = txFrame->length;
		if(txFrame->length > SPI_BUFFER_LEN)	{
			spiTxBuffer.spiData.hdr.status = FRAGMENT;
			txSegLength = SPI_BUFFER_LEN;
		} else {
			spiTxBuffer.spiData.hdr.status = LAST_FRAGMENT;
			SPITxState = SPITXIDLE;
		}

		// send the next fragment
		void *fragAddr = txFrame->buf + offset;
		memcpy(spiTxBuffer.spiData.buffer, fragAddr, txSegLength);
		spiTxBuffer.spiData.hdr.offset_hi = (offset >> 8);
		spiTxBuffer.spiData.hdr.offset_lo = (offset & 0xff);
		spiTxBuffer.spiData.hdr.length_hi = (txSegLength >> 8);
		spiTxBuffer.spiData.hdr.length_lo = (txSegLength & 0xFF);

		// done with frame
		if(spiTxBuffer.spiData.hdr.status == LAST_FRAGMENT)
		{
			free(txFrame->buf);
			free(txFrame);
		}
		break;
	}

	/*
	 * Inbound frame. Reassemble fragments if needed...
	 */

	switch(SPIRxState)	{

	case SPIRXIDLE:
		spiFrameStatus rstat = spiRxBuffer.spiData.hdr.status;
		if((rstat == NO_DATA) || (rstat >= NUM_STATS))
			break;

		if(!isIP400Frame(spiRxBuffer.spiData.hdr.eye))
			break;

		rxSegLen = ((uint16_t)spiRxBuffer.spiData.hdr.length_hi << 8) + (uint16_t)spiRxBuffer.spiData.hdr.length_lo;
		if(rstat != SINGLE_FRAME)	{
			prxData =  rxFrameBuffer + ((uint16_t)spiRxBuffer.spiData.hdr.offset_hi << 8) + (uint16_t)spiRxBuffer.spiData.hdr.offset_lo;
			memcpy(prxData, spiRxBuffer.spiData.buffer, rxSegLen);
			SPIRxState = SPIRXFRAG;
		} else {
			SendSPIFrame(&spiRxBuffer.spiData.hdr, spiRxBuffer.spiData.buffer, rxSegLen);
		}
		break;

	case SPIRXFRAG:
		uint8_t fragStat = spiRxBuffer.spiData.hdr.status;
		uint16_t offset = (spiTxBuffer.spiData.hdr.offset_hi << 8) + spiTxBuffer.spiData.hdr.offset_lo;
		prxData = rxFrameBuffer + offset;
		rxSegLen = ((uint16_t)spiRxBuffer.spiData.hdr.length_hi << 8) + (uint16_t) spiRxBuffer.spiData.hdr.length_lo;
		memcpy(prxData, spiRxBuffer.spiData.buffer, rxSegLen);

		if(fragStat == LAST_FRAGMENT)	{
			uint16_t frameLen = offset + rxSegLen;
			spiRxBuffer.spiData.hdr.length_hi = (frameLen >> 8);
			spiRxBuffer.spiData.hdr.length_lo = (frameLen & 0xFF);
			// process frame for tx here...
			SPIRxState = SPIRXIDLE;					// placeholder
		}
		break;
	}

}


// test an incoming frame
BOOL isIP400Frame(uint8_t *eye)
{
	if((eye[0] != 'I') || (eye[1] != 'P'))
		return FALSE;

	if((eye[2] != '4') || (eye[3] != 'C'))
		return FALSE;

	return TRUE;
}

#if __INCLUDE_SPI
// rx done callback
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef * hspi)
{
	// set data valid and start a new transfer
	if(hspi->State == HAL_SPI_STATE_READY)		{
		spiExchangeComplete = TRUE;
		spiXfer = HAL_SPI_TransmitReceive_DMA(&hSPI, spiTxBuffer.rawData, spiRxBuffer.rawData, SPI_RAW_LEN);
		if(spiXfer != HAL_OK)
			spiErrorOccurred = TRUE;
		return;
	}
	spiErrorOccurred = TRUE;			// spi not ready
}
#endif

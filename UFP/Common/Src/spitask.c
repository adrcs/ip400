/*---------------------------------------------------------------------------
	Project:	    NucleoCC2

	File Name:	    spitask.c

	Author:		    Martin, VE6VH

	Description:	SPI task. Exchanges data with the host, and calls the state machine
					to process the data.

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
#include <queue.h>

#include "FreeRTOS.h"

#include "config.h"
#include "types.h"
#include "main.h"
#include "spi.h"
#include "dataq.h"
#include "frame.h"
#include "memory.h"
#include "usart.h"

#include "led.h"
#include "tasks.h"

#if defined(__NUCLEOCC2) || defined(__PI_BOARD)
#include "xcvr.h"
#include "bfrmgr.h"
#endif

#define	SPI_MAX_TIME	200								// 200 ms max no activity timeout
#define	NO_SPI_TIMEOUT	(SPI_MAX_TIME/SPI_TASK_SCHED)	// timeout in schedule ticks

// outbound frame queue
FRAME_QUEUE spiTxQueue;			// queue for outbound
static SPI_BUFFER spiTxBuffer;

// inbound frame queue
typedef struct rx_queue_elem_t {
	void	*buffer;			// pointer to rx buffer
} RX_QUEUE_ELEM;

FRAME_QUEUE spiRxQueue;
SPI_BUFFER *spiRawFrame;

uint8_t					SPI_State;						// current state
HAL_StatusTypeDef 		spiXfer;		// last transfer status
BOOL 		spiExchangeComplete;		// spi exchange has been completed
BOOL 		spiErrorOccurred;
BOOL 		spiActive;
uint16_t	spiActivityTimer;			// no activity timer
uint16_t	fragOffset;					// fragment offset

SPI_STATS spi_stats;					// spi stats


// validate an inbound frame
BOOL isIP400Frame(uint8_t *eye);

/*
 * place a frame on the queue frame
 * frame is already in heap memory
 */
BOOL EnqueSPIFrame(void *ip400frame)
{
	IP400_FRAME *SrcFrame = (IP400_FRAME *)ip400frame;

	// spi is not running...
	if(!spiActive)		{
		DeleteFrame(ip400frame);
		spi_stats.nDiscarded++;
		return FALSE;
	}

	if(!enqueFrame(&spiTxQueue, SrcFrame, SrcFrame->length))
		return FALSE;

	spi_stats.nIBIP400Frames++;

	return TRUE;
}

/*
 * clean up any queued frames
 */
void EmptySPIFrameQ(void)
{
	while(quehasData(&spiTxQueue))	{
		IP400_FRAME * fr = dequeFrame(&spiTxQueue);
		spi_stats.nDiscarded++;
		DeleteFrame(fr);
	}
	return;
}

/*
 * dump the SPI stats
 */
void PrintSPIStats(void)
{
	if(spiActive)
		USART_Print_string("SPI is active\r\n");
	else
		USART_Print_string("SPI is not active\r\n");

	USART_Print_string("Number of IP400->SPI Frames: %d\r\n", spi_stats.nIBIP400Frames);
	USART_Print_string("Number of SPI->IP400 Frames: %d\r\n", spi_stats.nOBIP400Frames);

	USART_Print_string("\r\nSPI Single frames->%d\r\n", spi_stats.nSingle);
	USART_Print_string("SPI First fragment frames->%d\r\n", spi_stats.nFirstFrames);
	USART_Print_string("SPI Mid fragment frames->%d\r\n", spi_stats.nMidFrames);
	USART_Print_string("SPI Last fragment frames->%d\r\n", spi_stats.nLastFrames);
	USART_Print_string("SPI discarded frames->%d\r\n", spi_stats.nDiscarded);

}

/*
 * reset the stats
 */
void ResetSPIStats(void)
{
	memset(&spi_stats, 0, sizeof(SPI_STATS));
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

	SPI_HDR_STATUS defStat;
	defStat.frameStat.status = NO_FRAME;
	spiTxBuffer.spiData.hdr.spiStat = defStat.status_byte;

	spiActive = FALSE;					// no activity yet
	spiActivityTimer = 0;

	// tx (outbound) frame queue
	spiTxQueue.q_forw = &spiTxQueue;
	spiTxQueue.q_back = &spiTxQueue;

	// create the inbound frame queue
	spiRxQueue.q_forw = &spiRxQueue;
	spiRxQueue.q_back = &spiRxQueue;

	if((spiRawFrame = nodeMemAlloc(SPI, SPI_RAW_LEN)) == NULL)
		return;

	// clear stats
	ResetSPIStats();

#if __INCLUDE_SPI
	// start the ball rolling..
	if((spiXfer = HAL_SPI_TransmitReceive_DMA(&GPIO_SPI_HANDLE, spiTxBuffer.rawData, (uint8_t *)spiRawFrame, SPI_RAW_LEN)) != HAL_OK)
		spiErrorOccurred = TRUE;
#endif

}

// execute the task
void SPI_Task_Exec(void)
{

#if __INCLUDE_SPI
	// check the status first: repost Rx if an error occurred and it is now ready
	// realloc memory for receive if it failed in ISR
	if(spiErrorOccurred)	{
		if(GPIO_SPI_HANDLE.State == HAL_SPI_STATE_READY)	{
			if(spiRawFrame == NULL)	{
				if((spiRawFrame = nodeMemAlloc(SPI, SPI_RAW_LEN)) == NULL)	{
					return;
				}
			}
			if((spiXfer = HAL_SPI_TransmitReceive_DMA(&GPIO_SPI_HANDLE, spiTxBuffer.rawData, (uint8_t *)spiRawFrame, SPI_RAW_LEN)) == HAL_OK)	{
				spiErrorOccurred = FALSE;
			}
			spiExchangeComplete = FALSE;
			spiActivityTimer = 0;				// reset no activity timer
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
	if(spiActive && !spiExchangeComplete)		{
		spiActivityTimer += 1;
		if(spiActivityTimer >= NO_SPI_TIMEOUT)	{
			EmptySPIFrameQ();
			spiActive = FALSE;
			spiActivityTimer = 0;
		}
		return;
	}

	// revive the active status if an exchange occurred;
	// else keep emptying the queue
	if(spiExchangeComplete)	{
		spiExchangeComplete = FALSE;		// reset exchange done
		spiActive = TRUE;					// indicate that the SPI is active..
		spiActivityTimer = 0;				// reset no activity timer
	} else {
		EmptySPIFrameQ();
	}

	/*
	 * Inbound frame from SPI. Queue for transmit
	 */
	SPI_BUFFER *spiRxFrame;
	// check for an inbound frame to send
	if((spiRxFrame = (SPI_BUFFER *)dequeFrame(&spiRxQueue)) != NULL)	{
		int rxSegLen =  ((uint16_t)spiRxFrame->spiData.hdr.length_hi)<<8;
		rxSegLen += ((uint16_t)spiRxFrame->spiData.hdr.length_lo);
		SendSPIFrame(&spiRxFrame->spiData.hdr, (uint8_t *)&spiRxFrame->spiData.buffer, rxSegLen);
		spi_stats.nOBIP400Frames++;
		nodeMemFree(SPI,spiRxFrame);
	}

	/*
	 * Outbound frame for SPI
	 */
	IP400_FRAME *txFrame;
	if((txFrame=dequeFrame(&spiTxQueue)) == NULL)	{
		spiTxBuffer.spiData.hdr.spiStat = NO_FRAME;

	// if we have a buffer manager, put the available bytes in the length field
#if defined(__NUCLEOCC2) || defined(__PI_BOARD)
		RADIO_STATS *stats = GetRadioStats(XCVR_WL33);
		BUFFER_STATUS *bfrStatus = (BUFFER_STATUS *)stats->bfrStatus;
		if(bfrStatus != NULL)	{
			uint16_t txAvail = bfrStatus->txSize - bfrStatus->length;
			spiTxBuffer.spiData.hdr.length_hi = (uint8_t)(txAvail>>8);
			spiTxBuffer.spiData.hdr.length_lo = (uint8_t)(txAvail&0xFF);
		}
#endif
	} else {

		/*
		 * reformat an IP400 frame into an SPI frame
		 */
		// step 0: common fields
		memcpy(&spiTxBuffer.spiData.hdr.fromCall, txFrame->source.callbytes.callsign.bytes, N_CALL);
		memcpy(&spiTxBuffer.spiData.hdr.fromIP, txFrame->source.vpnBytes.vpn, N_IPBYTES);

		memcpy(&spiTxBuffer.spiData.hdr.toCall, txFrame->dest.callbytes.callsign.bytes, N_CALL);
		memcpy(&spiTxBuffer.spiData.hdr.toIP, txFrame->dest.vpnBytes.vpn, N_IPBYTES);

		// flag fields: untouched by man or machine
		spiTxBuffer.spiData.hdr.coding = txFrame->flagfld.flags.coding;

		uint8_t frag = txFrame->flagfld.flags.fragmentation;

		// so payload related stuff
		uint8_t *payload = (uint8_t *)txFrame->buf;
		uint16_t length = txFrame->length;
		length += ((uint16_t)txFrame->flagfld.flags.payloadMSB) << 8;

		// extended calls
		if(txFrame->flagfld.flags.srcExt)	{
			memcpy(payload, (void *)&txFrame->srcExt, N_CALL);
			payload += N_CALL;
		}
		if(txFrame->flagfld.flags.destExt)	{
			memcpy(payload, (void *)&txFrame->destExt, N_CALL);
			payload += N_CALL;
		}
		// hop table
		if(txFrame->flagfld.flags.hoptable)	{
			SPI_HOPTABLE *hSPItable = (SPI_HOPTABLE *)payload;
			HOPTABLE *hIP400 = (HOPTABLE *)txFrame->hopTable;
			for(int i=0;i<MAX_HOPS;i++)	{
				hSPItable->hopEntry[i].callentry.callsign.encoded = hIP400->rptCalls[i].callbytes.callsign.encoded;
				hSPItable->hopEntry[i].flags = hIP400->hopflags[i].flags;
			}
			payload += sizeof(SPI_HOPTABLE);
		}
		// remainder of payload
		memcpy(spiTxBuffer.spiData.buffer, payload, length);

		switch(frag)	{

			case FRAG_SELFCONTAINED:
				spiTxBuffer.spiData.hdr.offset_hi = 0;
				spiTxBuffer.spiData.hdr.offset_lo = 0;
				spiTxBuffer.spiData.hdr.length_lo = (uint8_t)length & 0xff;
				spiTxBuffer.spiData.hdr.length_hi = (uint8_t)(length >>8);
				spiTxBuffer.spiData.hdr.spiStat = SINGLE_FRAME;
				fragOffset = 0;
				spi_stats.nSingle++;
				break;

			case FRAG_FIRST_FRAG:
				spiTxBuffer.spiData.hdr.offset_hi = 0;
				spiTxBuffer.spiData.hdr.offset_lo = 0;
				spiTxBuffer.spiData.hdr.length_lo = (uint8_t)length & 0xff;
				spiTxBuffer.spiData.hdr.length_hi = (uint8_t)(length >>8);
				spiTxBuffer.spiData.hdr.spiStat = FIRST_FRAGMENT;
				fragOffset = length;
				spi_stats.nFirstFrames++;
				break;

			case FRAG_MIDDLE_FRAG:
				spiTxBuffer.spiData.hdr.offset_hi = (uint8_t)(fragOffset>>8);
				spiTxBuffer.spiData.hdr.offset_lo = (uint8_t)(fragOffset&0xFF);
				spiTxBuffer.spiData.hdr.length_lo = (uint8_t)length & 0xff;
				spiTxBuffer.spiData.hdr.length_hi = (uint8_t)(length >>8);
				spiTxBuffer.spiData.hdr.spiStat = MIDDLE_FRAGMENT;
				fragOffset += length;
				spi_stats.nMidFrames++;
				break;

			case FRAG_END_FRAG:
				spiTxBuffer.spiData.hdr.offset_hi = (uint8_t)((2*fragOffset)>>8);
				spiTxBuffer.spiData.hdr.offset_lo = (uint8_t)((2*fragOffset)&0xFF);
				spiTxBuffer.spiData.hdr.length_lo = (uint8_t)length & 0xff;
				spiTxBuffer.spiData.hdr.length_hi = (uint8_t)(length >>8);
				spiTxBuffer.spiData.hdr.spiStat = LAST_FRAGMENT;
				fragOffset = 0;
				spi_stats.nLastFrames++;
				break;
		}
		DeleteFrame(txFrame);
	}
}

// test if an inbound frame is valid
BOOL isIP400Frame(uint8_t *eye)
{
	if((eye[0] == 'I') && (eye[1] == 'P') && (eye[2] == '4') && (eye[3] == 'C'))
			return TRUE;

	return FALSE;
}

#if __INCLUDE_SPI
// rx done callback
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef * hspi)
{

	SPI_HDR_STATUS ibStatus;

	// interrupt is not for me...
	if(hspi->Instance != GPIO_SPI_INST)
		return;

	// scope trigger on nucleo board
#if	defined(__NUCLEOCC2) || defined(__POWER_NODE)	// board type in use
	HAL_GPIO_TogglePin(SCOPE_GPIO_Port, SCOPE_Pin);
#endif
#ifdef __PI_BOARD
	// temporarily toggle PB0 for the mini-node
	HAL_GPIO_TogglePin(GPIO_PB0_GPIO_Port, GPIO_PB0_Pin);
#endif

	// set data valid and start a new transfer
	if(hspi->State == HAL_SPI_STATE_READY)		{
		spiExchangeComplete = TRUE;

		// if the receiver has a valid frame, queue it and allocate another
		ibStatus.status_byte = spiRawFrame->spiData.hdr.spiStat;
		spiFrameStatus rstat = ibStatus.frameStat.status;

		// frame with status in the correct range
		SPI_BUFFER *oldBuffer = spiRawFrame;
		if((rstat > NO_FRAME) && (rstat < N_STATUS) && isIP400Frame(spiRawFrame->spiData.hdr.eye))	{
			// queue the frame. If it fails, just re-use it
			if(enqueFrame(&spiRxQueue, (IP400_FRAME *)spiRawFrame, 0))	{
				if((spiRawFrame = nodeMemAlloc(SPI, SPI_RAW_LEN)) == NULL)	{
					spiErrorOccurred = TRUE;
					spiRawFrame = oldBuffer;
					return;
				}
			}
		}

		// next transfer
		spiXfer = HAL_SPI_TransmitReceive_DMA(&GPIO_SPI_HANDLE, spiTxBuffer.rawData, (uint8_t *)spiRawFrame, SPI_RAW_LEN);
		if(spiXfer != HAL_OK)
			spiErrorOccurred = TRUE;
		return;
	}
	spiErrorOccurred = TRUE;			// spi not ready
}
#endif

// other HAL calls are here...

#if _HAS_FPGA
#include "fpga.h"
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef * hspi)
{
	if(hspi->Instance != FPGA_INST)
		return;
	FPGASpiTxDMAComplete();
}
//
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef * hspi)
{
	if(hspi->Instance != FPGA_INST)
		return;
}

#endif

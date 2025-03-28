/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        spitask.c

        Author:           VE6VH

        Creation Date:    Mar. 6, 2025

        Description:      {Definition here...)

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "types.h"
#include "spidefs.h"
#include "logger.h"
#include "dataq.h"

// locals
int spiDevFD;				// spi device file descriptor
int spiDevNum;				// device number

// transmittter states
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

// SPI transmit frame queue
FRAME_QUEUE	SPITxQueue;

// frame buffers
static SPI_BUFFER spiTxBuffer;
static SPI_BUFFER spiRxBuffer;

static union	{
	struct {
		struct spi_hdr_t	hdr;		// header
		uint8_t	buffer[PAYLOAD_MAX];
	} spiData;
	uint8_t	rawData[PAYLOAD_MAX + sizeof(struct spi_hdr_t)];
} rxFrameBuffer;

// frame validator
BOOL isIP400Frame(uint8_t *eye);

BOOL EnqueSPIFrame(void *spiFrame)
{
	SPI_DATA_FRAME *qFrame, *SrcFrame = (SPI_DATA_FRAME *)spiFrame;
	uint8_t *frameBuffer;

	// allocate an IP400 frame
	if((qFrame=malloc(sizeof(SPI_DATA_FRAME)))== NULL)
		return FALSE;
	memcpy(qFrame, SrcFrame, sizeof(SPI_DATA_FRAME));

	// alloc the data portion of the frame
	if((frameBuffer=malloc(SrcFrame->length)) == NULL)	{
		return FALSE;
	}

	memcpy(frameBuffer, (uint8_t *)SrcFrame->buffer, SrcFrame->length);
	qFrame->buffer = frameBuffer;
	qFrame->length = SrcFrame->length;

	if(!enqueFrame(&SPITxQueue, qFrame))
		return FALSE;

	return TRUE;
}

/*
 * Initialize the SPI task
 */
BOOL spiTaskInit(int spiDev)
{
	// open the SPI device
	if((spiDevFD = spi_open(spiDev)) == -1)	{
		logger(LOG_ERROR, "Unable to open SPI device %d\n", spiDev);
		return FALSE;
	}

	spiTxBuffer.spiData.hdr.status = NO_FRAME;
	spiRxBuffer.spiData.hdr.status = NO_FRAME;

	// init the tx queue
	SPITxQueue.q_forw = &SPITxQueue;
	SPITxQueue.q_back = &SPITxQueue;

	// init vars
	spiDevNum = spiDev;
	SPITxState = SPITXIDLE;
	SPIRxState = SPIRXIDLE;

	rxFrameBuffer.spiData.hdr.eye[0] = 'I';
	rxFrameBuffer.spiData.hdr.eye[1] = 'P';
	rxFrameBuffer.spiData.hdr.eye[2] = (400 >> 8) & 0xff;
	rxFrameBuffer.spiData.hdr.eye[3] = (400 & 0xff);

	return TRUE;
}

/*
 * Run the SPI functions.
 * Poll receiver for an inbound frame and read it. Add it to the frame queue.
 * Poll transmit queue for an outbound frame and send it.
 */
void spiTask(void)
{
	static SPI_DATA_FRAME *txFrame;
	static uint16_t txSegLength;
	static uint8_t *prxData;
	static uint16_t rxSegLen;

	memset(spiRxBuffer.rawData, 0, SPI_RAW_LEN);

	// do an exchange with the STM32
	int nxferred = spi_fdtransfer(spiDevNum, spiTxBuffer.rawData, spiRxBuffer.rawData, SPI_RAW_LEN);
	if(nxferred == -1)	{
		logger(LOG_ERROR, "SPI transmit error %d: %s\n", nxferred, geterrno(nxferred));
	}

	/*
	 * Inbound frame. Reassemble fragments if needed...
	 */
	// we have to declare these here, as teh !#)$*& compiler will not allow declarations in statements
	uint8_t rstat = NO_FRAME;
	uint8_t fragStat;
	uint16_t offset;
	uint16_t frameLen=0, prevLen=0;

	switch(SPIRxState)	{

	case SPIRXIDLE:
		rstat = spiRxBuffer.spiData.hdr.status;

		// next validate the frame type
		if((rstat == NO_FRAME) || (rstat >= N_FRAGS))
			break;

		// validate the eye of the frame
		if(!isIP400Frame(spiRxBuffer.spiData.hdr.eye))
			break;

		// copy the header fields
		memcpy(&rxFrameBuffer.spiData.hdr, &spiRxBuffer.spiData.hdr, sizeof(struct spi_hdr_t));

		// copy data fields
		prxData =  rxFrameBuffer.spiData.buffer;
		rxSegLen = (spiRxBuffer.spiData.hdr.length_hi << 8) + spiRxBuffer.spiData.hdr.length_lo;
		memcpy(prxData, spiRxBuffer.spiData.buffer, rxSegLen);

		if(rstat != SINGLE_FRAME)	{
			SPIRxState = SPIRXFRAG;
		} else {
			rxFrameBuffer.spiData.hdr.status = SINGLE_FRAME;
			rxFrameBuffer.spiData.hdr.length_hi = (rxSegLen >> 8);
			rxFrameBuffer.spiData.hdr.length_lo = (rxSegLen & 0xFF);
			rxFrameBuffer.spiData.hdr.offset_hi = rxFrameBuffer.spiData.hdr.offset_lo = 0;
			send_udp_packet(rxFrameBuffer.rawData, rxSegLen+sizeof(struct spi_hdr_t));
			SPIRxState = SPIRXIDLE;
			spiRxBuffer.spiData.hdr.status = N_FRAGS;	// set an invalid frame type
		}
		break;

	case SPIRXFRAG:
		fragStat = spiRxBuffer.spiData.hdr.status;
		offset = ((uint16_t)spiTxBuffer.spiData.hdr.offset_hi << 8) + (uint16_t)spiTxBuffer.spiData.hdr.offset_lo;
		prxData = rxFrameBuffer.rawData + offset;
		rxSegLen = (spiRxBuffer.spiData.hdr.length_hi << 8) + spiRxBuffer.spiData.hdr.length_lo;
		memcpy(prxData, spiRxBuffer.spiData.buffer, rxSegLen);

		if(fragStat == LAST_FRAGMENT)	{
			frameLen = offset + rxSegLen;
			spiRxBuffer.spiData.hdr.length_hi = (frameLen >> 8);
			spiRxBuffer.spiData.hdr.length_lo = (frameLen & 0xFF);
			// process frame for tx here...
			SPIRxState = SPIRXIDLE;					// placeholder
		}
		break;
	}

	/*
	 * process an outbound frame
	 * Fragment it if longer that 500 byte, s->length);s
	 */
	switch(SPITxState)	{

	case SPITXIDLE:
		if((txFrame=dequeFrame(&SPITxQueue)) == NULL)	{
			spiTxBuffer.spiData.hdr.status = NO_FRAME;
			break;
		}
		txSegLength = txFrame->length;
		spiTxBuffer.spiData.hdr.status = SINGLE_FRAME;

		// fragment the frame if needed
		if(txFrame->length > SPI_BUFFER_LEN)	{
			txSegLength = SPI_BUFFER_LEN;
			spiTxBuffer.spiData.hdr.status = FRAME_FRAGMENT;
			SPITxState = SPITXFRAG;
		}

		// send the frame and/or fragment
		memcpy(spiTxBuffer.rawData, txFrame->buffer, txSegLength);
		spiTxBuffer.spiData.hdr.offset_hi = spiTxBuffer.spiData.hdr.offset_lo = 0;

		// release memory if only a single frame
		if(spiTxBuffer.spiData.hdr.status == SINGLE_FRAME)
		{
			free(txFrame->buffer);
			free(txFrame);
		}
		break;

	case SPITXFRAG:
		offset = ((uint16_t)(spiTxBuffer.spiData.hdr.offset_hi) << 8) + (uint16_t)spiTxBuffer.spiData.hdr.offset_lo;
		prevLen = ((uint16_t)(spiTxBuffer.spiData.hdr.length_hi) << 8) + (uint16_t)spiTxBuffer.spiData.hdr.length_lo;
		offset += prevLen;
		txFrame->length -= prevLen;
		txSegLength = txFrame->length;
		if(txFrame->length > SPI_BUFFER_LEN)	{
			spiTxBuffer.spiData.hdr.status = FRAME_FRAGMENT;
			txSegLength = SPI_BUFFER_LEN;
		} else {
			spiTxBuffer.spiData.hdr.status = LAST_FRAGMENT;
			SPITxState = SPITXIDLE;
		}

		// send the next fragment
		void *fragAddr = txFrame->buffer + offset;
		memcpy(spiTxBuffer.spiData.buffer, fragAddr, txSegLength);
		spiTxBuffer.spiData.hdr.offset_hi = (offset >> 8);
		spiTxBuffer.spiData.hdr.offset_lo = (offset & 0xff);
		spiTxBuffer.spiData.hdr.length_hi = (txSegLength >> 8);
		spiTxBuffer.spiData.hdr.length_lo = (txSegLength & 0xFF);

		// done with frame
		if(spiTxBuffer.spiData.hdr.status == LAST_FRAGMENT)
		{
			free(txFrame->buffer);
			free(txFrame);
		}
		break;
	}

}

// validate the frame eye
BOOL isIP400Frame(uint8_t *eye)
{
	if((eye[0] != 'I') || (eye[1] != 'P'))
		return FALSE;

	if((eye[2] != '4') || (eye[3] != 'C'))
		return FALSE;

	return TRUE;

}

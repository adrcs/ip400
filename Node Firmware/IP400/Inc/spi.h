/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      spi.h

	Author:		      MartinA

	Creation Date:	  Jan 26, 2025

	Description:	  Definitions for SPI mod

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/

#ifndef INC_SPI_H_
#define INC_SPI_H_

#include "frame.h"

// SPI frame header
typedef struct spi_hdr_t	{
	uint8_t	eye[4];						// 'IP4X'
	uint8_t	status;						// status byte
	uint8_t offset_hi;					// high offset
	uint8_t offset_lo;					// low offset
	uint8_t	length_hi;					// length hi
	uint8_t	length_lo;					// length lo
	uint8_t	fromCall[N_CALL];			// from callsign
	uint8_t fromIP[N_IPBYTES];			// from port
	uint8_t	toCall[N_CALL];				// to callsign
	uint8_t toIP[N_IPBYTES];			// to port
	uint8_t coding;						// packet coding
	uint8_t hopCount;					// hop count
	uint8_t flags;						// remaining flags
} SPI_HEADER;

#define	SPI_BUFFER_LEN		400		// 400 bytes/transfer
#define	SPI_RAW_LEN			SPI_BUFFER_LEN + sizeof(struct spi_hdr_t)

// status values
typedef enum	{
		NO_DATA=0,		// no data available
		SINGLE_FRAME,	// single frame
		FRAGMENT,		// fragment
		LAST_FRAGMENT,	// last fragment
		NUM_STATS		// number of stats
} spiFrameStatus;

// data buffer struct
typedef union	{
	struct {
		SPI_HEADER	hdr;		// header
		uint8_t	buffer[SPI_BUFFER_LEN];
	} spiData;
	uint8_t	rawData[SPI_RAW_LEN];
} SPI_BUFFER;

#define	SPI_TIMEOUT				100				// SPI timeout

BOOL EnqueSPIFrame(void *raw);

#endif /* INC_SPI_H_ */

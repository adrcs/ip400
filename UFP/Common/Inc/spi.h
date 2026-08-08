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

#include <frame.h>
#include "dataq.h"

// new def's imported from node code
#define	SPI_BUFFER_LEN		500		// 400 bytes/transfer
#define	SPI_RAW_LEN			SPI_BUFFER_LEN + sizeof(struct spi_hdr_t)

// SPI flag field: lower 8 bits of IP400 frame flags
// definitions in frame.h
typedef struct spi_flags_t {
	unsigned modem:			2;		// target modem
	unsigned FECMethod:		3;		// FEC method
	unsigned bitsperCarr:	3;		// bits per carrier
	unsigned bandwidth:		2;		// on air bandwidth
	unsigned vacant:		2;		// msb's of 2nd byte are unused
	unsigned hoptable:		1;		// hop table is included
	unsigned srcExt:		1;		// source call is extended
	unsigned destExt:		1;		// dest call is extended
	unsigned payloadMSB:	1;		// msb of payload
} SPI_FLAGS;

typedef union {
	uint8_t		bytedefs[2];		// flag field
	uint16_t	alldefs;			// all definitions
	SPI_FLAGS 	bitdefs;			// bit defintions
} SPI_BYTE_FLAGS;

// SPI frame header
typedef struct spi_hdr_t	{
	uint8_t	eye[4];						// 'IP4X'			4
	uint8_t	spiStat;					// status byte		5
	uint8_t offset_hi;					// offset hi		6
	uint8_t	offset_lo;					// offset lo		7
	uint8_t length_hi;					// length hi		8
	uint8_t	length_lo;					// length lo		9
	uint8_t	fromCall[N_CALL];			// from callsign	13
	uint8_t fromIP[N_IPBYTES];			// from port		15
	uint8_t	toCall[N_CALL];				// to callsign		19
	uint8_t toIP[N_IPBYTES];			// to port			21
	uint8_t coding;						// packet coding	22
	uint8_t flagsMSB;					// was hop count	23
	uint8_t	flagsLSB;					// flags			24
} SPI_HEADER;

typedef union spi_stat_u	{
	struct spi_status_t	{
		unsigned	status:3;			// frame status
		unsigned	reserved:4;			// reserved
		unsigned	busy:1;				// busy bit
	} frameStat;
	uint8_t	status_byte;
} SPI_HDR_STATUS;

// hop table entry
typedef struct spi_hop_entry_t {
	IP400_CALL	callentry;				// callsign
	HOP_FLAGS	flags;					// hop flags
} SPI_HOP_ENTRY;

// hop table in SPI format
typedef struct spi_hop_tbl_t	{
	SPI_HOP_ENTRY	hopEntry[MAX_HOPS];
} SPI_HOPTABLE;

// status values
typedef enum	spi_frame_stat_e {
		NO_FRAME=0,			// no data available
		SINGLE_FRAME,		// single frame
		FIRST_FRAGMENT,		// fragment
		MIDDLE_FRAGMENT,	// middle of the pack
		LAST_FRAGMENT,		// last fragment
		N_STATUS			// last status
} spiFrameStatus;

// data buffer struct
typedef union	{
	struct {
		struct spi_hdr_t	hdr;		// header
		uint8_t	buffer[SPI_BUFFER_LEN];
	} spiData;
	uint8_t	rawData[SPI_RAW_LEN];
} SPI_BUFFER;


#define	SPI_BUFFER_LEN		500			// approx 1/2 the max payload size
#define	SPI_RAW_LEN			SPI_BUFFER_LEN + sizeof(struct spi_hdr_t)

// spi stats
typedef struct spi_stats_t {
	int nSingle;						// number of single frames
	int nFirstFrames;					// number of fragmented frames
	int nMidFrames;						// middle frames
	int nLastFrames;					// number of last frames
	int nIBIP400Frames;					// number of inbound IP400 frames
	int nOBIP400Frames;					// number of outbound IP400 frames
	int nDiscarded;						// discarded frames
} SPI_STATS;

#define	SPI_TIMEOUT				100				// SPI timeout

void PrintSPIStats(void);
void ResetSPIStats(void);
BOOL isIP400Frame(uint8_t *eye);

BOOL EnqueSPIFrame(void *ip400frame);
void SendSPIFrame(void *spi, uint8_t *payload, int len);


#endif /* INC_SPI_H_ */

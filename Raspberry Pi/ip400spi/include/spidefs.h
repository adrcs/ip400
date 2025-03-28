/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        spidefs.h

        Author:           root

        Creation Date:    Mar. 6, 2025

        Description:      Definitions for the SPI interface

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/
#ifndef SPIDEFSH
#define SPIDEFSH

#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "types.h"
#include "frame.h"

// typedefs
typedef   uint8_t				SPI_WORD;     // single byte
typedef   uint16_t 				SPI_DWORD;    // double byte
typedef   uint32_t				SPI_QWORD;	  // quad word

// SPI defines
enum {
	SPI_0_0=0,			// SPI device 0 CS 0
	SPI_0_1,			// SPI device 0 CS 1
	SPI_1_0,			// SPI device 1 CS 0
	SPI_1_1,			// SPI device 1 CS 1
	SPI_1_2,			// SPI device 1 CS 2
	N_SPI				// number of SPI devices
};

#define   SPI_0_DEV_0       "/dev/spidev0.0"
#define   SPI_0_DEV_1       "/dev/spidev0.1"
#define   SPI_1_DEV_0       "/dev/spidev1.0"
#define   SPI_1_DEV_1       "/dev/spidev1.1"
#define   SPI_1_DEV_2       "/dev/spidev1.2"
#define   SPI_SPEED         500000
#define   SPI_SLOW_SPEED    100000
#define	  SPI_NBITS	    	8				// All Spi's are 8 bits
#define   SPI_RESET_COUNT   10				// number of times to hold spi reset

// status returns from updates
#define   SPI_REG_SUCCESS   0               // successful completion
#define   SPI_REG_INVALID   -1              // invalid register called

// new def's imported from node code
#define	SPI_BUFFER_LEN		400		// 400 bytes/transfer
#define	SPI_RAW_LEN			SPI_BUFFER_LEN + sizeof(struct spi_hdr_t)

// status values
enum {
	NO_FRAME=0,				// no frame data to process
	SINGLE_FRAME,			// single frame
	FRAME_FRAGMENT,			// fragment
	LAST_FRAGMENT,			// last fragment
	N_FRAGS					// number of frags
};

#define		PAYLOAD_MAX		1053	// max frame payload

// SPI frame header
struct spi_hdr_t	{
	uint8_t	eye[4];						// 'IP4X'
	uint8_t	status;						// status byte
	uint8_t offset_hi;					// high offset
	uint8_t offset_lo;					// low offset
	uint8_t	length_hi;					// length hi
	uint8_t	length_lo;					// length lo
	uint8_t	fromCall[N_CALL];			// from callsign
	uint8_t fromPort[IP_400_PORT_SIZE];	// from port
	uint8_t	toCall[N_CALL];				// to callsign
	uint8_t toPort[IP_400_PORT_SIZE];	// to port
	uint8_t coding;						// packet coding
	uint8_t hopCount;					// hop count
	uint8_t flags;						// remaining flags
};

// data buffer struct
typedef union	{
	struct {
		struct spi_hdr_t	hdr;		// header
		uint8_t	buffer[SPI_BUFFER_LEN];
	} spiData;
	uint8_t	rawData[SPI_RAW_LEN];
} SPI_BUFFER;

// SPI struct
typedef struct spi_config_t	{
	uint8_t		 	mode;				// mode
	uint8_t		 	bitsPerWord;		// bits/word
	uint32_t	 	speed;				// speed
	int				fd;					// file descriptor
	uint8_t			debug;				// debug
} SPI_CONFIG;

// SPI data frame
typedef struct spi_data_frame_t	{
	void			*buffer;			// data buffer
	uint16_t		length;				// length
} SPI_DATA_FRAME;

//  SPI task
int spi_lookup(char *devName);
BOOL spiTaskInit(int spiDev);
void spiTask(void);

// SPI functions
void spi_setup(int device, uint8_t spiMode, uint8_t spibitsPerWord, int spiSpeed, uint8_t debug);
int spi_open(int device);
int spi_close(int device);
int spi_fdtransfer(int device, SPI_WORD *txdata, SPI_WORD *rxdata, int length);
int spi_hdread(int fd, uint8_t *data, uint16_t length);
int spi_hdwrite(int fd, uint8_t *data, uint16_t length);

// data queuing functions
BOOL EnqueSPIFrame(void *spiFrame);
BOOL isIP400Frame(uint8_t *eye);

// UDP stuff
BOOL setup_udp_socket(char *hostname, int hostport, int localport);
BOOL send_udp_packet(void *data, uint16_t length);

#endif


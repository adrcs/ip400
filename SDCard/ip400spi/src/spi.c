/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        spi.c

        Author:           Martin VE6VH

        Creation Date:    Mar. 6, 2025

        Description:      {Definition here...)

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/
#include <string.h>

#include "spidefs.h"
#include "logger.h"

// translate SPI logical devices to real devices
char *devnames[N_SPI] = {
		SPI_0_DEV_0,
		SPI_0_DEV_1,
		SPI_1_DEV_0,
		SPI_1_DEV_1,
        SPI_1_DEV_2
};

SPI_CONFIG spi_config[N_SPI];	// SPI configurations

/*
 * setup the SPI devices
 */
void spi_setup(int device, uint8_t spiMode, uint8_t spibitsPerWord, int spiSpeed, uint8_t debug)
{
    spi_config[device].mode = spiMode;
    spi_config[device].bitsPerWord = spibitsPerWord;
    spi_config[device].speed = spiSpeed;
    spi_config[device].debug = debug;

}

int spi_lookup(char *devName)
{
	for(int i=0;i<N_SPI;i++)
		if(!strcmp(devName, devnames[i]))
			return i;

	return -1;
}

/*
 * open an SPI device
 */
int spi_open(int device)
{
    int	spifd = -1;
    int statusVal = -1;

	if(spi_config[device].debug)
		logger(LOG_NOTICE, "SPI Open %d:\n", device);

    spifd = open(devnames[device], O_RDWR);
    if(spifd < 0)	{
  		logger(LOG_FATAL, "OPEN failed on %s\n", devnames[device]);
		return spifd;
    }

    statusVal = ioctl (spifd, SPI_IOC_WR_MODE, &spi_config[device].mode);
    if(statusVal < 0) {
   		logger(LOG_ERROR, "IOCTL failed: wr_mode\n");
		return(statusVal);
    }

    statusVal = ioctl (spifd, SPI_IOC_RD_MODE, &spi_config[device].mode);
    if(statusVal <  0) {
   		logger(LOG_ERROR, "IOCTL failed: rd_mode\n");
		return(statusVal);
    }

    statusVal = ioctl (spifd, SPI_IOC_WR_BITS_PER_WORD, &spi_config[device].bitsPerWord);
    if(statusVal < 0) {
   		logger(LOG_ERROR, "IOCTL failed: wr_bits/word\n");
    	return(statusVal);
    }

    statusVal = ioctl (spifd, SPI_IOC_RD_BITS_PER_WORD, &spi_config[device].bitsPerWord);
    if(statusVal < 0) {
   		logger(LOG_ERROR, "IOCTL failed: rd_bits/word\n");
    	return(statusVal);
    }

    statusVal = ioctl (spifd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_config[device].speed);
    if(statusVal < 0) {
   		logger(LOG_ERROR, "IOCTL failed: wr_speed\n");
    	return(statusVal);
    }

    statusVal = ioctl (spifd, SPI_IOC_RD_MAX_SPEED_HZ, &spi_config[device].speed);
    if(statusVal < 0) {
   		logger(LOG_ERROR, "IOCTL failed: rd_speed\n");
    	return(statusVal);
    }

    spi_config[device].fd = spifd;

	if(spi_config[device].debug)	{
		logger(LOG_NOTICE, "Opened device %s with fd %d\n", devnames[device], spifd);
    }

    return spifd;
}

/*
 * close an SPI device
 */
int spi_close(int spifd)
{
    int statusVal = -1;
    statusVal = close(spifd);
    return statusVal;
}

/*
 * perform a half duplex write
 */
int spi_hdwrite(int fd, uint8_t *data, uint16_t length)
{
	return write(fd, data, length);
}

/*
 * perform a half duplex read
 */
int spi_hdread(int fd, uint8_t *data, uint16_t length)
{
	return read(fd, data, length);
}

/*
 * perform a full duplex read/write to the SPI using IOCTL
 */
int spi_fdtransfer(int device, SPI_WORD *txdata, SPI_WORD *rxdata, int length)
{

	static struct spi_ioc_transfer spi;
	int retVal = -1;

	memset(&spi, 0, sizeof(spi));

	spi.tx_buf        = (unsigned long)(txdata);
	spi.rx_buf        = (unsigned long)(rxdata);
	spi.len           = sizeof(SPI_WORD)*length;
	spi.delay_usecs   = 0 ;
	spi.speed_hz      = spi_config[device].speed;
	spi.bits_per_word = spi_config[device].bitsPerWord ;
	spi.cs_change 	 = 0;
	spi.tx_nbits      = 8;
	spi.rx_nbits      = 8;
	spi.pad           = 0;

	retVal = ioctl (spi_config[device].fd, SPI_IOC_MESSAGE(1), &spi) ;

	if(spi_config[device].debug)	{
		logger(LOG_NOTICE, "Did IOCTL on %s with fd %d\n", devnames[device], spi_config[device].fd);
		logger(LOG_NOTICE, "Return value %d\n", retVal);
	}

	if(retVal < 0)
		return retVal;

	return retVal;
}


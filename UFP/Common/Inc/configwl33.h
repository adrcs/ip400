/*---------------------------------------------------------------------------
	Project:	      WL33_E04_RPI

	File Name:	      config.h

	Author:		      MartinA

	Creation Date:	  Jan 27, 2025

	Description:	  Configuration parameters

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_CONFIGWL33_H_
#define INC_CONFIGWL33_H_

#include "main.h"

// Baud rate for host communication.
// Baud rate for serial debugging.
#define DEBUGGING_SPEED	38400
#define SERIAL_SPEED	115200

#ifdef __NUCLEOCC2
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern SPI_HandleTypeDef hspi1;
// UAR/T Configuration
#define	__ENABLE_GPS			0				// board does not have GPS receiver
#define	__INCLUDE_KISS			1				// include kiss mode code
#define	ENABLE_KISS_ON_DEBUG	0				// 0: use main uart, 1: DEBUG UART
//
#define _HAS_CODEC				0				// has a codec
#define	_HAS_INT_RADIO			1				// has an internal radio
#define	_HAS_I2S				0				// has the TI codec on an I2S port
#define	_HAS_FPGA				0				// has an FPGA
//
#define	GPIO_SPI_HANDLE		hspi1
#define	HUART				huart1				// main uart
#define	HDEBUG				huart2				// debug uart
#define	DEBUG_INST			USART2				// Instance of the UART
#define	GPIO_SPI_INST		SPI1				// instance of the SPI UART
// configs for different board types
#define	__KISS_ON_SPI		0					// Kiss packets on the SPI as well
#define	__INCLUDE_SPI		1					// nucleo can have SPI
// transceiver types
#define	__XCVR_WL33			1					// has a WL33
#define	__XCVR_AT86			0					// has an AT86RF215
#define	__XCVR_OFDM_AB		0					// has MODE B
#define	N_XCVRS				(__XCVR_WL33+__XCVR_AT86+__XCVR_OFDM_AB)	// number of tranceivers
#endif
//
#ifdef __PI_BOARD
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef hlpuart1;
extern SPI_HandleTypeDef hspi3;
// UAR/T Configuration
#define	__ENABLE_GPS			0				// board does not have GPS receiver
#define	__INCLUDE_KISS			1				// include kiss mode code
#define	ENABLE_KISS_ON_DEBUG	0				// 0: use main uart, 1: DEBUG UART
//
#define _HAS_CODEC				0				// has a codec
#define	_HAS_INT_RADIO			1				// has an internal radio
#define	_HAS_I2S				0				// has the TI codec on an I2S port
#define	_HAS_FPGA				0				// has an FPGA
//
#define	GPIO_SPI_HANDLE		hspi3
#define	HUART				huart1				// main uart
#define	HDEBUG				hlpuart1			// debug uart
#define	DEBUG_INST			LPUART1				// Instance of the UART
#define	GPIO_SPI_INST		SPI3				// instance of the SPI UART
//
#define	__KISS_ON_SPI		0					// Kiss packets on the SPI as well
#define	__INCLUDE_SPI		1					// nucleo can have SPI
// transceiver types
#define	__XCVR_WL33			1					// has a WL33
#define	__XCVR_AT86			0					// has an AT86RF215
#define	__XCVR_OFDM_AB		0					// has MODE B
#define	N_XCVRS				(__XCVR_WL33+__XCVR_AT86+__XCVR_OFDM_AB)	// number of tranceivers
#endif
//
#ifdef __ZUMSPOT_USB
extern UART_HandleTypeDef huart1;
// UAR/T Configuration
#define	__ENABLE_GPS			0				// board does not have GPS receiver
#define	__INCLUDE_KISS			1				// include kiss mode code
#define	ENABLE_KISS_ON_DEBUG	0				// 0: use main uart, 1: DEBUG UART
//
#define	HUART				huart1				// main uart
#define	__KISS_ON_SPI		0					// Kiss packets on the SPI as well
#define	__INCLUDE_SPI		0					// nucleo can have SPI
// transceiver types
#define	__XCVR_WL33			1					// has a WL33
#define	__XCVR_AT86			0					// has an AT86RF215
#define	__XCVR_OFDM_B		0					// has MODE B
#define	__XCVR_OFDM_C		0					// has mode C
#define	N_XCVRS				(__XCVR_WL33+__XCVR_AT86+__XCVR_OFDM_B+__XCVR_OFDM_C)	// number of tranceivers
#endif
//

#endif /* INC_CONFIGWL33_H_ */




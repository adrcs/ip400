/*---------------------------------------------------------------------------
	Project:	      OFDM TNC and transceivers

	File Name:	      config.h

	Author:		      MartinA

	Creation Date:	  Jan 27, 2025

	Description:	  Configuration parameters for supernode and power node II

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_

#include "main.h"

// Baud rate for host communication.
// Baud rate for serial debugging.
#define DEBUGGING_SPEED	38400
#define SERIAL_SPEED	115200

#ifdef __SUPERNODE_F722
#ifndef STM32F7XX
#define	STM32F7XX
#endif
// peripheral handles
extern I2C_HandleTypeDef hi2c1;
extern QSPI_HandleTypeDef hqspi;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;
// UAR/T Configuration
#define	__ENABLE_GPS			0				// GPS receiver not installed
#define	__INCLUDE_KISS			1				// include kiss mode code
#define	ENABLE_KISS_ON_DEBUG	0				// 0: use main uart, 1: DEBUG UART
// Codec Configuration
extern I2C_HandleTypeDef 		hi2c1;			// codec control interface
#define	TLV320_I2C_HANDLE		hi2c1			// I2c for tlv320
#define	CODEC_SAMPLE_RATE		24000			// codec sample rate
//
#define _HAS_CODEC				1				// has a codec
#define	_HAS_INT_RADIO			0				// has an internal radio
#define	_HAS_I2S				0				// has the TI codec on an I2S port
#define	_HAS_FPGA				1				// has an FPGA
#if	_HAS_I2S
extern I2S_HandleTypeDef 		hi2s1;			// codec I2S for transmit
extern I2S_HandleTypeDef 		hi2s2;			// codec I2S for receive
#define	TLV320_I2S_TX			hi2s1			// I2S for transmit
#define	TLV320_I2S_RX			hi2s2			// I2S for receive
#endif
#if _HAS_FPGA
#define	CODEC_SPI				hspi1			// SPI for CODEC
#define	FPGA_FLASH_HANDLE		hqspi			// SPI for programming EEPROM
#define	N_PHY_CHANNELS			1				// number of physical DMA channels
#endif
//
#define	SI5351_I2C_HANDLE	hi2c1				// i2s for SI5351
#define	TLV320_I2C_HANDLE	hi2c1				// I2s for tlv320
#define	FPGA_SPI_HANDLE		hspi1				// SPI for FPGA
#define	GPIO_SPI_HANDLE		hspi2				// SPI for Pi
#define	MODEM_SPI_HANDLE	hspi3				// modem SPI handle
#define	FPGA_FLASH_HANDLE	hqspi				// SPI for programming EEPROM
#define	HUART				huart1				// main uart
#define	HDEBUG				huart2				// debug uart
#if _HAS_INT_RADIO
#define	HRADIO				huart3				// radio uart
#endif
//
#define	DEBUG_INST			USART2				// Instance of the UART
#define	GPIO_SPI_INST		SPI1				// instance of the SPI UART
#define	FPGA_INST			SPI2				// instance of the FPGA SPI
#define	ENABLE_DEBUG_UART	1					// debug uart enabled
#define	__INCLUDE_SPI		1					// include SPI on this version
#define	__KISS_ON_SPI		0					// Kiss packets on the SPI as well
// transceiver types
#define	__XCVR_WL33			0					// has a WL33
#define	__XCVR_AT86			0					// has an AT86RF215
#define	__XCVR_OFDM_AB		1					// has MODE B
#define	N_XCVRS				(__XCVR_WL33+__XCVR_AT86+__XCVR_OFDM_AB)	// number of tranceivers
#endif
/*
 * Power Node II is similar to T20 add_on
 */
#ifdef __POWER_NODE_II
 #ifndef STM32F7XX
  #define	STM32F7XX
 #endif
// peripheral handles
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern I2C_HandleTypeDef hi2c1;

// UAR/T Configuration
#define	__ENABLE_GPS			0				// board does not have GPS receiver
#define	__INCLUDE_KISS			1				// include kiss mode code
#define	ENABLE_KISS_ON_DEBUG	0				// 0: use main uart, 1: DEBUG UART
// Codec Configuration
extern I2C_HandleTypeDef 		hi2c1;			// SI5351 control interface
#if __T20_ADD_ON
extern I2C_HandleTypeDef 		hi2c2;			// codec control interface
#define	SI5351_I2C_HANDLE		hi2c1			// i2s for SI5351
#define	TLV320_I2C_HANDLE		hi2c2			// I2s for tlv320
#else
#define	SI5351_I2C_HANDLE		hi2c1			// i2s for SI5351
#define	TLV320_I2C_HANDLE		hi2c1			// I2s for tlv320
#endif
#define	CODEC_SAMPLE_RATE		24000			// codec sample rate
//
#define _HAS_CODEC				1				// has a codec
#define	_HAS_INT_RADIO			1				// has an internal radio
#define	_HAS_I2S				1				// has the TI codec on an I2S port
#define	_HAS_FPGA				0				// has an FPGA
#if	_HAS_I2S
extern I2S_HandleTypeDef 		hi2s1;			// codec I2S for transmit
extern I2S_HandleTypeDef 		hi2s2;			// codec I2S for receive
#define	TLV320_I2S_TX			hi2s1			// I2S for transmit
#define	TLV320_I2S_RX			hi2s2			// I2S for receive
#define	N_PHY_CHANNELS			2				// number of physical DMA channels
#endif
#if _HAS_FPGA
#define	CODEC_SPI				hspi1			// SPI for CODEC
#define	FPGA_FLASH_HANDLE		hqspi			// SPI for programming EEPROM
#ifndef __INCLUDE_SPI							// must include SPI
#define	__INCLUDE_SPI			1				// no SPI on this version
#endif
#endif
// SPI Inclusion
#ifndef __INCLUDE_SPI							// must include SPI
#define	__INCLUDE_SPI			0				// no SPI on this version
#endif
//
#define	HUART				huart1				// main uart
#define	HDEBUG				huart2				// debug uart
#if _HAS_INT_RADIO
#define	HRADIO				huart3				// radio uart
#endif
#define	GPIO_SPI_INST		SPI1				// instance of the SPI UART
#define	DEBUG_INST			USART2				// Instance of the UART
#define	ENABLE_DEBUG_UART	1					// debug uart enabled
#define	__KISS_ON_SPI		0					// Kiss packets on the SPI as well

// transceiver types
#define	__XCVR_WL33			0					// has a WL33
#define	__XCVR_AT86			0					// has an AT86RF215
#define	__XCVR_OFDM_AB		1					// has OFDM mode
#define	N_XCVRS				(__XCVR_WL33+__XCVR_AT86+__XCVR_OFDM_AB)	// number of tranceivers
#endif
/*
 * Backwards compatibility is in another file...
 */
#if defined(__NUCLEOCC2) || defined(__PI_BOARD)
#include "configwl33.h"
#endif
//
// DO NOT modify these...
//
#if __ENABLE_GPS
	#if __INCLUDE_KISS && ENABLE_KISS_ON_DEBUG
		#error	"GPS and KISS mode are mutually exclusive on second UART"
	#endif
	#if __INCLUDE_KISS && !ENABLE_KISS_ON_DEBUG
		#define	ENABLE_DEBUG_UART		1		// enable debug uart
		#define	ENABLE_GPS_DEBUG		1		// connect debug UART to GPS
		#define	__AX25_COMPATIBILITY	1		// enable AX25 mode
	#endif
#else	// GPS not enabled: KISS mode can be on the debug UART
	#if __INCLUDE_KISS
		#if	ENABLE_KISS_ON_DEBUG					// do not modify these...
			#define	KISS_ON_DEBUG			1		// kiss is on LPUART
			#define	ENABLE_DEBUG_UART		1		// enable lpuart for KISS mode
		#endif
		#define	__AX25_COMPATIBILITY		1		// enable AX25 mode
	#endif
#endif
//



#endif /* INC_CONFIG_H_ */




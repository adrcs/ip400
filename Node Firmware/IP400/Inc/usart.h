/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      usart.h

	Author:		      MartinA

	Creation Date:	  Jan 12, 2025

	Description:	  <decription here?

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/

#ifndef INC_USART_H_
#define INC_USART_H_

#include "types.h"
#include "main.h"

// GPS and KISS mode need LPUART; so does the telemetry board
#if (__ENABLEGPS == 1) || (__INCLUDE_KISS == 1) || (_BOARD_TYPE	== TELEM_BOARD)		// board type in use
#define ENABLE_LPUART	1
#else
#define ENABLE_LPUART	0
#endif

// definitions
typedef	uint32_t		UART_TIMEOUT_T;		// uart timer type
typedef uint16_t		BUFFER_SIZE_T;		// buffer size type
typedef uint8_t			DATA_ELEMENT;		// buffer data element

// uart HAL handle
extern UART_HandleTypeDef huart1;			// console UART
extern UART_HandleTypeDef hlpuart1 ;		// GPS UART

// logger defines
#define	LOG_NOTICE		0			// notice
#define LOG_ERROR		1			// error
#define	LOG_SEVERE		2			// severe error

// logger
void logger(int severity, char *format, ...);

// API calls
void USART_RxBuffer_reset(void);
size_t databuffer_bytesInBuffer(void);
DATA_ELEMENT databuffer_get(UART_TIMEOUT_T timeout);
BOOL databuffer_contains(const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer);
BOOL USART_Send_String(const char *string, size_t len);
BOOL USART_Send_Char(const char c);
void USART_Print_string(char *format, ...);

// same as USART but for LPUART
void LPUART_RxBuffer_reset(void);
void LPUART_Send_String(char *str, uint16_t len);

size_t lpuart_bytesInBuffer(void);
DATA_ELEMENT lpuart_buffer_get(UART_TIMEOUT_T timeout);

#if ENABLE_LPUART && __ENABLE_GPS
#define gpsbuffer_bytesInBuffer lpuart_bytesInBuffer
#define gpsbuffer_get lpuart_buffer_get
#endif


#endif /* INC_USART_H_ */

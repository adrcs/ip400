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

#define	LPUART		DBUART					// LPUART is handled by debug uart

// definitions
typedef	uint32_t		UART_TIMEOUT_T;		// uart timer type
typedef uint16_t		BUFFER_SIZE_T;		// buffer size type
typedef uint8_t			BYTE_BUFFER;		// a raw data buffer


// uart HAL handle
extern UART_HandleTypeDef huart1;			// console UART
extern UART_HandleTypeDef hlpuart1 ;		// GPS UART

// data buffer defs
typedef int16_t			USART_ELEMENT;		// usart data element
#define                 BUFFER_EMPTY(x)		(x.nDataBytes == 0)
#define                 BUFFER_NO_DATA		-1

// DEC VT100 Escape sequences
#define ASCII_TAB		0x09			// tab character
#define ASCII_RET		0x0D			// return character
#define	ASCII_ESC		0x1B			// escape character

#define	DEC_LEADIN		'['				// lead-in character
#define	MAX_SEQ			6				// max sequence length

// API calls
void USART_RxBuffer_reset(void);
size_t USART_databuffer_bytesInBuffer(void);
USART_ELEMENT USART_databuffer_get(UART_TIMEOUT_T timeout);
BOOL USART_databuffer_contains(const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer);
BOOL USART_Send_String(const char *string, size_t len);
BOOL USART_Send_Char(const char c);
void USART_Print_string(char *format, ...);

// same as USART but for DBUART (aka LPUART)
void DBUART_RxBuffer_reset(void);
size_t DBUART_bytesInBuffer(void);
USART_ELEMENT DBUART_buffer_get(UART_TIMEOUT_T timeout);
BOOL DBUART_bufferr_contains(const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer);
void DBUART_Send_String(char *str, uint16_t len);
BOOL DBUART_Send_Char(const char c);
void DBUART_Print_string(char *format, ...);
//
// Once more for USB
void USBUART_RxBuffer_reset(void);
size_t USBUART_bytesInBuffer(void);
USART_ELEMENT USBUART_buffer_get(UART_TIMEOUT_T timeout);
BOOL USBUART_bufferr_contains(const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer);
void USBUART_Send_String(char *str, uint16_t len);
BOOL USBUART_Send_Char(const char c);
void USBUART_Print_string(char *format, ...);

#if ENABLE_LPUART && __ENABLE_GPS
#define gpsbuffer_bytesInBuffer DBUART__bytesInBuffer
#define gpsbuffer_get DBUART_buffer_get
#endif

#endif /* INC_USART_H_ */

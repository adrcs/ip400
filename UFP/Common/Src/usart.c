/*---------------------------------------------------------------------------
	Project:	      IP400 Modem

	File Name:	    usart.c

	Author:		    Martin C. Alcock, VE6VH

	Revision:	    1.05

	Description:	API for USART handling

	Revision History:

---------------------------------------------------------------------------*/
//---------------------------------------------------------------------------
//!
//!	\file  	usart.c
//!
//! \brief 	USART Handler and API
//!
//!	\author Martin C. Alcock, VE6VH
//!
//!	\par 	Revision History:
//!			<2026-05-26> Complete re-write to use DMA
//!
//! \copyright
//!
//! 	Copyright (c) Alberta Digital Radio Communications Society
//! 	All rights reserved.
//!
//! 	This program is free software: you can redistribute it and/or modify
//! 	it under the terms of the GNU General Public License as published by
//! 	the Free Software Foundation, either version 2 of the License, or
//! 	(at your option) any later version, provided this copyright notice
//! 	is included.
//!
//!
//---------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <cmsis_os2.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <config.h>

#include "stream_buffer.h"
#include "types.h"
#include "usart.h"
#include "frame.h"

// local defines
#define RX_TIMEOUT      10000       // 10 second rx timeout
#define	TX_TIMEOUT		1000		// 1s transmit timeout

#ifdef STM32F7XX
#define	UART_RX_BUF_SIZE	2048			// uart buffer size
#define	UART_RX_DMA_SIZE	1024			// rx DMA size
#define	UART_TX_BUF_SIZE	512				// transmit buffer size
#else
#define	UART_RX_BUF_SIZE	1024			// uart buffer size
#define	UART_RX_DMA_SIZE	256				// rx DMA size
#define	UART_TX_BUF_SIZE	200				// transmit buffer size
#endif

// USART device control block
/**
 * @struct usart_dcb_t
 * @brief Device Control Block data structure for UAR/T
 *
 */
typedef struct	usart_dcb_t {
	UART_HandleTypeDef		*huart;				                                		/*! HAL UART handle */
	USART_TypeDef			*typeDef;													/*! Uar/t typedef */
	StreamBufferHandle_t	RxBuffer;			                               			/*! handle to stream buffer buffer */
	StaticStreamBuffer_t   	StreamBuffer;		                               			/*! Stream buffer data storage */
	BYTE_BUFFER				rxData[UART_RX_BUF_SIZE];									/*! data portion of Stream buffer */
	BYTE_BUFFER 			DMABUffer[UART_RX_DMA_SIZE] __attribute__((aligned(16))); 	/*! DMA buffer */
	SemaphoreHandle_t 		txCompleted;												/*! Semapphore for transmit completed */
	char					txBuffer[UART_TX_BUF_SIZE];									/*! transmit buffer for printf()	*/
	uint16_t				rdPtr;														/*! DMA buffer read pointer */
} USART_DCB;

// DCB for main UART
USART_DCB	main_dcb = {
		.huart = &HUART,
		.typeDef = USART1,
		.rdPtr = 0
};
// DCB for debug uart
#if ENABLE_DEBUG_UART
USART_DCB	debug_dcb = {
		.huart = &HDEBUG,
		.typeDef = USART2,
		.rdPtr = 0
};
#endif
/*
 * Fwd ref's
 */
// main API calls
size_t dcb_databuffer_bytesInBuffer(USART_DCB *dcb);
USART_ELEMENT dcb_databuffer_get(USART_DCB *dcb, UART_TIMEOUT_T timeout);
BOOL dcb_databuffer_contains(USART_DCB *dcb, const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer);
BOOL dcb_Send_String(USART_DCB *dcb, const char *string, size_t len);
BOOL dcb_Send_Char(USART_DCB *dcb, const char c);

/* ==================================================================
 * USART task initialization
   =================================================================*/
void USART_API_init(void)
{
	// main uart first
    main_dcb.txCompleted = xSemaphoreCreateBinary();
	main_dcb.RxBuffer = xStreamBufferCreateStatic(UART_RX_BUF_SIZE, 1, main_dcb.rxData, &main_dcb.StreamBuffer);
	USART_RxBuffer_reset();
	HAL_UARTEx_ReceiveToIdle_DMA(main_dcb.huart,&main_dcb.DMABUffer[0], UART_RX_DMA_SIZE);

#if ENABLE_DEBUG_UART
    debug_dcb.txCompleted = xSemaphoreCreateBinary();
	debug_dcb.RxBuffer = xStreamBufferCreateStatic(UART_RX_BUF_SIZE, 1, debug_dcb.rxData, &debug_dcb.StreamBuffer);
	DBUART_RxBuffer_reset();
	HAL_UARTEx_ReceiveToIdle_DMA(debug_dcb.huart, &debug_dcb.DMABUffer[0], UART_RX_DMA_SIZE);
#endif
}

/* ==================================================================
 * API interface calls
   =================================================================*/
//
// Main UART
//
/**
 * @fn void USART_RxBuffer_reset(void)
 * @brief Clears the main uart stream buffer and resets contents to zero
 *
 */
void USART_RxBuffer_reset(void)
{
	xStreamBufferReset(main_dcb.RxBuffer);
}
/**
 * @fn size_t USART_databuffer_bytesInBuffer(void)
 * @brief Returns a count of the number of bytes available in the main uart stream buffer
 *
 * @return a size_t integer
 */
size_t USART_databuffer_bytesInBuffer(void)
{
	return dcb_databuffer_bytesInBuffer(&main_dcb);
}
/**
 * @fn USART_ELEMENT USART_databuffer_get(UART_TIMEOUT_T)
 * @brief Gets a data element from the recieve buffer
 *
 * @param timeout a timeout value in ms, or zero to wait indefinately
 * @return data as a USART_ELEMENT, or BUFFER_NO_DATA on timeout
 */
USART_ELEMENT USART_databuffer_get(UART_TIMEOUT_T timeout)
{
	return dcb_databuffer_get(&main_dcb, timeout);
}
/**
 * @fn BOOL USART_databuffer_contains(const char*, UART_TIMEOUT_T, BOOL, char*)
 * @brief See if the data buffer contains a certain tag
 *
 * @param tag			the tag to look for in the buffer
 * @param rx_timeout	receive timeout in MS or zero for no timeout
 * @param saveData		TRUE to save data from the current buffer pointer to where the tag was found; FALSE otherwise
 * @param SaveBuffer	Address of buffer to save data (null terminated)
 * @return				TRUE if found; FALSE if not or timeout
 */
BOOL USART_databuffer_contains(const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer)
{
	return dcb_databuffer_contains(&main_dcb, tag, rx_timeout, saveData, SaveBuffer);
}
/**
 * @fn BOOL USART_Send_String(const char*, size_t)
 * @brief Send a character string out from the UART using DMA
 *
 * @param string	address of character string
 * @param len		length of the string
 * @return			TRUE if no error occurred; FALSE otherwise
 */
BOOL USART_Send_String(const char *string, size_t len)
{
	return dcb_Send_String(&main_dcb, string, len);
}
/**
 * @fn BOOL USART_Send_Char(const char)
 * @brief	Send out a single character
 *
 * @param c	character to send
 * @return	TRUE if no error occurred; FALSE otherwise
 */
BOOL USART_Send_Char(const char c)
{
	return dcb_Send_Char(&main_dcb, c);
}
/**
 * @fn void USART_Print_string(char*, ...)
 * @brief Formats a string into the DCB transmit buffer and sends it out
 *
 * @param format printf-style string
 */
void USART_Print_string(char *format, ...)
{
	// process the arg list
    va_list argptr;
    va_start(argptr, format);
    vsprintf(main_dcb.txBuffer,format, argptr);
    va_end(argptr);

	USART_Send_String(main_dcb.txBuffer, strlen(main_dcb.txBuffer));
}

//
// repeated for debug uart
//
#if ENABLE_DEBUG_UART
void DBUART_RxBuffer_reset(void)
{
	xStreamBufferReset(debug_dcb.RxBuffer);
}
//
size_t DBUART_bytesInBuffer(void)
{
	return dcb_databuffer_bytesInBuffer(&debug_dcb);
}
USART_ELEMENT DBUART_buffer_get(UART_TIMEOUT_T timeout)
{
	return dcb_databuffer_get(&debug_dcb, timeout);
}
BOOL DBUART_buffer_contains(const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer)
{
	return dcb_databuffer_contains(&debug_dcb, tag, rx_timeout, saveData, SaveBuffer);
}
//
void DBUART_Send_String(char *str, uint16_t len)
{
	dcb_Send_String(&debug_dcb, str, len);
}
//
BOOL DBUART_Send_Char(const char c)
{
	return dcb_Send_Char(&debug_dcb, c);
}
void DBUART_Print_string(char *format, ...)
{
	// process the arg list
    va_list argptr;
    va_start(argptr, format);
    vsprintf(debug_dcb.txBuffer,format, argptr);
    va_end(argptr);

	DBUART_Send_String(debug_dcb.txBuffer, strlen(debug_dcb.txBuffer));
}
#endif

/* ==================================================================
 * Implementers for USART calls
   =================================================================*/
// return the number of byte in the buffer
size_t dcb_databuffer_bytesInBuffer(USART_DCB *dcb)
{
	size_t nBytes = xStreamBufferBytesAvailable(dcb->RxBuffer);
	return nBytes;
}
//
USART_ELEMENT dcb_databuffer_get(USART_DCB *dcb, UART_TIMEOUT_T timeout)
{
	USART_ELEMENT retval;

	TickType_t tickTimeout;

	if(timeout == 0)
		tickTimeout = portMAX_DELAY;
	else
		tickTimeout = pdMS_TO_TICKS(timeout);

	if(xStreamBufferReceive(dcb->RxBuffer, &retval, 1, tickTimeout) == 0)
		return BUFFER_NO_DATA;

	return retval;
}
//
BOOL dcb_databuffer_contains(USART_DCB *dcb, const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer)
{
	USART_ELEMENT c;

	uint8_t tagSize = (uint8_t)(strlen(tag) & 0xff);
	uint8_t tagLen = tagSize;

	const char *tagAddr = tag;

	while ((c = dcb_databuffer_get(dcb,rx_timeout)) != BUFFER_NO_DATA)	{
		if (c == *tagAddr) {
			if (--tagLen == 0)	{
				if(saveData)	{
					*SaveBuffer++ = c;
					*SaveBuffer = '\0';
				}
				return TRUE;
			}
			tagAddr++;
		}
		else {
			tagAddr = tag;
			tagLen = tagSize;
		}
		if(saveData)
			*SaveBuffer++ = c;
	}
	if(saveData)
		*SaveBuffer = '\0';
	return FALSE;

}
//
BOOL dcb_Send_String(USART_DCB *dcb, const char *string, size_t len)
{
	// send using DMA
	uint16_t dataLen = (uint16_t)len;
	if((HAL_UART_Transmit_DMA(dcb->huart, (const uint8_t *)string, dataLen)) != HAL_OK)
		return FALSE;

	// wait for completion..
	if((xSemaphoreTake(dcb->txCompleted, pdMS_TO_TICKS(TX_TIMEOUT))) == pdTRUE)
			return TRUE;

	return FALSE;
}
BOOL dcb_Send_Char(USART_DCB *dcb, const char c)
{
	// send using interrupt
	HAL_UART_Transmit_IT(dcb->huart, (const uint8_t *)&c, 1);

	// wait for completion..
	if((xSemaphoreTake(dcb->txCompleted, pdMS_TO_TICKS(TX_TIMEOUT))) == pdTRUE)
			return TRUE;

	return FALSE;
}

/* ==================================================================
 * HAL interface callbacks
   =================================================================*/
// Transmit completed: trigger semaphore
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	BaseType_t  xHigherPriorityTaskWoken = pdFALSE;

#if ENABLE_DEBUG_UART
	if(huart->Instance == debug_dcb.typeDef)		{
		xSemaphoreGiveFromISR(debug_dcb.txCompleted, &xHigherPriorityTaskWoken);
		return;
	} else
#endif
	if(huart->Instance == main_dcb.typeDef)		{
		xSemaphoreGiveFromISR(main_dcb.txCompleted, &xHigherPriorityTaskWoken);
		return;
	}
}
// Receive complete: copy data from DMA buffer to streambuffer
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	USART_DCB *dcb_triggered = &main_dcb;			// DCB that triggered the interrupt
	uint16_t nBytesTransferred=0;

#if ENABLE_DEBUG_UART
	if(huart->Instance == debug_dcb.typeDef)
		dcb_triggered = &debug_dcb;
#endif
	// transfer data to the Stream buffer
	nBytesTransferred = Size-dcb_triggered->rdPtr;
	if(nBytesTransferred)
		xStreamBufferSendFromISR(dcb_triggered->RxBuffer, &dcb_triggered->DMABUffer[dcb_triggered->rdPtr], nBytesTransferred, NULL);
	dcb_triggered->rdPtr = (Size == UART_RX_DMA_SIZE) ? 0 : Size;
}

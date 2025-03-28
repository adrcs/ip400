/*---------------------------------------------------------------------------
	Project:	      IP400 Modem

	File Name:	    usart.c

	Author:		    Martin C. Alcock, VE6VH

	Revision:	    1.05

	Description:	API for USART handling

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History:

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <cmsis_os2.h>
#include <FreeRTOS.h>
#include <semphr.h>

#include "config.h"
#include "stream_buffer.h"
#include "types.h"
#include "streambuffer.h"
#include "usart.h"

// local defines
#define RX_TIMEOUT      10000       // 10 second rx timeout
#define	TX_TIMEOUT		1000		// 1s transmit timeout

// stream buffer
StreamBufferHandle_t	USART_RxBuffer;			// handle to buffer
StaticStreamBuffer_t   	USART_StreamBuffer;

// UART/T Support
SemaphoreHandle_t 	txCompleted;		// tx completed semaphore
static DATA_ELEMENT USARTRxChar[10];	// last received character

uint8_t usart_data[bufferSIZE];
char usartPrintBuffer[200];

// fwd refs here...
void USART_Receive_char(void);

#if __ENABLE_GPS
#define	LPUART_DMA_SIZE			32
static DATA_ELEMENT LPUARTRxChar[LPUART_DMA_SIZE];

void LPUART_Receive_char(void);

StreamBufferHandle_t	LPUART_RxBuffer;			// handle to buffer
StaticStreamBuffer_t   	LPUART_StreamBuffer;
uint8_t gps_data[bufferSIZE];
#endif

/*
 * API for the rx data buffer
 */
void USART_API_init(void)
{
	// create the tx completed semaphore
    txCompleted = xSemaphoreCreateBinary();

    // start USART
	USART_RxBuffer = xStreamBufferCreateStatic(bufferSIZE, 1, usart_data, &USART_StreamBuffer);
	USART_RxBuffer_reset();
    USART_Receive_char();

#if __ENABLE_GPS
	LPUART_RxBuffer = xStreamBufferCreateStatic(bufferSIZE, 1, gps_data, &LPUART_StreamBuffer);
	LPUART_RxBuffer_reset();
    LPUART_Receive_char();
#endif
}

// reset the data buffer
void USART_RxBuffer_reset(void)
{
	xStreamBufferReset(USART_RxBuffer);
	return;
}

#if __ENABLE_GPS
// reset the LPUART Buffer
void LPUART_RxBuffer_reset(void)
{
	xStreamBufferReset(LPUART_RxBuffer);
	return;
}
#endif

// return the number of byte in the buffer
size_t databuffer_bytesInBuffer(void)
{
	size_t nBytes = xStreamBufferBytesAvailable(USART_RxBuffer);
	return nBytes;
}

// get a byte from the buffer: blocks if timeout is zero,
// else returns BUFFER_NO_DATA if timeout exceeded
DATA_ELEMENT databuffer_get(UART_TIMEOUT_T timeout)
{
	DATA_ELEMENT retval;

	TickType_t tickTimeout;

	if(timeout == 0)
		tickTimeout = portMAX_DELAY;
	else
		tickTimeout = pdMS_TO_TICKS(timeout);

	if(xStreamBufferReceive(USART_RxBuffer, &retval, 1, tickTimeout) == 0)
		return BUFFER_NO_DATA;

	return retval;
}

/*
 * Similar but from GPS buffer
 */
#if __ENABLE_GPS
// return the number of byte in the buffer
size_t gpsbuffer_bytesInBuffer(void)
{
	size_t nBytes = xStreamBufferBytesAvailable(LPUART_RxBuffer);
	return nBytes;
}
// get a bytes
DATA_ELEMENT gpsbuffer_get(UART_TIMEOUT_T timeout)
{
	DATA_ELEMENT retval;

	TickType_t tickTimeout;

	if(timeout == 0)
		tickTimeout = portMAX_DELAY;
	else
		tickTimeout = pdMS_TO_TICKS(timeout);

	if(xStreamBufferReceive(LPUART_RxBuffer, &retval, 1, tickTimeout) == 0)
		return BUFFER_NO_DATA;

	return retval;
}
#endif

// see if the buffer contains a keyword, save data up to it if needed
BOOL databuffer_contains(const char *tag, UART_TIMEOUT_T rx_timeout, BOOL saveData, char *SaveBuffer)
{
	DATA_ELEMENT c;

	uint8_t tagSize = (uint8_t)(strlen(tag) & 0xff);
	uint8_t tagLen = tagSize;

	const char *tagAddr = tag;

	while ((c = databuffer_get(rx_timeout)) != BUFFER_NO_DATA)	{
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

/*
 * Low level HAL interaction functions
 */

// send a packet (with timeout)
BOOL USART_Send_String(const char *string, size_t len)
{
	// send using DMA
	uint16_t dataLen = (uint16_t)len;
	if((HAL_UART_Transmit_DMA(&huart1, (const uint8_t *)string, dataLen)) != HAL_OK)
		return FALSE;

	// wait for completion..
	if((xSemaphoreTake(txCompleted, pdMS_TO_TICKS(TX_TIMEOUT))) == pdTRUE)
			return TRUE;

	return FALSE;
}

// send a packet (with timeout)
BOOL USART_Send_Char(const char c)
{

	// send using interrupt
	HAL_UART_Transmit_IT(&huart1, (const uint8_t *)&c, 1);

	// wait for completion..
	if((xSemaphoreTake(txCompleted, pdMS_TO_TICKS(TX_TIMEOUT))) == pdTRUE)
			return TRUE;

	return FALSE;
}

// print a string to the UAR/T
void USART_Print_string(char *format, ...)
{
	// process the arg list
    va_list argptr;
    va_start(argptr, format);
    vsprintf(usartPrintBuffer,format, argptr);
    va_end(argptr);

	USART_Send_String(usartPrintBuffer, strlen(usartPrintBuffer));
}


// Transmit completed: trigger semaphore
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
#if __ENABLE_GPS
	// Not interested in LPUART
	if(huart->Instance == LPUART1)
		return;
#endif
	BaseType_t  xHigherPriorityTaskWoken = pdFALSE;
	xSemaphoreGiveFromISR(txCompleted, &xHigherPriorityTaskWoken);

}

// receive a byte with DMA, wait for DMA completed interrupt
void USART_Receive_char(void)
{
	HAL_UART_Receive_IT(&huart1,(uint8_t *)USARTRxChar,1);
}


// callback function when rx is completed: overrides previous
// __weak definition
// NB: Console USART and GPS LPUART share the same HAL routine
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if __ENABLE_GPS
	// service the LPUART
	if(huart->Instance == LPUART1)	{
		xStreamBufferSendFromISR(LPUART_RxBuffer, LPUARTRxChar, 1, NULL);
		HAL_UART_Receive_IT(&hlpuart1,(uint8_t *)LPUARTRxChar,1);
		return;
	}
#endif
	// service the USART
	xStreamBufferSendFromISR(USART_RxBuffer, USARTRxChar, 1, NULL);
	HAL_UART_Receive_IT(&huart1,(uint8_t *)USARTRxChar,1);
}

#if __ENABLE_GPS
// send a string to the LPUART
void LPUART_Send_String(char *str, uint16_t len)
{
	// send using interrupt
	HAL_UART_Transmit_IT(&hlpuart1, (const uint8_t *)str, len);
}


// receive a byte with DMA, wait for DMA completed interrupt
void LPUART_Receive_char(void)
{
	HAL_UART_Receive_IT(&hlpuart1,(uint8_t *)LPUARTRxChar,1);
	// HAL_UARTEx_ReceiveToIdle_DMA(&hlpuart1,(uint8_t *)LPUARTRxChar,LPUART_DMA_SIZE);
	//__HAL_DMA_DISABLE_IT(&hlpuart1, DMA_IT_HT);
}


// callback from GPS Rx
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	xStreamBufferSendFromISR(LPUART_RxBuffer, LPUARTRxChar, Size, NULL);
	HAL_UARTEx_ReceiveToIdle_DMA(&hlpuart1,(uint8_t *)LPUARTRxChar,LPUART_DMA_SIZE);
}

#endif

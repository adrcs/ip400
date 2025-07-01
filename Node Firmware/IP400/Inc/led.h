/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      led.h

	Author:		      MartinA

	Creation Date:	  Jan 26, 2025

	Description:	  Definitions for the LED module

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_LED_H_
#define INC_LED_H_

#include <config.h>

// led command modes
typedef enum {
	LED_CMD_RF,					// set RF indication mode
	LED_CMD_TELEM				// set telemetry mode
} LEDCommand;

// 	LED functions
enum {

      BICOLOR_OFF=0,           // turn LED off
      BICOLOR_RED,             // red mode steady
      BICOLOR_RED_FLASH,   	   // red mode flashing
      BICOLOR_GREEN,       	   // green mode steady
      BICOLOR_GREEN_FLASH, 	   // green mode flashing
      BICOLOR_RED_GREEN,   	   // alternate RED/GREEN
	  TX_LED_ON,			   // tx LED on
	  TX_LED_OFF,			   // tx LED OFF
      N_LED_MODE			   // modes1
};

#if (_BOARD_TYPE == PI_BOARD) || (_BOARD_TYPE == IP400_MODULE)
// bidirectional LED
#define LED_Green_Pin 		GPIO_PIN_0
#define LED_Green_GPIO_Port GPIOB
#define LED_Red_Pin 		GPIO_PIN_9
#define LED_Red_GPIO_Port 	GPIOA

// Tx LED
#define	TXLED_Pin			GPIO_PIN_15
#define	TXLED_GPIO_Port		GPIOB

// PA enable pin
#define	PA_ENA_Pin			GPIO_PIN_0
#define	PA_ENA_GPIO_Port	GPIOA
#endif

#if (_BOARD_TYPE == PI_BOARD) || (_BOARD_TYPE == IP400_MODULE) || (_BOARD_TYPE == NUCLEO_BOARD)
// Set Led Mode
void SetLEDState(uint8_t mode);
#define  SetLEDMode SetLEDState
#endif

#if _BOARD_TYPE == TELEM_BOARD
// define the LED ports
#define LED_Green_Pin 		GPIO_PIN_14
#define LED_Green_GPIO_Port GPIOB
#define LED_Red_Pin 		GPIO_PIN_15
#define LED_Red_GPIO_Port 	GPIOB

// Tx LED
#define	TXLED_Pin			GPIO_PIN_0
#define	TXLED_GPIO_Port		GPIOA

// PA enable pin
#define	PA_ENA_Pin			GPIO_PIN_7
#define	PA_ENA_GPIO_Port	GPIOA

// set LED mode
void SetCommLEDMode(LEDCommand cmd, uint8_t mode);
void SetLEDState(uint8_t mode);
uint8_t GetLEDMode(void);
void SetConnMode(BOOL mode);
#define SetLEDMode(c) SetCommLEDMode(LED_CMD_RF, c)
#endif

#endif /* INC_LED_H_ */

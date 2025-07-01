/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    led.c

	Author:		    MartinA

	Description:	Handler for the LED's on nucleo or PI boards

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History:

---------------------------------------------------------------------------*/
/*
 * notes for different implementations.
 * On the Pi there are two LED's a dedicated red and a bicolor red-green.
 * TX:			On when transmitting
 * BICOLOR:		Green solid when Rx is enabled
 * 				Red solid when HAL error or NMI occurred
 *
 * On the NUCLEO board, there are three, red, green and blue
 * BLUE (LD1)	Duplicates the TX LED
 * GREEN (LD2)	Duplicates the GREEN bi-color function
 * RED (LD3)	Duplicates the RED Bi-color function
 */

#include <config.h>
#include "types.h"
#include "led.h"
#include "usart.h"


// include LED defs from the right place
#if _BOARD_TYPE == NUCLEO_BOARD
#include <stm32wl3x_nucleo.h>
#endif

// local defines
#define	FLASH_OFF		0				// LED flashing: off state
#define FLASH_ON		1				// LED flashing: on state

// timer for 1/2 second when flashing
#define	LED_TIMER		5				// timer
#define	TEST_TIMER		20				// test timer

// internals
void SetLEDState(uint8_t mode);
void LED_SetOff(void);
void LED_SetRed(void);
void LED_SetGreen(void);
void setTxLED(BOOL state);

// vars
BOOL ToggleEnable;						// led toggling enabled
uint8_t ledColour; 		                // direction (colour)
uint8_t ledMode;						// led mode
uint8_t ledState;                       // state on/off
uint8_t ledTimer;						// timer for led test
uint8_t testNum;						// test number
uint8_t testTimer;						// test timer
uint8_t saveMode;						// saved mode

//
// Notes on timer setup for STM32H732
// Prescaler value of 0 allows 64MHz clock to drive timer
//                    1 reduces it to 32MHz
// Period divider of 6400 yeilds 100uSec time base
//
#define N_LED		5
struct led_tests_t {
	char *testName;
	uint8_t testMode;
} LEDTests[N_LED] = {
#if _BOARD_TYPE == NUCLEO_BOARD
		{"RED On", BICOLOR_RED },
		{"GREEN On", BICOLOR_GREEN },
		{"GREEN only", BICOLOR_OFF },
		{"BLUE On", TX_LED_ON },
		{"BLUE Off", TX_LED_OFF }
#else
		{"Bicolor RED On", BICOLOR_RED },
		{"Bicolor GREEN On", BICOLOR_GREEN },
		{"Bicolor off", BICOLOR_OFF },
		{"Tx LED On", TX_LED_ON },
		{"Tx LED Off", TX_LED_OFF }
#endif
};
// Initialization
void Led_Task_Init(void)
{
    ToggleEnable = FALSE;
    ledColour = BICOLOR_RED;
    ledState = BICOLOR_OFF;
    ledTimer = LED_TIMER;
    LED_SetOff();
    setTxLED(FALSE);
    testTimer = 0;
    testNum = 0;
}

// We handle the flashing here...
// for the bicolor led, we can be solid red, solid green,
// or red-green flashing
void Led_Task_Exec(void)
{

	if(ToggleEnable)    {

		if(ledTimer != 0)	{
			ledTimer--;
			return;
		}

		ledTimer--;

		// blink LED if enabled
		switch(ledMode)        {

		case BICOLOR_RED_FLASH:
		case BICOLOR_GREEN_FLASH:
			ledState = (ledState == FLASH_ON) ? FLASH_OFF : FLASH_ON;
			break;

		case BICOLOR_RED_GREEN:
			ledState = (ledState == FLASH_ON) ? FLASH_OFF : FLASH_ON;
			ledColour = (ledColour == BICOLOR_RED) ? BICOLOR_GREEN : BICOLOR_RED;
			break;
		}

		// update the LED state
		if(ledState == FLASH_OFF)	{
			LED_SetOff();
		} else {
			if(ledColour == BICOLOR_RED)
				LED_SetRed();
			else
				LED_SetGreen();
		}
	}
}

// led test mode
BOOL LedTest(void)
{

	if(testTimer == 0)	{
		if(testNum == 0)
			saveMode = ledMode;
		if(testNum == N_LED)	{
			testNum = 0;
			SetLEDState(saveMode);
			return TRUE;
		}

		USART_Print_string("%s\r\n", LEDTests[testNum].testName);
		SetLEDState(LEDTests[testNum].testMode);
		testTimer =  TEST_TIMER;
		testNum++;
		return FALSE;
	}

	testTimer--;
	return FALSE;
}

// API routine: get the LED mode
uint8_t GetLEDMode()
{
	return ledMode;
}

// API routines: set the LED state for nodes only
void SetLEDState(uint8_t mode)
{
	ledMode = mode;

    switch(ledMode)    {

        case BICOLOR_OFF:
            LED_SetOff();
            ToggleEnable = FALSE;
            ledState = FLASH_OFF;
            break;

        case BICOLOR_RED:
            LED_SetRed();
            ToggleEnable = FALSE;
            ledColour = BICOLOR_RED;
            ledState = FLASH_ON;
            break;

        case BICOLOR_RED_FLASH:
            LED_SetRed();
            ToggleEnable = TRUE;
            ledColour = BICOLOR_RED;
            ledState = FLASH_ON;
            break;

        case BICOLOR_GREEN:
            LED_SetGreen();
            ToggleEnable = FALSE;
            ledColour = BICOLOR_GREEN;
            ledState = FLASH_ON;
            break;

        case BICOLOR_GREEN_FLASH:
            LED_SetGreen();
            ToggleEnable = TRUE;
            ledColour = BICOLOR_GREEN;
            ledState = FLASH_ON;
            break;

        case BICOLOR_RED_GREEN:
        	LED_SetRed();
            ToggleEnable = TRUE;
            ledColour = BICOLOR_RED;
            ledState = FLASH_ON;
            break;

        case TX_LED_ON:
        	setTxLED(TRUE);
        	break;

        case TX_LED_OFF:
        	setTxLED(FALSE);
        	break;

    }
}

// do the actual updates
void LED_SetOff(void)
{
#if _BOARD_TYPE == NUCLEO_BOARD
	BSP_LED_Off(LED_RED);
	BSP_LED_Off(LED_GREEN);
	BSP_LED_Off(LED_BLUE);
#else
    HAL_GPIO_WritePin(TXLED_GPIO_Port, TXLED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_Green_GPIO_Port, LED_Green_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_Red_GPIO_Port, LED_Red_Pin, GPIO_PIN_RESET);
#endif
}

void setTxLED(BOOL state)
{
#if _BOARD_TYPE == NUCLEO_BOARD
	if(state)
		BSP_LED_On(LED_BLUE);
	else
		BSP_LED_Off(LED_BLUE);
#else
	if(state)
    	HAL_GPIO_WritePin(TXLED_GPIO_Port, TXLED_Pin, GPIO_PIN_SET);
	else
		HAL_GPIO_WritePin(TXLED_GPIO_Port, TXLED_Pin, GPIO_PIN_RESET);
#endif
}
// set green direction for bidir LED
void LED_SetGreen(void)
{
#if _BOARD_TYPE == NUCLEO_BOARD
	BSP_LED_On(LED_GREEN);
#else
    HAL_GPIO_WritePin(LED_Green_GPIO_Port, LED_Green_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_Red_GPIO_Port, LED_Red_Pin, GPIO_PIN_RESET);
#endif
}

// set red direction for bidir LED
void LED_SetRed(void)
{
#if _BOARD_TYPE == NUCLEO_BOARD
	BSP_LED_On(LED_RED);
#else
    HAL_GPIO_WritePin(LED_Green_GPIO_Port, LED_Green_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_Red_GPIO_Port, LED_Red_Pin, GPIO_PIN_SET);
#endif
}




/*---------------------------------------------------------------------------
	Project:	      IP400 Unified Firmware Platform

	Module:		      Flash read/write for STM32WL33

	File Name:	      flash.c

	Date Created:	  Apr 19, 2026

	Author:			  MartinA

	Description:      Read and write the setup data to internal flash memory

					  Copyright © 2024-26, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/
#include <string.h>

#include "setup.h"

// flash stuff for wl333
#ifndef STM32F7XX
#define FLASH_PAGE_ADDR    ((uint32_t)0x1007F800)
#define FLASH_PAGE_NUM		127
static FLASH_EraseInitTypeDef EraseInitStruct;
#endif

// data is stored in the last flash page
BOOL ReadSetup(void)
{
	__IO uint32_t data32 = 0;;

	uint32_t memAddr = FLASH_PAGE_ADDR;

	uint32_t *dst_addr = setup_memory.flashwords;
	uint16_t nwords = sizeof(STN_PARAMS)/sizeof(uint32_t);

	while(nwords--){
	    data32 = *(__IO uint32_t *)memAddr;
	    *dst_addr++ = data32;
	    memAddr += sizeof(uint32_t);
	}

	SetMyVPNAddr();

	return TRUE;
}

// write the setup to flash on WL33
HAL_StatusTypeDef WriteSetup(void)
{
	HAL_StatusTypeDef status=0;
	uint32_t PageError;

	// first get all the setup data
	for(int i=0;i<N_XCVRS;i++)		{
		RADIO_SETUP *setup = (RADIO_SETUP *)GetRadioSetup(i);
		memcpy(&setup_memory.params.radio_data[i], setup, sizeof(RADIO_SETUP));
	}
	strcpy(setup_memory.params.setup_data.gridSq, GetGridSq(setup_memory.params.setup_data.latitude, setup_memory.params.setup_data.longitude));


	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

	// erase it first
	USART_Print_string("Erasing...");
	EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Page        = FLASH_PAGE_NUM;
	EraseInitStruct.NbPages     = 1;

	if ((status=HAL_FLASHEx_Erase(&EraseInitStruct, &PageError)) != HAL_OK)
	  return status;

	// waste some time...
	for(int i=0;i<1000;i++);

	// now write
	USART_Print_string("Writing...");
	uint32_t memAddr = FLASH_PAGE_ADDR;
	uint32_t *src_addr = setup_memory.flashwords;
	uint16_t nwords = sizeof(SETUP_MEMORY)/sizeof(uint32_t);

	while(nwords--)
	{
	    uint32_t data32 = *(__IO uint32_t *)src_addr++;

	    if ((status=HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, memAddr, data32)) != HAL_OK)
	    	return status;
	    memAddr += sizeof(uint32_t);
#if __FLASH_TEST
		USART_Print_string("%02d..", nwords);
#endif
	}
	return status;
}

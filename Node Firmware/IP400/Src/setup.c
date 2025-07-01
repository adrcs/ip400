/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    setup.c

	Author:		    MartinA

	Description:	Holds and displays the setup data

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History: Revised to Rev 34

---------------------------------------------------------------------------*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <main.h>

#include "types.h"
#include "setup.h"
#include "usart.h"
#include "ip.h"
#include "frame.h"

#define USE_HAL

// flash stuff
#define FLASH_PAGE_ADDR    ((uint32_t)0x1007F800)
#define FLASH_PAGE_NUM		127
static FLASH_EraseInitTypeDef EraseInitStruct;

// current build number
char *revID = "$Revision: 37 $";
char *dateID = "$Date: 2025-06-30 20:36:27 -0600 (Mon, 30 Jun 2025) $";

/*
 * Default setup parameters
 */
SETUP_MEMORY setup_memory;

SETUP_MEMORY def_params = {
		.params.setup_data.flags = { 1, 0, 0 , 1, 0 },		// LSB specified first
		.params.setup_data.stnCall ="NOCALL",
		.params.setup_data.gridSq = "DO21vd",
		.params.setup_data.latitude = "51.08",
		.params.setup_data.longitude = "-114.10",
		.params.setup_data.beaconInt = 5,
		//
		.params.radio_setup.lFrequencyBase = 445750000,
		.params.radio_setup.xModulationSelect = MOD_4FSK,
		.params.radio_setup.lDatarate = 100000,
		.params.radio_setup.lFreqDev = 25000,
		.params.radio_setup.lBandwidth = 200000,
		.params.radio_setup.dsssExp = 0,
		.params.radio_setup.outputPower = 14,
		.params.radio_setup.PADrvMode = PA_DRV_TX_HP,
		.params.radio_setup.rxSquelch = -95,
		//
		.params.FirmwareVerMajor = 1,			// current rev is 1.3
		.params.FirmwareVerMinor = 3,
		.params.Magic = SETUP_MAGIC,
		.params.SetupCRC = 0
};

/*
 * IP Address information
 */
IP400_MAC myMAC;				// my callsign encoded
SOCKADDR_IN myIP;					// my IP address

// enum fields in setup struct
// modulation type
char *modTypes[] = {
		"2FSK",
		"4FSK",
		"2GFSK05",
		"2GFSK1",
		"4GFSK05",
		"4GFSK1",
		"ASK",
		"OOK",
		"POLAR",
		"CW"
};

// PA modes
char *paModes[] = {
		"TX 10dBm Max",
		"HP 14dBm Max",
		"TX_HP 20dBm Max"
};

// return the setup struct
STN_PARAMS *GetStationParams(void)			// get the station params
{
	return &setup_memory.params;
}

// compare call to station callsign
// returns true if callsigns match
BOOL CompareToMyCall(char *call)
{
	char expCall[20];

	// make sure call sign is padded out to 6 characters b4 comparison
	strcpy(expCall, setup_memory.params.setup_data.stnCall);
	strcat(expCall, "      ");

	if(!strncmp(call, expCall, MAX_CALL))
		return TRUE;
	return FALSE;
}

/*
 * Print the setup struct
 */
void printStationSetup(void)
{
	// station callsigns first
	USART_Print_string("Station Callsign->%s\r\n", setup_memory.params.setup_data.stnCall);
	if(setup_memory.params.setup_data.flags.AX25)		{
		USART_Print_string("AX.25 Address SSID: %s-%d\r\n", setup_memory.params.setup_data.stnCall,
				setup_memory.params.setup_data.flags.SSID);
	} else	{
		USART_Print_string("AX.25 Compatibility Not Enabled\r\n");
	}
	USART_Print_string("Description->%s\r\n\n", setup_memory.params.setup_data.Description);

	// station location
	USART_Print_string("Latitude->%s\r\n", setup_memory.params.setup_data.latitude);
	USART_Print_string("Longitude->%s\r\n", setup_memory.params.setup_data.longitude);
	USART_Print_string("Grid Square->%s\r\n", setup_memory.params.setup_data.gridSq);
	USART_Print_string("Capabilities->");
	if(setup_memory.params.setup_data.flags.fsk)
		USART_Print_string("FSK ");
	if(setup_memory.params.setup_data.flags.ofdm)
		USART_Print_string("OFDM ");
	if(setup_memory.params.setup_data.flags.AX25)
		USART_Print_string("AX.25 ");
	if(setup_memory.params.setup_data.flags.repeat)
		USART_Print_string("\r\nRepeat mode on\r\n");
	else
		USART_Print_string("\r\nRepeat mode off\r\n");
	USART_Print_string("Beacon Interval->%d mins\r\n\n", setup_memory.params.setup_data.beaconInt);
}

void printRadioSetup(void)
{
	// dump the radio init struct
	uint16_t fWhole = setup_memory.params.radio_setup.lFrequencyBase/1e6;
	uint16_t fFract = setup_memory.params.radio_setup.lFrequencyBase/1e3 - fWhole*1e3;
	USART_Print_string("RF Frequency->%d.%d MHz\r\n", fWhole, fFract);

	USART_Print_string("Modulation method->%s\r\n", modTypes[setup_memory.params.radio_setup.xModulationSelect]);

	uint16_t dWhole = setup_memory.params.radio_setup.lDatarate/1000;
	uint16_t dFract = setup_memory.params.radio_setup.lDatarate - dWhole*1000;
	USART_Print_string("Data Rate->%d.%d Kbps\r\n", dWhole, dFract);

	uint16_t pWhole = setup_memory.params.radio_setup.lFreqDev/1000;
	uint16_t pFract = setup_memory.params.radio_setup.lFreqDev - pWhole*1000;
	USART_Print_string("Peak Deviation->%d.%d KHz\r\n", pWhole, pFract);

	uint16_t bWhole = setup_memory.params.radio_setup.lBandwidth/1000;
	uint16_t bFract = setup_memory.params.radio_setup.lBandwidth - bWhole*1000;
	USART_Print_string("Channel Filter Bandwidth->%d.%d KHz\r\n", bWhole, bFract);

	USART_Print_string("Output Power->%d dBm\r\n", setup_memory.params.radio_setup.outputPower);
	USART_Print_string("PA Mode->%s\r\n", paModes[setup_memory.params.radio_setup.PADrvMode]);
	USART_Print_string("Rx Squelch->%d\r\n\n\n", setup_memory.params.radio_setup.rxSquelch);
}

/*
 * Manage the IP address
 */
 void GetMyVPN(SOCKADDR_IN **ipAddr)
 {
	 *ipAddr = &myIP;
 }

 void GetMyMAC(IP400_MAC **mac)
 {
	 *mac = &myMAC;
 }

 // set my IP Address
 void SetMyVPNAddr(void)
 {
	 char paddedCall[20];
	 strncpy(paddedCall, setup_memory.params.setup_data.stnCall, MAX_CALL);
	 strcat(paddedCall, "      ");

	 // encode my callsign
	 EncodeChunk(paddedCall, MAX_CALL, &myMAC.callbytes.encoded);

	 myMAC.vpnBytes.encvpn = GetVPNLowerWord();
	 GetVPNAddrFromMAC(&myMAC, &myIP);
 }

/*
 * This code manages saving and reading the setup params
 */
// internals
uint32_t CalcSetupCRC(void);

BOOL UpdateSetup(void)
{
	// update CRC before writing
	setup_memory.params.SetupCRC = CalcSetupCRC();

	if(WriteSetup() != HAL_OK)		{
		return FALSE;
	}

	if(!ReadSetup())		{
		return FALSE;
	}

	if(!VerifySetup())		{
		return FALSE;
	}

	return TRUE;			// only one iteration required
}

// verify that the current setup record is valid
BOOL VerifySetup(void)
{
	// if the magic number matches, then all is well
	if(setup_memory.params.Magic != SETUP_MAGIC)
		return FALSE;

	uint32_t SetupCRC =  CalcSetupCRC();

	if(SetupCRC == setup_memory.params.SetupCRC)
		return TRUE;

	return FALSE;
}

// set the default setup
void SetDefSetup(void)
{
	   memcpy((void *)&setup_memory.bytes, (const void *)&def_params.bytes, sizeof(SETUP_MEMORY));
}

// Read setup from Flash memory
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

// write the setup to OTP memory
HAL_StatusTypeDef WriteSetup(void)
{
	HAL_StatusTypeDef status=0;
	uint32_t PageError;

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
		USART_Print_string("%02d..", nwords);
	}
	return status;
}

/*
 * build related stuff
 */
char *getRevID(void)
{
	return &revID[1];
}

char *getDateID(void)
{
	return &dateID[1];
}


/*
 * HAL related functions
 */
uint32_t GetDevID0(void)
{
	return HAL_GetUIDw0();
}

uint32_t GetDevID1(void)
{
	return HAL_GetUIDw1();
}

// calcuate the setup CRC
uint32_t CalcSetupCRC(void)
{
	uint32_t CRCValue;
#ifdef USE_HAL
	CRCValue = HAL_CRC_Calculate(&hcrc, (uint32_t *)&setup_memory.bytes, sizeof(setup_memory) - sizeof(uint32_t));
#else

	uint32_t cnt, count = sizeof(setup_memory) - sizeof(uint32_t);
	uint8_t *arr = setup_memory.bytes;

	/* Reset CRC data register if necessary */
	CRC->CR = CRC_CR_RESET;


	/* Calculate number of 32-bit blocks */
	cnt = count >> 2;

	/* Calculate */
	while (cnt--) {
		/* Set new value */
		CRC->DR = *(uint32_t *)arr;

		/* Increase by 4 */
		arr += 4;
	}

	/* Calculate remaining data as 8-bit */
	cnt = count % 4;

	/* Calculate */
	while (cnt--) {
		/* Set new value */
		*((uint8_t *)&CRC->DR) = *arr++;
	}

	/* Return data */
	CRCValue = CRC->DR;
#endif
	return(CRCValue);
}

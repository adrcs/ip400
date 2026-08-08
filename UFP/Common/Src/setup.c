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

	Revision History: Revised to v1.5, fixed memory, added kiss and fixed SPI
					Changed memory allocation scheme
					Fixed SPI code
					Added more stats
					Fixed repeat mode
                    Fixed SPI up to 900 bytes
                    Split SPI task into two for easier debugging
                    FSM is common code to simulation and node
                    Updated KISS mode
                    Tested repeater mode
                    Added spinlock to enque/deque
                    reworked for UFP
                    Beta release
                    Moved flash memory handling to a separate platform file
                    Supernode platform is working
                    Added FPGA programming
                    Reworked UARTS to use DMA
                    Added OFDM-AB Code
                    Fixed bugs in DMA code
                    Reworked DMA
                    Fixed test tone; added flash update menu

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
#include "config.h"
#include "xcvr.h"

#define USE_HAL

// current build number
char *revID = "$Revision: 194 $";
char *dateID = "$Date: 2026-08-07 17:18:26 -0600 (Fri, 07 Aug 2026) $";

// kiss squelch setting
#define	SQUELCH_BASE			127				// 0 is -127dBm
#define	MAX_SQUELCH				-20				// max squelch setting

/*
 * Default setup parameters
 */
SETUP_MEMORY setup_memory;

SETUP_MEMORY def_params = {
		.params.setup_data.flags =
			{ __XCVR_WL33 | __XCVR_AT86, 		// has an FSK modem
			  __XCVR_OFDM_AB,					// has OFDM mode
			 TRUE , 							// AX.25 enabled
			 TRUE, 								// repeat is enabled
			 0 },								// SSID
		.params.setup_data.stnCall ="NOCALL",
		.params.setup_data.Description = "OFDM-AB Base Code",
		.params.setup_data.latitude = "51.143",
		.params.setup_data.longitude = "-114.176",
		.params.setup_data.beaconInt = 5,
		.params.setup_data.defModem = 0,

		//
		.params.FirmwareVerMajor = 2,						// current rev is 2.0
		.params.FirmwareVerMinor = 0,
		.params.Magic = SETUP_MAGIC,
		.params.SetupCRC = 0
};

/*
 * remote update from KISS set hardware command
 */
void SetHardware(uint8_t power, uint8_t squelch)
{

#if defined(__NUCLEOCC2) || defined(__PI_BOARD)
	// change power output

	RADIO_SETUP *setup = (RADIO_SETUP *)GetRadioSetup(XCVR_WL33);

	if(power < MAX_OUPUT_POWER)	{
		setup->outputPower = power;
		ApplySetup(XCVR_WL33);
	}

	// squelch setting is done in the subg state machine
	int16_t sqSetting = (int16_t)squelch - SQUELCH_BASE;
	if(sqSetting <= MAX_SQUELCH)
		setup->rxSquelch = sqSetting;
#endif
}

/*
 * IP Address information
 */
IP400_MAC myMAC;				// my callsign encoded
SOCKADDR_IN myIP;					// my IP address

// return the setup struct
STN_PARAMS *GetStationParams(void)			// get the station params
{
	return &setup_memory.params;
}

// return the current radio setup struct
RADIO_SETUP *getRadioSetup(int xcvrNum)
{
	return &setup_memory.params.radio_data[xcvrNum];
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

#if	__AX25_COMPATIBILITY
	if(setup_memory.params.setup_data.flags.AX25)		{
		USART_Print_string("AX.25 and KISS Mode Enabled, SSID: %s-%d\r\n", setup_memory.params.setup_data.stnCall,
				setup_memory.params.setup_data.flags.SSID);
	} else	{
		USART_Print_string("AX.25 and KISS Mode Not Enabled\r\n");
	}
#endif

	USART_Print_string("Description->%s\r\n\n", setup_memory.params.setup_data.Description);
	// station location
	USART_Print_string("Latitude->%s\r\n", setup_memory.params.setup_data.latitude);
	USART_Print_string("Longitude->%s\r\n", setup_memory.params.setup_data.longitude);
	strcpy(setup_memory.params.setup_data.gridSq, GetGridSq(setup_memory.params.setup_data.latitude, setup_memory.params.setup_data.longitude));
	USART_Print_string("Grid Square->%s\r\n", setup_memory.params.setup_data.gridSq);
	USART_Print_string("Capabilities->");
	if(setup_memory.params.setup_data.flags.fsk)
		USART_Print_string("FSK ");
	if(setup_memory.params.setup_data.flags.ofdm)
		USART_Print_string("OFDM ");
	#if	__AX25_COMPATIBILITY
	if(setup_memory.params.setup_data.flags.AX25)
		USART_Print_string("AX.25 ");
#endif
#if __INCLUDE_KISS
	USART_Print_string("KISS ");
#endif
	if(setup_memory.params.setup_data.flags.repeat)
		USART_Print_string("\r\nRepeat mode on\r\n");
	else
		USART_Print_string("\r\nRepeat mode off\r\n");
	USART_Print_string("Default Modem->%s\r\n", getType(setup_memory.params.setup_data.defModem));
	USART_Print_string("Beacon Interval->%d mins\r\n\n", setup_memory.params.setup_data.beaconInt);
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

 // get the AX25 mode
 BOOL isAX25Enabled(void)
 {
#if	__AX25_COMPATIBILITY
	if(setup_memory.params.setup_data.flags.AX25)
		return TRUE;
	else
		return FALSE;
#else
	return FALSE;
#endif
}

// get the SSID
uint8_t getAX25SSID(void)
{
 	 return setup_memory.params.setup_data.flags.SSID;
}

 // set my IP Address BOOL callEncode(char *callsign, uint16_t ipAddr, IP400_FRAME *frame, CallSignSource dest)
 void SetMyVPNAddr(void)
 {

	 IP400_FRAME fr;
	 callEncode(setup_memory.params.setup_data.stnCall, GetVPNLowerWord(), &fr, SRC_CALLSIGN);

	 GetVPNAddrFromMAC(&fr.source, &myIP);
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

// set the transceiver setup
// copy to transceiver data areas
void SetXcvrSetup(void)
{
	for(int i=0;i<N_XCVRS;i++)		{
		RADIO_SETUP *setup = (RADIO_SETUP *)GetRadioSetup(i);
		memcpy(setup, &setup_memory.params.radio_data[i], sizeof(RADIO_SETUP));
	}
}

// set the default setup
void SetDefSetup(void)
{
	   memcpy((void *)&setup_memory.bytes, (const void *)&def_params.bytes, sizeof(SETUP_MEMORY));
	   for(int i=0;i<N_XCVRS;i++)	{
		   RADIO_SETUP *radio = GetRadioSetup(i);
		   memcpy(&setup_memory.params.radio_data[i], radio, sizeof(RADIO_SETUP));
	   }
		strcpy(setup_memory.params.setup_data.gridSq, GetGridSq(setup_memory.params.setup_data.latitude, setup_memory.params.setup_data.longitude));

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

// calculate the setup CRC
uint32_t CalcSetupCRC()
{
	return(DoCRCcalc((uint32_t *)&setup_memory.bytes, sizeof(setup_memory) - sizeof(uint32_t)));
}


// calcuate a CRC-16 using hardware
uint32_t DoCRCcalc(uint32_t *data, int size)
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

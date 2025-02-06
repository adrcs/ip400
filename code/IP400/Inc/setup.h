/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      setup.h

	Author:		      MartinA

	Creation Date:	  Jan 13, 2025

	Description:	  Definitions for setup data

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_SETUP_H_
#define INC_SETUP_H_

#include <stm32wl3x_hal_mrsubg.h>

#include "frame.h"
#include "usart.h"

#define	__USE_SETUP_PARAMS	1				// set to 1 to use setup parameters

// defined elsewhere
extern char *modTypes[];
extern char *paModes[];

typedef struct setup_flags_t {
	unsigned	fsk:	1;					// can run FSK
	unsigned	ofdm:	1;					// can run OFDM
	unsigned 	aredn:	1;					// is an AREDN node
	unsigned	repeat:	1;					// repeat mode default
	unsigned	ext:	1;					// use extended callsign
	unsigned	rate:   3;					// data rate in use
} SETUP_FLAGS;

// data rates in the flag field (FSK mode)
enum	{
	FSK_1200,
	FSK_9600,
	FSK_56K,
	FSK_100K,
	FSK_200K,
	FSK_400K,
	FSK_600K
};

// setup data struct
typedef struct setup_data_t {
	SETUP_FLAGS		flags;					// flags
	char			stnCall[MAX_CALL];		// station call sign
	char			extCall[EXT_CALL];		// extended call sign
	char			latitude[10];			// latititude
	char			longitude[10];			// longitude
	char			gridSq[10];				// grid square
	uint16_t		beaconInt;				// beacon interval
} SETUP_DATA;

// Radio setup struct
typedef struct radio_setup_t {
	  uint32_t          lFrequencyBase;     /*!< Specifies the base carrier frequency (in Hz) */
	  MRSubGModSelect   xModulationSelect;  /*!< Specifies the modulation @ref MRSubGModSelect */
	  uint32_t          lDatarate;          /*!< Specifies the datarate expressed in sps.*/
	  uint32_t          lFreqDev;           /*!< Specifies the frequency deviation expressed in Hz. */
	  uint32_t          lBandwidth;         /*!< Specifies the channel filter bandwidth expressed in Hz. */
	  uint8_t           dsssExp;            /*!< Specifies the DSSS spreading exponent. Use 0 to disable DSSS. */
	  uint8_t           outputPower;        /*!< PA value to write expressed in dBm. */
	  MRSubG_PA_DRVMode PADrvMode;          /*!< PA drive mode. */
	  int16_t			rxSquelch;			// rx squelch level
} RADIO_SETUP;

// setup struct
typedef struct stn_params_t {
	SETUP_DATA	setup_data;					// basic setup data
	RADIO_SETUP	radio_setup;				// radio setup
    uint8_t		FirmwareVerMajor;			// firmware major rev
    uint8_t		FirmwareVerMinor;			// firmware minor vers
    uint32_t    Magic;                      // magic number: "DEBEADEF"
    uint32_t	SetupCRC;					// CRC
} STN_PARAMS;

// beacon header
typedef union {
	struct beacon_hdr_t {
		SETUP_FLAGS	flags;
		uint8_t		txPower;
	} setup;
	uint8_t		hdrBytes[sizeof(struct beacon_hdr_t)];
} BEACON_HEADER;

#define SETUP_MAGIC		0xDEBEADEF			// magic number

typedef union {
	STN_PARAMS	params;
	uint8_t		bytes[sizeof(STN_PARAMS)];
	uint32_t	flashwords[sizeof(STN_PARAMS)/sizeof(uint32_t)];
} SETUP_MEMORY;

extern SETUP_MEMORY setup_memory;
extern SETUP_MEMORY def_params;
extern CRC_HandleTypeDef hcrc;

// validations used by key entry
#if US
#define	MIN_FREQ		420000000			// min freq (US only)
#else
#define	MIN_FREQ		430000000			// min freq (CAN only)
#endif

// links in
void printStationSetup(void);				// print setup struct
void printRadioSetup(void);					// print radio setup
char *GetMyCall(void);						// return the station's callsign
STN_PARAMS *GetStationParams(void);			// get the station params
BOOL CompareToMyCall(char *call);
//
BOOL VerifySetup(void);
BOOL ReadSetup(void);
BOOL WriteSetup(void);
void SetDefSetup(void);
BOOL UpdateSetup(void);

#endif /* INC_SETUP_H_ */

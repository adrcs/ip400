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

				  Copyright (c) 2024-25 lberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_SETUP_H_
#define INC_SETUP_H_

#include "frame.h"
#include "usart.h"
#include "config.h"
#include "xcvr.h"

#if	 __XCVR_OFDM_AB
#include "ofdm.h"
#endif

#define	__USE_DEFAULT_SETUP	0				// set to 1 to use setup parameters
#define	US					0				// station is in the US

#define	MAX_DATAFLD			10				// max data field size
#define	MAX_DESC			32				// description field

typedef struct setup_flags_t {
	unsigned	fsk:	1;					// can run FSK
	unsigned	ofdm:	1;					// can run OFDM
	unsigned 	AX25:	1;					// is using AX.25 compatibility addressing
	unsigned	repeat:	1;					// repeat mode default
	unsigned	SSID:	4;					// AX.25 SSID
} SETUP_FLAGS;

// setup data struct
typedef struct setup_data_t {
	SETUP_FLAGS		flags;					// flags
	char			stnCall[MAX_CALL];		// station call sign
	char			extCall[MAX_CALL];		// extended call sign
	char			Description[MAX_DESC];	// description of my radio
	char			latitude[10];			// latititude
	char			longitude[10];			// longitude
	char			gridSq[10];				// grid square
	uint16_t		beaconInt;				// beacon interval
	uint8_t			defModem;				// default transmit modem
} SETUP_DATA;

// radio header
typedef struct radio_hdr_t	{
	uint8_t		txPower;					// transmit power
	uint32_t	txFrequency;				// transmit frequency
	uint32_t	rxFrequency;				// receive frequency
} RADIO_HDR;

// beacon header
typedef union {
	struct beacon_hdr_t {
		uint8_t		nXcvrs;					// number of transceivers
		SETUP_FLAGS	flags;					// setup flags
		uint8_t		FirmwareMajor;			// firmware major version
		uint8_t		FirmwareMinor;			// firmware minor version
		RADIO_HDR	radios[MAX_XCVRS];
	} setup;
	uint8_t		hdrBytes[sizeof(struct beacon_hdr_t)];
} BEACON_HEADER;

#if __XCVR_WL33
#include "stm32wl3x_hal_mrsubg.h"
typedef MRSubGModSelect 	ModSelect;
typedef MRSubG_PA_DRVMode  	PA_DRVMode;
#else
/*
 * These are TBDs for now...
 */
typedef	uint8_t		ModSelect;
typedef	uint8_t		PA_DRVMode;
#endif

// Radio setup structs
// WL33
typedef struct radio_setup_t {
// common fields
	  uint32_t          lFrequencyBase;     /*!< Specifies the base carrier frequency (in Hz) */
	  uint8_t           outputPower;        /*!< PA value to write expressed in dBm. */
	  ModSelect   		xModulationSelect;  /*!< Specifies the modulation @ref MRSubGModSelect */
// WL33/AT86 Fields
#if  (__XCVR_WL33 || __XCVR_AT86)
	  uint32_t          lBandwidth;         /*!< Specifies the channel filter bandwidth expressed in Hz. */
	  uint32_t          lDatarate;          /*!< Specifies the datarate expressed in sps.*/
	  uint32_t          lFreqDev;           /*!< Specifies the frequency deviation expressed in Hz. */
	  uint8_t           dsssExp;            /*!< Specifies the DSSS spreading exponent. Use 0 to disable DSSS. */
	  PA_DRVMode 		PADrvMode;          /*!< PA drive mode. */
	  int16_t			rxSquelch;			// rx squelch level
#endif
// OFDM-AB Fields
#if	 __XCVR_OFDM_AB
	  uint8_t			portSelect;			// selected port
	  uint8_t			adcGain;			// ADC input gain
	  uint8_t			dacGain;			// dac input gain
	  uint8_t			txDelay;			// transmitter keyup delay
	  OFDM_BW			bandwidth;			// Bandwidth
	  OFDM_FEC			defFEC;				// default FEC method
#endif
} RADIO_SETUP;

// setup data struct
typedef struct stn_params_t {
	SETUP_DATA		setup_data;				// basic setup data
	RADIO_SETUP		radio_data[N_XCVRS];	// radio setup data
    uint8_t			FirmwareVerMajor;		// firmware major rev
    uint8_t			FirmwareVerMinor;		// firmware minor vers
    uint32_t    	Magic;                  // magic number: "DEBEADEF"
    uint32_t		SetupCRC;				// CRC
} STN_PARAMS;

/*
 * menu system definitions
 */
// struct to hold validation values
typedef struct field_validator_t {
	uint32_t	MinVal;				// minimum value or string length
	uint32_t	MaxVal;				// maximum value or string length
	void    	*setupVal;			// pointer to setup value
	int			type;				// type of entry
	uint32_t	scalar;				// scalar to convert to decimal
} FIELD_VALIDATOR;
//

// data types we are updating
enum entry_types_e {
	uint4_lo,		// uint4 lsb
	uint4_hi,		// uint4 msb
	uint8_type,		// uint8 field
	int16_type,		// int16 field
	uint32_type,	// uint32 field
	float_type,		// floating point
	char_type,		// character type
	yesno_type		// yes/no type
};

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
void SetHardware(uint8_t power, uint8_t squelch);	// set hardware from kiss frame
void printStationSetup(void);						// print setup struct
char *GetMyCall(void);								// return the station's callsign
STN_PARAMS *GetStationParams(void);					// get the station params
RADIO_SETUP *getRadioSetup(int xcvrNum);
BOOL CompareToMyCall(char *call);
//
BOOL VerifySetup(void);
BOOL ReadSetup(void);
HAL_StatusTypeDef WriteSetup(void);
void SetDefSetup(void);
void SetXcvrSetup(void);
BOOL UpdateSetup(void);
BOOL isAX25Enabled(void);
uint8_t getAX25SSID(void);
void SetMyVPNAddr(void);
//
uint32_t DoCRCcalc(uint32_t *data, int size);

// buffer to beacon header struct
void buf2hdr(BEACON_HEADER *beacon_hdr, uint8_t *buf);

// device ID
uint32_t GetDevID0(void);
uint32_t GetDevID1(void);
// build ID
char *getRevID(void);
char *getDateID(void);
char *GetGridSq(char *latitude, char *longitude);

// get the grid square from lat/long
char *GetGridSq(char *latitude, char *longitude);

#endif /* INC_SETUP_H_ */

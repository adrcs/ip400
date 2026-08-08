/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      Kiss.h

	Author:		      MartinA

	Creation Date:	  Jan 13, 2025

	Description:	  Definitions for kiss mode frames

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef KISSDEFINES_H
#define  KISSDEFINES_H

#include <stdint.h>

// frame delimiters
#define  KISS_FEND  			0xC0U		// frame start/end
#define  KISS_FESC  			0xDBU		// frame escape
#define  KISS_TFEND 			0xDCU		// escaped frame end
#define  KISS_TFESC 			0xDDU		// escaped escape
#define  KISS_EOF				0xFFU		// end of frame found

// kiss frame types
typedef enum kiss_types_e	{
		KISS_TYPE_DATA=0,         			// 00: Data frame
		KISS_TYPE_TX_DELAY,     			// 01: set tx delay (not implemented)
		KISS_TYPE_P_PERSISTENCE,			// 02: set persistence (not implemented)
		KISS_TYPE_SLOT_TIME,    			// 03: set slot time (not implemented)
		KISS_TYPE_TX_TAIL,      			// 04: tail time (not implemented)
		KISS_TYPE_FULL_DUPLEX,  			// 05: duplex (not implemented)
		KISS_TYPE_SET_HARDWARE, 			// 06: set power and squelch setting
		KISS_TYPE_SET_CONFIGURATION,		// 07: set configuration
		KISS_GET_VERSION,					// 08: get version number
		KISS_TEST_MODE,						// 09: test mode
		KISS_SET_SERIAL,					// 0A: set serial number
		KISS_GET_ALL,						// 0B: get all param
		KISS_UNDEFINED,						// 0C: not defined
		KISS_FLASH_UPDATE,					// 0D: update flash memory
		KISS_GET_SERIAL,					// 0E: get serial number
		N_KISS_COMMANDS
} KissCommandType;

// test modes
enum kiss_test_modes_e {
		KISS_TEST_OFF,						// all tests off
		KISS_TEST_CW,						// CW test mode
		KISS_TEST_PRBS,						// PRBS test mode
		N_KISS_TESTS						// number of test modes
};

#define  KISS_TYPE_DATA_WITH_ACK 0x0CU
#define  KISS_TYPE_ACK           0x0CU
#define  KISS_TYPE_POLL          0x0EU

// return values back to menu
#define	 MENU_MORE				0			// more required
#define	 MENU_DONE				-1			// done (equivalent to NO_ITEM in menu)

/*
 * AX.25 frame structures
 */
#define		N_AX25_CALL		6				// length of callsign
#define		N_ADDR			7				// length of address
#define		N_NO_RPT		2				// address fields in no repeat frame
#define		N_ONE_RPT		3				// address fields in single repeater frame
#define		N_TWO_RPT		4				// address fields in two repeater frame
#define		SHORT_BUFFER	20				// short data buffer (handles most commands)
/*
 * Ax25 frame fields
 */
typedef enum ax25_addr_flds_e	{
	AX25_DEST_ADDRESS=0,					// destination address
	AX25_SOURCE_ADDRESS,					// source address
	AX25_RPT1_ADDRESS,						// repeater one field
	AX25_RPT2_ADDRESS						// repeater two field
} AX25AddrIndex;

// call sign byte field
typedef struct	call_field_t	{
	unsigned	char res:		1;				// unused bit
	unsigned	char ascii:		7;				// ascii characters
} CALL_FIELD;
// SSID byte field
typedef struct	addr_ssid_t	{
	unsigned	char final:		1;				// final address bit
	unsigned	char ssid:		4;				// SSID
	unsigned	char res:		2;				// reserved bits
	unsigned	char c_h_bit:	1;				// 'C' or 'H' bit
} ADDR_SSID;


// address field
typedef union	ax_25_addr_t	{
	struct	{
		union {
			CALL_FIELD	callsign[N_AX25_CALL];	// callsign
			uint8_t callbytes[N_AX25_CALL];
		} call;
		union {
			ADDR_SSID	ssidField;				// SSID
			uint8_t ssidbyte;
		} ssid;
	} callFields;
	uint8_t	addressField[N_ADDR];
} AX25_ADDR;
/*
 * Kiss structures
 */
// kiss frame basic structures
typedef struct kiss_type_t	{
	unsigned 	type:4;						// frame type
	unsigned	port:4;						// port number
} KISS_CMD;
/*
 * Generic KISS frame
 */
typedef struct generic_kiss_t	{
	KISS_CMD	command;				// command type
	uint8_t		data[SHORT_BUFFER];		// data portion
} GENERIC_KISS_FRAME;
/*
 * AX.25 KISS frame
 */
// non-repeaet frame
typedef struct ax25_non_rpt_frame {
	union	{
		AX25_ADDR	address[N_NO_RPT];		// address field
		uint8_t bytes[N_NO_RPT*(N_AX25_CALL+1)];
	} frame_header;
	uint8_t data[];
} AX25_NON_RPT_FRAME;
// same with one repeater
typedef struct ax25_one_rpt_frame {
	union	{
		AX25_ADDR	address[N_ONE_RPT];		// address field
		uint8_t bytes[N_ONE_RPT*(N_AX25_CALL+1)];
	} frame_header;
	uint8_t data[];
} AX25_ONE_RPT_FRAME;
// same with two repeaters
typedef struct ax25_two_rpt_frame {
	union	{
		AX25_ADDR	address[N_TWO_RPT];		// address field
		uint8_t bytes[N_ONE_RPT*(N_AX25_CALL+1)];
	} frame_header;
	uint8_t data[];
} AX25_TWO_RPT_FRAME;

/*
 * References
 */
void KissInit(void);
BOOL processKissFrame(void);						// process a kiss frame
#endif


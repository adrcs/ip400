/*---------------------------------------------------------------------------
	Project:	      IP400 Unified Firmware Platform

	Module:		      Transceiver Abstraction Layer

	File Name:	      xcvr.h

	Date Created:	  Mar 3, 2026

	Author:			  MartinA

	Description:      Definitions for the abstraction calling structs

					  Copyright © 2024-26, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/

#ifndef XCVR_H_
#define XCVR_H_

#include "types.h"
#include "frame.h"

#define	MAX_XCVRS				3				// max transceivers
#define	DEFAULT_MODEM			0				// default modem for internal packets

// transceiver abstraction structure
typedef struct	xcvr_abs_t	{
		uint8_t	Index;							// radio index
		char	*type;							// type of radio
// static functions
		void	(*Init)(void);					// initialize
		void	(*Process)(void);				// processing
		void *	(*GetSetup)(void);				// get the radio setup
		void 	(*ApplySetup)(void *);			// apply setup
// filled in by xcvr type
		void	(*QueTxFrame)(void *);			// queue a transmit frame
		void	(*SetTestMode)(uint8_t);		// set a test mode
		void *	(*GetStats)(void);				// get stats
// callbacks to this module
		void	(*QueRxFrame)(void *);			// queue an rx frame
} XCVR_ABS;

// abstractions
extern XCVR_ABS	xcvrs[];						// transceiver abstractions


// xcvr test modes
typedef enum xcvr_test_e {
	XCVR_TEST_OFF=0,			// test mode off
	XCVR_TEST_CW,				// CW mode: digital loopback
	XCVR_TEST_PATTERN,			// send Test pattern
	XCVR_TEST_ANALB				// analog loopback (OFDM Only)
} XcvrTestMode;

// test modes for codec
typedef enum codec_test_e {
	CODEC_TEST_OFF=0,			// tests off
	CODEC_TEST_TONE,			// 1KHz tone
	CODEC_TEST_ANALOG_LB,		// analog loopback w/o AGC
	CODEC_TEST_ANALOG_LB_AGC,	// analog loopback with AGC
	CODEC_TEST_DIGITAL_LB,		// digital loopback w/o AGC
	CODEC_TEST_DIGITAL_LB_AGC	// digital loopback with AGC
} CodecTestMode;

#define	CODEC_TEST(x)	(x<<4)	// codec tests in upper nibble

// transceiver hardware and setup offsets
typedef enum xvr_index_e {
#if	__XCVR_AT86					// has a AT86RF215
	XCVR_AT86_SUBG,				// AT86 sub-Ghz
#endif
#if	__XCVR_OFDM_AB				// has an OFDM transceiver
	XCVR_OFDM,					// OFDM-AB mode
#endif
#if	__XCVR_WL33					// has a WL33
	XCVR_WL33,					// WL 33 transceiver
#endif
} XcvrIndex;

/*
 * Include radio definitions
 */

#if defined(__NUCLEOCC2) || defined(__PI_BOARD)
#include "wl33.h"
#endif

#ifdef __SUPERNODE_F722
#include "at86RF215.h"
#endif

// links processed here..
uint8_t getNxcvrs(void);
void Xcvr_Task_GetVectors(void);
char *getType(uint8_t index);

// abstraction interface for physical levels..
void *GetRadioSetup(int index);
void ApplySetup(int index);
void QueueTxFrame(void *txframe, int xcvrAddr);
void runXcvrTest(uint8_t index, uint8_t testNum);
RADIO_STATS *GetRadioStats(uint8_t index);

void resetBufferStats(int index);

// callbacks
void QueueRxFrameCallback(void *rxframe);

// get the abstractions for each transceiver type
__REPLACEABLE XCVR_ABS *wl33_GetVectors(void);				// get wl33 Vectors
__REPLACEABLE XCVR_ABS *AT86RF215_GetVectors(void);			// get AT Vectors
__REPLACEABLE XCVR_ABS *ofdb_ab_GetVectors(void);			// get OFDM_AB Vectors

#endif /* XCVR_H_ */

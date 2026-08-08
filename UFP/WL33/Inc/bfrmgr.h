/*---------------------------------------------------------------------------
	Project:	      NucleoCC2

	Module:		      <module name here>

	File Name:	      bfrmgr.h

	Date Created:	  Mar 17, 2026

	Author:			  MartinA

	Description:      <Definitions in here>

					  Copyright © 2024-26, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/

#ifndef BFRMGR_H_
#define BFRMGR_H_

#include <stdint.h>

#include "types.h"

// typedefs
typedef uint8_t	RAWBUFFER;

// buffer state
typedef enum	{
		BUFFER_READY,		// ready to tx/rx
		BUFFER_EMPTY,		// empty: ready for data
		BUFFER_ACTIVE,		// active: filling or emptying
		BUFFER_FULL,		// full: ready to tx or has RX data
		BUFFER_UNALLOC		// not allocated yet
} BufferState;

// timer states
typedef enum tx_timer_state_e {
	TMR_NOTRUNNING=0,		// timer is not running
	TMR_RUNNING,			// timer is running
	TMR_EXPIRED				// expired
} TxTmrState;

// buffer status struct
typedef struct buf_stat_t {
	BufferState		state;						// subg buffer state
	RAWBUFFER	 	*addr;						// address of local buffer
	uint16_t		length;						// rx length
	TxTmrState		tmrState;					// timer state
	uint8_t			tmrValue;					// timer value
	int 			nFrames;					// number of frames processed
	int				nXmitted;					// number transmitted
	uint16_t		txSize;						// transmitter buffer size
} BUFFER_STATUS;

IP400_FRAME *Buf2IP400(void *);

// task links
BOOL BufferTask_init(void);
void BufferTask_Exec(void);


// Buffer manager methods
BOOL RxHasData(void);
uint8_t *GetRxBufferAddr(void);
uint8_t *getRxBufferFrame(void);
void SetRxDone(void);

BOOL TxHasRoom(int length);
BOOL IsTxReady(void);
BOOL PutTxBuffer(IP400_FRAME *fr);
uint16_t GetTxBufferLength(void);
void *GetTxBufferAddr(void);
void *GetTxBufferAltAddr(void);
void SetTxBufferDone(void);

//
BUFFER_STATUS *getBufferStatus(void);

#endif /* BFRMGR_H_ */

/*---------------------------------------------------------------------------
	Project:	      IP400 Unified Firmware platform

	Module:		      Transceiver abstraction task

	File Name:	      xcvr.c

	Author:		      MartinA

	Creation Date:	  Jan 8, 2025

	Description:      Manage the STMWL33 Sub-GHz radio physical connection

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.


	Revision History:

---------------------------------------------------------------------------*/
#include <cmsis_os2.h>
#include <stdlib.h>
#include <string.h>
#include <config.h>

#include <cmsis_os2.h>
#include <FreeRTOS.h>
#include <semphr.h>

#include "frame.h"
#include "dataq.h"
#include "usart.h"
#include "setup.h"
#include "memory.h"
#include "xcvr.h"

#include "led.h"

// frame queues
FRAME_QUEUE			rxQueue;		// receiver queue
void QueueRxFrame(void *txframe);

/*
 * Transceiver abstraction interfaces
 */
XCVR_ABS xcvrs[N_XCVRS] = {
#if	__XCVR_WL33						// has a WL33
		{ .Index = XCVR_WL33,
		  .type = "WL33",
		},
#endif
#if	__XCVR_AT86						// has a AT86RF215
		{ .Index = XCVR_AT86_SUBG,
		  .type = "AT86RF215",
		},
#endif
#if	__XCVR_OFDM_AB						// has a MODE B
		{ .Index = XCVR_OFDM,
		  .type = "OFDM-AB",
		},
#endif
};


/*
 * Hooks to retreive the transceiver vectors
 */
typedef struct abs_vectors_t {
		XCVR_ABS *	(*GetVectors)(void);
} ABS_VECTORS;;

ABS_VECTORS xcvr_vect_structs[N_XCVRS] = {
#if	__XCVR_WL33
		{ .GetVectors = &wl33_GetVectors },
#endif
#if	__XCVR_AT86						// has a AT86RF215
		{ .GetVectors = &AT86RF215_GetVectors },
#endif
#if	__XCVR_OFDM_AB						// has a MODE B
		{ .GetVectors = &ofdb_ab_GetVectors },
#endif
};

/*
 * Functions processed locally
 */

/*
 * Initialize the abstractions
 */
void Xcvr_Task_GetVectors(void)
{
	for(int i=0;i<N_XCVRS;i++)	{
		XCVR_ABS xcvr_vectors =  (*xcvr_vect_structs[i].GetVectors());

		// links into xcvr code
		xcvrs[i].Init = xcvr_vectors.Init;
		xcvrs[i].Process = xcvr_vectors.Process;
		xcvrs[i].GetSetup = xcvr_vectors.GetSetup;
		xcvrs[i].ApplySetup = xcvr_vectors.ApplySetup;
		xcvrs[i].QueTxFrame = xcvr_vectors.QueTxFrame;
		xcvrs[i].SetTestMode = xcvr_vectors.SetTestMode;
		xcvrs[i].GetStats = xcvr_vectors.GetStats;
	}
}

/*
 * return number of xcvrs
 */
uint8_t getNxcvrs(void)
{
	return N_XCVRS;
}

/*
 * get the name
 */
char *getType(uint8_t index)
{
	if(index > N_XCVRS)
		return "<null>";

	return xcvrs[index].type;
}

/*
 * General entry points for all transceivers
 * in the same order as the vector struct
 */

/*
 * Initialize the xcvr and local vars
 */
void Xcvr_Task_init(void)
{
	// init rx queue
	rxQueue.q_forw = &rxQueue;
	rxQueue.q_back = &rxQueue;

	// init callbacks

	for(int i=0;i<N_XCVRS;i++)	{
		(xcvrs[i].Init)();
	}

}

/*
 * Processing loop
 */
void Xcvr_Task_Exec(void)
{
	for(int i=0;i<N_XCVRS;i++)
		(xcvrs[i].Process());

	// process any outstanding rx frames
	// see if we can buffer anything...
	while(quehasData(&rxQueue))	{
		IP400_FRAME *f = dequeFrame(&rxQueue);
		ProcessRxFrame(f, f->length);
	}
}

/*
 * get the setup params
 */
void *GetRadioSetup(int index)
{
	if(index > N_XCVRS)
		return NULL;

	return (char *)(xcvrs[index].GetSetup());
}


/*
 * Apply the setup params
 */
void ApplySetup(int index)
{
	if(index > N_XCVRS)
		return;

	RADIO_SETUP *setup = getRadioSetup(index);

	(xcvrs[index].ApplySetup((void *)setup));
}

/*
 * Queue a transmit frame for a transceiver
 */
void QueueTxFrame(void *txframe, int xcvrAddr)
{
	IP400_FRAME *fr = (IP400_FRAME *)txframe;

	if(xcvrAddr > N_XCVRS-1)
		return;

	(xcvrs[xcvrAddr].QueTxFrame(fr));
}


/*
 * run a test on a xcvr
 */
void runXcvrTest(uint8_t index, uint8_t testNum)
{
	if(index > N_XCVRS)
		return;

	(xcvrs[index].SetTestMode(testNum));
}

/*
 * retreive the stats for a specific radio
 */
RADIO_STATS *GetRadioStats(uint8_t index)
{
	if(index > N_XCVRS)
		return NULL;

	void *stats = (xcvrs[index].GetStats());

	return (RADIO_STATS *)stats;
}

/*
 * callback to queue a received frame
 */
void QueueRxFrameCallback(void *rxframe)
{
	IP400_FRAME *fr = (IP400_FRAME *)rxframe;
	enqueFrame(&rxQueue, fr, fr->length);
}


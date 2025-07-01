/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    dataq.c

	Author:		    MartinA

	Description:	This module enques and deques an IP400 Frame type on a queue

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History:

---------------------------------------------------------------------------*/
#include <malloc.h>

#include "types.h"
#include "frame.h"
#include "dataq.h"

/*
 * Enque a frame. The frame data buffer must have
 * alredy been allocated
 */
BOOL enqueFrame(FRAME_QUEUE *que, IP400_FRAME *fr)
{
	FRAME_QUEUE *f;
	if((f = malloc(sizeof(FRAME_QUEUE))) == NULL)
		return FALSE;

	// set the frame buffer
	f->frame = fr;
	f->length = fr->length + sizeof(IP400_FRAME);

	insque((QUEUE_ELEM *)f, (QUEUE_ELEM *)que->q_back);
	return TRUE;

}

/*
 * Deqeue a frame
 * Returns null if no frame is one the queue
 * Does NOT dealloc data or frame or q
 */
IP400_FRAME *dequeFrame(FRAME_QUEUE *que)
{
	IP400_FRAME *ipFrame;

	if(que->q_back == que)
		return NULL;

	FRAME_QUEUE *f = que->q_forw;
	remque((struct qelem *)f);

	ipFrame = f->frame;

	free(f);
	return ipFrame;
}

/*
 * Test to see if anything is queued
 */
BOOL quehasData(FRAME_QUEUE *que)
{
	if(que->q_back == que)
		return FALSE;

	return TRUE;
}

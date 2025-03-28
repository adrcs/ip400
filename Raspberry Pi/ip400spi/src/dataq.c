/*---------------------------------------------------------------------------
	Project:	    ip400SPI

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
#include "dataq.h"


/*
 * Enque a frame. The frame data buffer must have
 * alredy been allocated
 */
BOOL enqueFrame(FRAME_QUEUE *que, void *fr)
{
	FRAME_QUEUE *f;
	if((f = malloc(sizeof(FRAME_QUEUE))) == NULL)
		return FALSE;

	// set the frame buffer
	f->data = fr;

	insque((QUEUE_ELEM *)f, (QUEUE_ELEM *)que->q_back);
	return TRUE;

}

/*
 * Deqeue a frame
 * Returns null if no frame is one the queue
 */
void *dequeFrame(FRAME_QUEUE *que)
{
	void *ipFrame;

	if(que->q_back == que)
		return NULL;

	FRAME_QUEUE *f = que->q_forw;
	remque((struct qelem *)f);

	ipFrame = f->data;

	free(f);
	return ipFrame;
}

// check to see if a transmit frame is queued up
BOOL checkQueue(FRAME_QUEUE *que)
{
	return (que->q_back == que);
}

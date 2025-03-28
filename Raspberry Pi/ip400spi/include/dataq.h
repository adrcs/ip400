/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	Module:		      <module description here>

	File Name:	      queue.h

	Date Created:	  Jan 9, 2025

	Author:			  MartinA

	Description:      <what it does>

					  Copyright © 2024-25, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/

#ifndef INC_DATAQ_H_
#define INC_DATAQ_H_

#ifndef __CCALL
#ifdef __cplusplus
#define __CCALL extern "C"
#else
#define __CCALL
#endif
#endif

#define	Q_TYPECAST	(struct qelem *)

// queue structureBOOL checkQueue(FRAME_QUEUE *que)
typedef struct qelem {
  struct qelem *q_forw;
  struct qelem *q_back;
} QUEUE_ELEM;

// frame data queue
typedef struct frame_data_queue_t {
	struct frame_data_queue_t *q_forw;			// forwared pointer
	struct frame_data_queue_t *q_back;			// backward pointer
	void 					  *data;			// Frame data
	uint16_t				  length;			// data length
} FRAME_QUEUE;

// queue functions
BOOL enqueFrame(FRAME_QUEUE *que, void *fr);
void *dequeFrame(FRAME_QUEUE *que);
BOOL checkQueue(FRAME_QUEUE *que);

// queue management
void insque (struct qelem *elem, struct qelem *pred);
void remque (struct qelem *elem);


#endif /* INC_DATAQ_H_ */

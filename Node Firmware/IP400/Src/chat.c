/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    chat.c

	Author:		    MartinA

	Description:	This code contains the chat application. It gathers key until the CR
					is hit to send a packet. Two internal operations are exit and set the
					destination.

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History:

---------------------------------------------------------------------------*/
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include "setup.h"
#include "frame.h"
#include "types.h"
#include "usart.h"
#include "streambuffer.h"
#include "dataq.h"

#define MAX_KEY		140				// max keys in a buffer
#define	MAX_DEST	20				// max chars in dest callsign
#define	BROADCAST	"FFFF"			// broadcast address

static char keyBuffer[MAX_KEY];		// buffer for keystrokes
uint8_t keyPos;						// current position

#define	KEY_EOL			0x0D		// carriage return
#define KEY_RPT			0x12		// change repeat status
#define	KEY_EXIT		0x1A		// exit key
#define	KEY_ESC			0x1B		// escape key
#define KEY_DEL			0x7F		// delete key
#define	KEY_BKSP		0x08		// backspace key
#define	KEY_DUMP		0x04		// toggle dump mode

char dest_call[MAX_DEST];			// broadcast destination
char *dp = dest_call;				// pointer to dest call characters
char *entCall = "Enter Destination Callsign";
char *rptMode[] = {
		"Repeat mode->off",
		"Repeat mode->on"
};

char *dumpStrings[] = {
		"Dump mode->off",
		"Dump mode->on"
};

char *welcome = "Welcome to chat. ESC to set destination, CTRL/R to toggle repeat, CTRL/Z to exit";

BOOL destEnt = FALSE;				// not entering destination
BOOL repeat = TRUE;					// repeating by default
BOOL deleteMode = FALSE;			// delete mode
BOOL welcomed = FALSE;				// welcome mat is out
BOOL dumpMode = FALSE;

FRAME_QUEUE chatQueue;				// queue for inbound frames

// fwd refs
void sendLine(char *buffer, int len);
void PrintFrame(IP400_FRAME *FrameBytes);

// init entry
void Chat_Task_init(void)
{
	strcpy(dest_call, BROADCAST);
	destEnt = FALSE;
	chatQueue.q_forw = &chatQueue;
	chatQueue.q_back = &chatQueue;
	keyPos = 0;
}

void Chat_Task_welcome(void)
{
	// start with welcome message
	USART_Print_string("%s\r\n", welcome);
	USART_Print_string("%s; ", rptMode[repeat]);

	if(!strcmp(dest_call, BROADCAST))
		USART_Print_string("%s; Destination callsign->(Broadcast)\r\n\n");
	else
		USART_Print_string("%s; Destination callsign->%s\r\n\n", dest_call);

	welcomed = TRUE;
}

/*
 * place a frame on the queue frame content is copied
 * to alloc'd memory
 */
BOOL EnqueChatFrame(void *raw)
{
	IP400_FRAME *qFrame, *SrcFrame = (IP400_FRAME *)raw;
	uint8_t *frameBuffer;

	// if the welcome mat is not out, then discard any pending frames
	if(!welcomed)
		return FALSE;

	// allocate an IP400 frame
	if((qFrame=malloc(sizeof(IP400_FRAME)))== NULL)
		return FALSE;
	memcpy(qFrame, SrcFrame, sizeof(IP400_FRAME));

	// alloc the data portion of the frame
	if((frameBuffer=malloc(SrcFrame->length)) == NULL)	{
		return FALSE;
	}

	memcpy(frameBuffer, (uint8_t *)SrcFrame->buf, SrcFrame->length);
	qFrame->buf = frameBuffer;
	qFrame->length = SrcFrame->length;

	if(!enqueFrame(&chatQueue, qFrame))
		return FALSE;

	return TRUE;
}

/*
 * chat task main entry
 * return: 	TRUE if return to main menu is required
 * 			FALSE: more to go
 */
BOOL Chat_Task_exec(void)
{
	if(!welcomed)
		Chat_Task_welcome();

	char c;
	int nBytesinBuff;
	IP400_FRAME *fr;

	// process any inbound frames first..
	if((fr=dequeFrame(&chatQueue)) != NULL)	{
		PrintFrame(fr);
		free(fr->buf);
		free(fr);
	}

	if((nBytesinBuff=databuffer_bytesInBuffer()) == 0)
		return FALSE;

	for(int i=0;i<nBytesinBuff;i++)		{

		c=databuffer_get(0);

		if(deleteMode)	{
			if((c != KEY_DEL) && (c != KEY_BKSP))	{
				USART_Print_string("\\%c", c);
				if(keyPos < MAX_KEY)
					keyBuffer[keyPos++] = c;
				deleteMode = FALSE;
			} else {
				if(keyPos > 0)	{
					USART_Print_string("%c", keyBuffer[--keyPos]);
				} else {
					USART_Print_string("\\\r\n");
					deleteMode = FALSE;
				}
			}
			continue;
		} else {
		// processing a key

			switch (c)	{

			// CTRL/R: change repeat flag
			case KEY_RPT:
				repeat = repeat ? FALSE : TRUE;
				char *r = rptMode[repeat];
				USART_Print_string("%s\r\n",r);
				break;

			// CTRL/D: change dump mode
			case KEY_DUMP:
				dumpMode = dumpMode ? FALSE : TRUE;
				char *d = dumpStrings[dumpMode];
				USART_Print_string("%s\r\n",d);
				break;

			// escape key: get a destination call sign
			case KEY_ESC:
				if(destEnt)		{
					strcpy(dest_call, BROADCAST);
					USART_Print_string("Destination set to broadcast\r\n");
					destEnt=FALSE;
					break;
				}
				if(keyPos == 0)	{
					USART_Print_string("%s->", entCall);
					dp = dest_call;
					destEnt = TRUE;
				}
				break;

				// EOL key: sent the packet
			case KEY_EOL:
				USART_Print_string("\r\n");
				if(destEnt)	{
					int cpyLen = keyPos > MAX_CALL ? MAX_CALL : keyPos;
					strncpy(dest_call, keyBuffer, cpyLen);
					destEnt = FALSE;
				} else {
					if(keyPos != 0)	{
					keyBuffer[keyPos++] = '\0';
					sendLine(keyBuffer, keyPos);
					} else {
						USART_Print_string(">>>not sent\r\n");
					}
				}
				keyPos = 0;
				break;

			case KEY_EXIT:
				welcomed = FALSE;
				return TRUE;

			case KEY_DEL:
			case KEY_BKSP:
				if(keyPos > 0)	{
					USART_Print_string("\\%c", keyBuffer[--keyPos]);
					deleteMode = TRUE;
				}
				break;

			default:
				USART_Send_Char(c);
				if(keyPos < MAX_KEY)
					keyBuffer[keyPos++] = c;
				break;
			}
		}
	}
	return FALSE;
}

// send a line of text
void sendLine(char *buffer, int len)
{
		SendTextFrame(setup_memory.params.setup_data.stnCall, 0, dest_call, 0, buffer, len, repeat);
}

/*
 * Print a received frame on the console
 */
void PrintFrame(IP400_FRAME *FrameBytes)
{
	char printBuf[250];
	char decCall[100];
	uint16_t port;
	uint16_t dataLen = FrameBytes->length;

	// dump mode for header debugging
	if(dumpMode)	{
	      /* print the received data */
	      USART_Print_string("RX - Data received: [ ");

	      for(uint8_t i=0; i<sizeof(IP400_FRAME); i++)
	        USART_Print_string("%02x ", FrameBytes[i]);

	      USART_Print_string("]\r\n");
	      return;
	}

	// source call
	callDecode(&FrameBytes->source, decCall, &port);
	USART_Print_string("%s(%d)>", decCall, port);

	// dest call
	if((FrameBytes->dest.callbytes.bytes[0] == BROADCAST_ADDR)	 &&
		(FrameBytes->dest.callbytes.bytes[1] == BROADCAST_ADDR)) {
		USART_Print_string("BROADCAST(%d)", FrameBytes->dest.port);
	} else {
		callDecode(&FrameBytes->dest, decCall, &port);
		USART_Print_string("%s(%d)", decCall, port);
	}

	// flags
	USART_Print_string("[%d:%04d]:", FrameBytes->flagfld.flags.hop_count, FrameBytes->seqNum);

	// now dump the data
	memcpy(printBuf, FrameBytes->buf, dataLen);
	printBuf[dataLen] = '\0';
	USART_Print_string("%s\r\n", printBuf);

}

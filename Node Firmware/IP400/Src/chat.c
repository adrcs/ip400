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
#include "ip.h"
#include "utils.h"

#define MAX_KEY			140			// max keys in a buffer
#define	MAX_DEST		20			// max chars in dest callsign
#define	BROADCAST_CALL	"FFFF"		// broadcast address
#define	BROADCAST_IP	0xFFFF		// broadcast IP address

static char keyBuffer[MAX_KEY];		// buffer for keystrokes
uint8_t keyPos;						// current position

#define	KEY_EOL			0x0D		// carriage return
#define KEY_ECHO		0x05		// enter echo mode
#define KEY_RPT			0x12		// change repeat status
#define	KEY_EXIT		0x1A		// exit key
#define	KEY_ESC			0x1B		// escape key
#define KEY_DEL			0x7F		// delete key
#define	KEY_BKSP		0x08		// backspace key
#define	KEY_DUMP		0x04		// toggle dump mode

#define	ECHO_TIME		50			// echo send time

char dest_call[MAX_DEST];			// broadcast destination
uint8_t dest_idx;					// index
uint16_t dest_ip;					// destination IP address
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

// destination MAC's
IP400_MAC	*DestMacs;

// echo modes
enum echoModes {
	ECHO_MODE_OFF=0,				// off
	ECHO_MODE_MANUAL,				// manual
	ECHO_MODE_TIMED,				// timed
	N_ECHO_MODES					// number of modes
};


char *welcome = "Welcome to chat. \r\nESC to set destination, CTRL/R to toggle repeat,\r\nCTRL/E to enter echo mode, CTRL/Z to exit\r\n";

BOOL destEnt = FALSE;				// not entering destination
BOOL repeat = TRUE;					// repeating by default
BOOL deleteMode = FALSE;			// delete mode
BOOL welcomed = FALSE;				// welcome mat is out
BOOL dumpMode = FALSE;				// frame dump mode
uint8_t echoMode = ECHO_MODE_OFF;	// echo mode
uint8_t echoTimer;					// echo mode timer

FRAME_QUEUE chatQueue;				// queue for inbound frames

// fwd refs
void sendLine(char *buffer, int len);
void sendEchoreq(void);
void PrintFrame(IP400_FRAME *FrameBytes);
void ListAllMeshEntries(char *call, int len, int nMACEntries);
BOOL splitCall(char *dest_call, uint8_t *dest_idx, char *keyBuffer, int keyLen, uint8_t *adjLen);
void GetNthMeshentry(char *dest_call, int cpyLen, uint8_t index, int nMACEntries, IP400_MAC **mac);

// init entry
void Chat_Task_init(void)
{
	strcpy(dest_call, BROADCAST_CALL);
	dest_ip = BROADCAST_IP;
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

	if(!strcmp(dest_call, BROADCAST_CALL))
		USART_Print_string("Destination callsign->(Broadcast)\r\n\n");
	else
		USART_Print_string("Destination callsign->%s\r\n\n", dest_call);

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

	if(SrcFrame->flagfld.flags.coding == ECHO_RESPONSE)	{
		PrintFrame(SrcFrame);
		return TRUE;
	}

	// if the welcome mat is not out, then discard any pending frames
	if(!welcomed)
			return FALSE;

	// allocate an IP400 frame
	if((qFrame=malloc(sizeof(IP400_FRAME))) == NULL)
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
	if(!welcomed)	{
		strcpy(dest_call, BROADCAST_CALL);
		dest_ip = BROADCAST_IP;
		echoMode = FALSE;
		Chat_Task_welcome();
	}

	char c;
	int nBytesinBuff;
	int nMACEntries;
	IP400_FRAME *fr;
	IP400_MAC *destMac;
	BOOL hasIndex = FALSE;
	uint8_t cpyLen;

	// process any inbound frames first..
	if((fr=dequeFrame(&chatQueue)) != NULL)	{
		PrintFrame(fr);
		free(fr->buf);
		free(fr);
	}

	if(echoMode == ECHO_MODE_TIMED)		{
		echoTimer++;
		if(echoTimer == ECHO_TIME)	{
			echoTimer = 0;
			sendEchoreq();
			USART_Print_string("Timed echo request sent\r\n");
		}
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

			// CTRL/E: enter echo mode
			case KEY_ECHO:
				echoMode = (echoMode + 1) % N_ECHO_MODES;
				if(echoMode != ECHO_MODE_OFF)		{
					if(!strcmp(dest_call, BROADCAST_CALL))	{
						USART_Print_string("Echo cannot be sent to a broadcast address\r\n");
						echoMode = ECHO_MODE_OFF;
						break;
					}
				}

				// set the echo mode
				switch(echoMode)		{

					case ECHO_MODE_OFF:				// off
						USART_Print_string("Echo mode off\r\n");
						break;

					case ECHO_MODE_MANUAL:
						USART_Print_string("Manual echo mode on\r\n");
						break;

					case ECHO_MODE_TIMED:				// timed
						echoTimer = 0;
						USART_Print_string("Timed echo mode on\r\n");
						break;
				}
				break;

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
				if(keyPos == 0)	{
					USART_Print_string("%s->", entCall);
					dp = dest_call;
					destEnt = TRUE;
				}
				break;

				// EOL key: sent the packet
			case KEY_EOL:
				USART_Print_string("\r\n");
				// in destination entry mode
				if(destEnt)	{
					if(keyPos == 0)		{
						strcpy(dest_call, BROADCAST_CALL);
						dest_ip = BROADCAST_IP;
						USART_Print_string("Destination set to broadcast\r\n");
						destEnt=FALSE;
						break;
					}
					memset(dest_call, 0, MAX_DEST);
					hasIndex = splitCall(dest_call, &dest_idx, keyBuffer, keyPos, &cpyLen);
					nMACEntries=getNMeshEntries(dest_call, cpyLen);

					switch(nMACEntries)		{

					// not found..
					case 0:
						USART_Print_string("Destination address %s not found in Mesh table\r\n", dest_call);
						destEnt = FALSE;
						break;

					// single entry found
					case 1:
						destMac = getMeshEntry(dest_call, cpyLen);
						dest_ip = destMac->vpnBytes.encvpn;
						USART_Print_string("Destination address set to %s\r\n", dest_call);
						destEnt = FALSE;
						break;

					// multiple entries found
					default:
						if(hasIndex)	{
								GetNthMeshentry(dest_call, cpyLen, dest_idx, nMACEntries, &destMac);
								dest_ip = destMac->vpnBytes.encvpn;
						} else	{
							destMac = getMeshEntry(dest_call, cpyLen);
							dest_ip = IP_BROADCAST;
							ListAllMeshEntries(dest_call, cpyLen, nMACEntries);
						}
						destEnt = FALSE;
						break;
					}
				} else {
					if(keyPos != 0)	{
						keyBuffer[keyPos++] = '\0';
						sendLine(keyBuffer, keyPos);
					} else {
						if(!echoMode)		{
							USART_Print_string("Nothing sent\r\n");
						} else {

							if(!strcmp(dest_call, BROADCAST_CALL))	{
								USART_Print_string("Echo cannot be sent to a broadcast address\r\n");
							} else {
								sendEchoreq();
								USART_Print_string("Echo request sent\r\n");
							}
						}
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
				if(destEnt)
					c = isLower(c) ? toupper(c) : c;
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
		SendTextFrame(setup_memory.params.setup_data.stnCall, GetVPNLowerWord(), dest_call, dest_ip, buffer, len, repeat);
}

void sendEchoreq(void)
{
		char buffer[50];

		strcpy(buffer, "Echo request frame");
		int len = strlen(buffer);


		SendEchoReqFrame(setup_memory.params.setup_data.stnCall, GetVPNLowerWord(), dest_call, dest_ip, buffer, len, FALSE);
}

/*
 * List all the mesh entries with a given callsign
 */
void ListAllMeshEntries(char *call, int len, int nMACEntries)
{
	struct ip400MAC_t {
		IP400_MAC *MacEntry;
	};

	struct ip400MAC_t *MacEntries;
	IP400_MAC *macEntry;
	SOCKADDR_IN ipAddr;

	if((MacEntries = (struct ip400MAC_t *) malloc(nMACEntries * sizeof(struct ip400MAC_t))) == NULL)		{
		USART_Print_string("?An error has occurred, cannot allocate mesh table entries\r\n");
		return;
	}

	// announce how many have been found
	USART_Print_string("Call sign %s has %d mesh table entries\r\n", call, nMACEntries);

	// get the entries
	int nEntries=0;
	if((macEntry = getMeshEntry(call, len)) == NULL)	{
		USART_Print_string("?An error has occurred, cannot find any mesh table entries for %s\r\n", call);
		free(MacEntries);
		return;
	}
	MacEntries[nEntries++].MacEntry = macEntry;

	// get the rest of the entries
	while((macEntry = getNextEntry(call, len)) != NULL)
		MacEntries[nEntries++].MacEntry = macEntry;

	// now display them
	for(int i=0;i<nMACEntries;i++)		{
		GetVPNAddrFromMAC(MacEntries[i].MacEntry, &ipAddr);
		USART_Print_string("%s[%d]\t(%d.%d.%d.%d)\r\n",
			call, i+1,
			ipAddr.sin_addr.S_un.S_un_b.s_b1, ipAddr.sin_addr.S_un.S_un_b.s_b2,
			ipAddr.sin_addr.S_un.S_un_b.s_b3, ipAddr.sin_addr.S_un.S_un_b.s_b4);
	}
	USART_Print_string("Destination has been set to all instances\r\n");
	USART_Print_string("For a specific address, use call[subscript] as listed\r\n\n");
	free(MacEntries);
}

// split a callsign x-n into x and n
BOOL splitCall(char *dest_call, uint8_t *dest_idx, char *keyBuffer, int keyLen, uint8_t *adjLen)
{
	for(int i=0;i<keyLen;i++)	{
		if(keyBuffer[i] == '[')	{
			strncpy(dest_call, keyBuffer, i);
			*dest_idx = keyBuffer[i+1] - '0';
			*adjLen = i;
			return TRUE;
		}
	}
	*adjLen = keyLen;
	strncpy(dest_call, keyBuffer, keyLen);
	return FALSE;
}

// get the nth entry
void GetNthMeshentry(char *dest_call, int cpyLen, uint8_t index, int nMACEntries, IP400_MAC **mac)
{
	IP400_MAC *macEntry;
	SOCKADDR_IN ipAddr;
	int nEntries = 0;

	if((index < 0) || (index > nMACEntries))	{
			USART_Print_string("Callsign index is out of range\r\n");
			return;
	}
	if((macEntry = getMeshEntry(dest_call, cpyLen)) == NULL)	{
		USART_Print_string("?An error has occurred, cannot find any mesh table entries for %s\r\n", dest_call);
		return;
	}
	nEntries = 1;
	while(nEntries < index)	{
		if((macEntry = getNextEntry(dest_call, cpyLen)) == NULL)	{
			USART_Print_string("Not enough entries to satisfy index %d\r\n", index);
			return;
		}
		nEntries++;
	}

	// announce the selection
	GetVPNAddrFromMAC(macEntry, &ipAddr);
	USART_Print_string("Destination address set to %s(%d.%d.%d.%d)\r\n\n", dest_call,
			ipAddr.sin_addr.S_un.S_un_b.s_b1, ipAddr.sin_addr.S_un.S_un_b.s_b2,
			ipAddr.sin_addr.S_un.S_un_b.s_b3, ipAddr.sin_addr.S_un.S_un_b.s_b4);

	*mac = macEntry;
}

/*
 * Print a received frame on the console
 */
void PrintFrame(IP400_FRAME *FrameBytes)
{
	char printBuf[250];
	char decCall[100];
	uint16_t dataLen = FrameBytes->length;
	SOCKADDR_IN fromIP;

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
	callDecode(&FrameBytes->source, decCall, NULL);
	GetVPNAddrFromMAC(&FrameBytes->source, &fromIP);

	USART_Print_string("%s(%d.%d.%d.%d) ", decCall,
			fromIP.sin_addr.S_un.S_un_b.s_b1, fromIP.sin_addr.S_un.S_un_b.s_b2,
			fromIP.sin_addr.S_un.S_un_b.s_b3, fromIP.sin_addr.S_un.S_un_b.s_b4);

	// dest call
	if((FrameBytes->dest.callbytes.bytes[0] == BROADCAST_ADDR)	 &&
		(FrameBytes->dest.callbytes.bytes[1] == BROADCAST_ADDR)) {
		USART_Print_string("BROADCAST");
	} else {
		callDecode(&FrameBytes->dest, decCall, NULL);
		GetVPNAddrFromMAC(&FrameBytes->dest, &fromIP);
		USART_Print_string("%s(%d.%d.%d.%d) ", decCall,
				fromIP.sin_addr.S_un.S_un_b.s_b1, fromIP.sin_addr.S_un.S_un_b.s_b2,
				fromIP.sin_addr.S_un.S_un_b.s_b3, fromIP.sin_addr.S_un.S_un_b.s_b4);
	}

	// flags
	USART_Print_string("[%d:%04d]:", FrameBytes->flagfld.flags.hop_count, FrameBytes->seqNum);

	// now dump the data
	if(FrameBytes->flagfld.flags.coding == ECHO_RESPONSE)	{
		USART_Print_string("Echo response\r\n");
	} else {
		memcpy(printBuf, FrameBytes->buf, dataLen);
		printBuf[dataLen] = '\0';
		USART_Print_string("%s\r\n", printBuf);
	}
}

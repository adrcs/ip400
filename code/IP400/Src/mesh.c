/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    mesh.c

	Author:		    MartinA

	Description:	This code builds and lists the mesh status

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
#include <sys/queue.h>
#include <stdint.h>
#include <FreeRTOS.h>
#include <task.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include "tasks.h"
#include "frame.h"
#include "usart.h"
#include "dataq.h"
#include "setup.h"
#include "tod.h"

#define	EXPIRY_TIME		100			// seconds since last heard
#define	RSSI_SCALAR		161			// 95-Gain(rx), appros 65

char *FSKSpeeds[] = {
		"FSK 1200",
		"FSK 9600",
		"FSK_56K",
		"FSK_100K",
		"FSK_200K",
		"FSK_300K",
		"FSK_400K",
		"FSK_600K"
};

// mesh entry struct
struct mesh_entry_t {
	struct mesh_entry_t	*q_forw;		// forward pointer
	struct mesh_entry_t	*q_back;		// backward pointer
	IP400_CALL			encCall;		// encoded callsign
	uint32_t			nextSeq;		// next sequence number
	int16_t				lastRssi;		// signal strength
	uint8_t				txPower;		// transmit power
	SETUP_FLAGS			capabilities;	// capabilities
	TIMEOFDAY			lastHeard;		// last heard
	uint8_t				hopCount;		// hop count
};

/*
 * Since V0.4, the raw rx frame has already been decoded
 * however, the capabilities are in the data buffer
 * this may look cumbersome but it grows if the flags do
 */
BEACON_HEADER mesh_bcn_hdr;			// beacon header
struct mesh_entry_t		meshHead;	// head of mesh chain
int nMeshEntries = 0;
uint32_t seqNum;					// sequence number received

// forward refs in this module
struct mesh_entry_t *findCall(IP400_CALL *call);
void AddMeshEntry(IP400_FRAME *frameData, int16_t rssi, BOOL isBeacon);

// task initialization
void Mesh_Task_Init(void)
{
	meshHead.q_forw = &meshHead;
	meshHead.q_back = &meshHead;
}

// process a beacon packet: if we don't have it, enter it
// otherwise update the last time heard
void Mesh_ProcessBeacon(void *rxFrame, uint32_t rssi)
{
	struct mesh_entry_t *newEntry;
	IP400_FRAME *frameData = (IP400_FRAME *)rxFrame;

	TIMEOFDAY current;
	getTOD(&current);

	int16_t actRSSI = rssi/2 - RSSI_SCALAR;

	// see if we already know about it
	// if so, just update last heard, expected sequence and rssi
	// NB: firt frame is sent with an all '1's sequence number
	if((newEntry = findCall(&frameData->source)) != NULL)	{

		// if it is repeated frame with a higher hop count, ignore it
		if(newEntry->hopCount < frameData->flagfld.flags.hop_count)
			return;

		newEntry->lastHeard.Hours = current.Hours;
		newEntry->lastHeard.Minutes = current.Minutes;
		newEntry->lastHeard.Seconds = current.Seconds;
		newEntry->lastRssi = actRSSI;
		newEntry->nextSeq = frameData->seqNum == 0xFFFFFFFF ? 0 : ++frameData->seqNum;

		//ugly, but necessary
		memset(mesh_bcn_hdr.hdrBytes, 0, sizeof(BEACON_HEADER));
		uint8_t *p = (uint8_t *)frameData->buf;
		mesh_bcn_hdr.hdrBytes[0] = *p++;
		mesh_bcn_hdr.hdrBytes[4] = *p;

		newEntry->capabilities = mesh_bcn_hdr.setup.flags;
		newEntry->txPower = mesh_bcn_hdr.setup.txPower;

		// all done
		return;
	}

	// beacon from a new station
	AddMeshEntry(frameData, actRSSI, TRUE);
}

/*
 * add a new station to the mesh table
 */
void AddMeshEntry(IP400_FRAME *frameData, int16_t actRSSI, BOOL isBeacon)
{
	struct mesh_entry_t *newEntry;

	TIMEOFDAY current;
	getTOD(&current);

	// create a new entry
	if((newEntry = (struct mesh_entry_t *)malloc(sizeof(struct mesh_entry_t))) == NULL) {
		return;
	}

	// grab call and capabilities
	newEntry->encCall = frameData->source;
	newEntry->lastHeard.Hours = current.Hours;
	newEntry->lastHeard.Minutes = current.Minutes;
	newEntry->lastHeard.Seconds = current.Seconds;
	newEntry->nextSeq = frameData->seqNum == 0xFFFFFFFF ? 0 : ++frameData->seqNum;
	newEntry->lastRssi = actRSSI;
	newEntry->txPower = 0;
	newEntry->hopCount = frameData->flagfld.flags.hop_count;

	if(isBeacon)	{
		memcpy(mesh_bcn_hdr.hdrBytes, (struct beacon_hdr_t *)frameData->buf, sizeof(BEACON_HEADER));
		newEntry->capabilities = mesh_bcn_hdr.setup.flags;
		newEntry->txPower = mesh_bcn_hdr.setup.txPower;
	} else {
		memset(&newEntry->capabilities, 0, sizeof(SETUP_FLAGS));
	}

	// insert this at the end of the queue
	insque((struct qelem *)newEntry, (struct qelem *)meshHead.q_back);
	nMeshEntries++;
}

/*
 * reject a duplicate frame that may have been repeated
 * TRUE: keep it, FALSE: reject it
 */
BOOL Mesh_Accept_Frame(void *rxFrame, uint32_t rssi)
{
	IP400_FRAME *frameData = (IP400_FRAME *)rxFrame;
	struct mesh_entry_t *meshEntry;

	if((meshEntry = findCall(&frameData->source)) != NULL)	{
		// rebooted
		if(frameData->seqNum == 0xFFFFFFFF)
			meshEntry->nextSeq = 0;
		// reject if the seq is lower
		if(frameData->seqNum < meshEntry->nextSeq)
			return FALSE;
		meshEntry->nextSeq = frameData->seqNum + 1;
		return TRUE;
	} else {
		// ok to process: but don't know the sender yet, so add him
		int16_t actRSSI = rssi/2 - RSSI_SCALAR;
		AddMeshEntry(frameData, actRSSI, FALSE);
	}
	return TRUE;
}

// find a callsign in the list
struct mesh_entry_t *findCall(IP400_CALL *call)
{
	struct mesh_entry_t *elem = meshHead.q_forw;

	for(int i=0;i<nMeshEntries;i++)	{
		if(elem->encCall.callbytes.encoded == call->callbytes.encoded)
			return elem;
		elem = elem->q_forw;
	}
	return NULL;
}

char capabilties[50];
// return the capabilites of a node
char *GetCapabilities(SETUP_FLAGS cap)
{
	mesh_bcn_hdr.setup.flags = cap;

	if(mesh_bcn_hdr.hdrBytes[0] == 0)	{
		strcpy(capabilties, "Unknown");
	}

	// modes
	if(cap.fsk)		{
		sprintf(capabilties, "%s", FSKSpeeds[cap.rate]);
	}
	else if(cap.ofdm)	{
		strcat(capabilties, " OFDM");
	}
	else if(cap.aredn)	{
		strcat(capabilties, "AREDN");
	}

	// repeat mode is on..
	if(cap.repeat)	{
		strcat(capabilties, " RPT");
	}

	return capabilties;
}

// list the mesh status: walk the mesh entries
void Mesh_ListStatus(void)
{
	USART_Print_string("Stations Heard: %d\r\n", nMeshEntries);
	if(nMeshEntries == 0)
		return;

	// process the list
	struct mesh_entry_t *elem = meshHead.q_forw;
	char decodedCall[20];
	uint16_t port;

	USART_Print_string("Call(Port)\tRSSI\tNext Seq\tLast Heard\tHops\tCapabilities\r\n");

	for(int i=0;i<nMeshEntries;i++)	{
		callDecode(&elem->encCall, decodedCall, &port);
		USART_Print_string("%s(%d)\t%-03d\t%04d\t\t%02d:%02d:%02d\t%d\t%s %d dBm\r\n",
				decodedCall, port,
				elem->lastRssi,
				elem->nextSeq,
				elem->lastHeard.Hours, elem->lastHeard.Minutes, elem->lastHeard.Seconds,
				elem->hopCount,
				GetCapabilities(elem->capabilities), elem->txPower);
		elem = elem->q_forw;
	}
	USART_Print_string("\r\n\n");
}

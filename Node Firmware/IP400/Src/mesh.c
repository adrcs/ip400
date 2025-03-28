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
#include <string.h>
#include <stdio.h>

#include "tasks.h"
#include "frame.h"
#include "usart.h"
#include "dataq.h"
#include "setup.h"
#include "tod.h"
#include "ip.h"

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

// table entry status
typedef enum {
	MESHTBL_UNUSED=0,			//  unused entry
	MESHTBL_VALID,				//  valid entry
	MESHTBL_LOST				//  lost in space
} MeshTableStatus;

// mesh entry struct
typedef struct mesh_entry_t {
	MeshTableStatus		status;			// status
	IP400_CALL			encCall;		// encoded callsign
	uint32_t			nextSeq;		// next sequence number
	int16_t				lastRssi;		// signal strength
	uint8_t				txPower;		// transmit power
	SETUP_FLAGS			capabilities;	// capabilities
	TIMEOFDAY			lastHeard;		// last heard
	uint8_t				hopCount;		// hop count
	uint32_t			ipAddress;		// IP address
} MESH_ENTRY;

// mesh table definitions
#define	MAX_MESH_MEMORY		2048			// max amount of mesh memory used
#define	MAX_MESH_ENTRIES	(MAX_MESH_MEMORY/sizeof(MESH_ENTRY))
#define	ENTRY_NOTFOUND		-1				// not found in the table

// table size is limited to by memory defintion
static MESH_ENTRY MeshTable[MAX_MESH_ENTRIES] __attribute__((section("MESHTABLE")));
int nMeshEntries = 0;

BEACON_HEADER 		mesh_bcn_hdr;			// beacon header
uint32_t 			seqNum;					// sequence number received

// forward refs in this module
int findCall(IP400_CALL *call);
void AddMeshEntry(IP400_FRAME *frameData, int16_t rssi, BOOL isBeacon);

// task initialization
void Mesh_Task_Init(void)
{
	for(int i=0;i<MAX_MESH_ENTRIES;i++)
		MeshTable[i].status = MESHTBL_UNUSED;
}

// process a beacon packet: if we don't have it, enter it
// otherwise update the last time heard
void Mesh_ProcessBeacon(void *rxFrame, uint32_t rssi)
{
	IP400_FRAME *frameData = (IP400_FRAME *)rxFrame;

	TIMEOFDAY current;
	getTOD(&current);

	int16_t actRSSI = rssi/2 - RSSI_SCALAR;
	int entryNum;

	// see if we already know about it
	// if so, just update last heard, expected sequence and rssi
	// NB: firt frame is sent with an all '1's sequence number
	if((entryNum = findCall(&frameData->source)) != ENTRY_NOTFOUND)	{

		// if it is repeated frame with a higher hop count, ignore it
		if(MeshTable[entryNum].hopCount < frameData->flagfld.flags.hop_count)
			return;

		MeshTable[entryNum].lastHeard.Hours = current.Hours;
		MeshTable[entryNum].lastHeard.Minutes = current.Minutes;
		MeshTable[entryNum].lastHeard.Seconds = current.Seconds;
		MeshTable[entryNum].lastRssi = actRSSI;
		MeshTable[entryNum].nextSeq = frameData->seqNum == 0xFFFFFFFF ? 0 : ++frameData->seqNum;

		//ugly, but necessary
		memset(mesh_bcn_hdr.hdrBytes, 0, sizeof(BEACON_HEADER));
		uint8_t *p = (uint8_t *)frameData->buf;
		mesh_bcn_hdr.hdrBytes[0] = *p++;
		mesh_bcn_hdr.hdrBytes[4] = *p;

		MeshTable[entryNum].capabilities = mesh_bcn_hdr.setup.flags;
		MeshTable[entryNum].txPower = mesh_bcn_hdr.setup.txPower;

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
	// check for room first
	if(nMeshEntries >= MAX_MESH_ENTRIES)
		return;

	MESH_ENTRY newEntry;

	TIMEOFDAY current;
	getTOD(&current);
	SOCKADDR_IN ipAddr;

	// grab call and capabilities
	newEntry.encCall = frameData->source;
	newEntry.lastHeard.Hours = current.Hours;
	newEntry.lastHeard.Minutes = current.Minutes;
	newEntry.lastHeard.Seconds = current.Seconds;
	newEntry.nextSeq = frameData->seqNum == 0xFFFFFFFF ? 0 : ++frameData->seqNum;
	newEntry.lastRssi = actRSSI;
	newEntry.txPower = 0;
	newEntry.hopCount = frameData->flagfld.flags.hop_count;

	GetIPAddr(&newEntry.encCall, &ipAddr);
	newEntry.ipAddress = ipAddr.sin_addr.S_un.S_addr;

	if(isBeacon)	{
		memcpy(mesh_bcn_hdr.hdrBytes, (struct beacon_hdr_t *)frameData->buf, sizeof(BEACON_HEADER));
		newEntry.capabilities = mesh_bcn_hdr.setup.flags;
		newEntry.txPower = mesh_bcn_hdr.setup.txPower;
	} else {
		memset(&newEntry.capabilities, 0, sizeof(SETUP_FLAGS));
	}

	// insert this at the end of the queue
	newEntry.status = MESHTBL_VALID;
	memcpy(&MeshTable[nMeshEntries++], &newEntry, sizeof(MESH_ENTRY));
}

/*
 * reject a duplicate frame that may have been repeated
 * TRUE: keep it, FALSE: reject it
 */
BOOL Check_Sender_Address(void *rxFrame, uint32_t rssi)
{
	IP400_FRAME *frameData = (IP400_FRAME *)rxFrame;
	int entryNum;

	if((entryNum = findCall(&frameData->source)) != ENTRY_NOTFOUND)	{
		// rebooted
		if(frameData->seqNum == 0xFFFFFFFF)
			MeshTable[entryNum].nextSeq = 0;
		// reject if the seq is lower
		if(frameData->seqNum < MeshTable[entryNum].nextSeq)
			return FALSE;
		MeshTable[entryNum].nextSeq = frameData->seqNum + 1;
		return TRUE;
	}

	// sender is unknown: add him for now..
	int16_t actRSSI = rssi/2 - RSSI_SCALAR;
	AddMeshEntry(frameData, actRSSI, FALSE);
	return TRUE;
}

/*
 * Check if the frame can be accepted.
 * Accept a broadcast or my address if:
 * 	-the sender is not in the mesh table
 * 	-the sequence is not out of order
 * 	Else reject it
 */
BOOL Mesh_Accept_Frame(void *rxFrame, uint32_t rssi)
{
	IP400_FRAME *frameData = (IP400_FRAME *)rxFrame;
	char decCall[30];
	uint16_t port;;

	// frame is sent a broadcast address
	if((frameData->dest.callbytes.bytes[0] == BROADCAST_ADDR)
		&&	(frameData->dest.callbytes.bytes[0] == BROADCAST_ADDR))
		return Check_Sender_Address(rxFrame, rssi);

	callDecode(&frameData->dest, decCall, &port);
	if(CompareToMyCall(decCall))
		return Check_Sender_Address(rxFrame, rssi);

	// not for me
	return FALSE;
}

// find a callsign in the list
int findCall(IP400_CALL *call)
{
	for(int i=0;i<nMeshEntries;i++)	{
		if(MeshTable[i].encCall.callbytes.encoded == call->callbytes.encoded)
			return i;
	}
	return ENTRY_NOTFOUND;
}

char capabilties[50];
// return the capabilities of a node
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
	char decodedCall[20];
	uint16_t port;
	SOCKADDR_IN ipAddr;

	USART_Print_string("Call(Port)\tIP Addr\t\tRSSI\tNext Seq\tLast Heard\tHops\tCapabilities\r\n");

	for(int i=0;i<nMeshEntries;i++)	{
		if(MeshTable[i].status == MESHTBL_VALID)		{

			callDecode(&MeshTable[i].encCall, decodedCall, &port);
			GetIPAddr(&MeshTable[i].encCall, &ipAddr);

			USART_Print_string("%s(%d)\t%d.%d.%d.%d\t%-03d\t%04d\t\t%02d:%02d:%02d\t%d\t%s %d dBm\r\n",
					decodedCall, port,
					ipAddr.sin_addr.S_un.S_un_b.s_b1, ipAddr.sin_addr.S_un.S_un_b.s_b2,
					ipAddr.sin_addr.S_un.S_un_b.s_b3, ipAddr.sin_addr.S_un.S_un_b.s_b4,
					MeshTable[i].lastRssi,
					MeshTable[i].nextSeq,
					MeshTable[i].lastHeard.Hours, MeshTable[i].lastHeard.Minutes, MeshTable[i].lastHeard.Seconds,
					MeshTable[i].hopCount,
					GetCapabilities(MeshTable[i].capabilities), MeshTable[i].txPower);
		}
		USART_Print_string("\r\n\n");
	}
}

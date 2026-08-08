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

#include "frame.h"
#include "usart.h"
#include "dataq.h"
#include "setup.h"
#include "tod.h"
#include "ip.h"

#include "tasks.h"

#define	EXPIRY_TIME		100			// seconds since last heard
#define	RSSI_SCALAR		161			// 95-Gain(rx), appros 65

// table entry status
typedef enum {
	MESHTBL_UNUSED=0,			//  unused entry
	MESHTBL_VALID,				//  valid entry
	MESHTBL_LOST				//  lost in space
} MeshTableStatus;

// mesh entry struct
typedef struct mesh_entry_t {
	MeshTableStatus		status;			// status
	IP400_MAC			macAddr;		// MAC address/IP Address
	IP400_FLAGS			flags;			// flags (mostly for OFDM)
	uint32_t			nextSeq;		// next sequence number
	int16_t				lastRssi;		// signal strength
	TIMEOFDAY			bcnTime;		// timestamp of last beacon
	BEACON_HEADER		beacon;			// last beacon message header
} MESH_ENTRY;

// time constants
#define	MAX_MISSING		(30*60)			// 30 minute max elapsed time
#define MAX_LOST		(60*60)			// one hour to gone...

// mesh table definitions
#define	MAX_MESH_MEMORY		2048			// max amount of mesh memory used
#define	MAX_MESH_ENTRIES	(MAX_MESH_MEMORY/sizeof(MESH_ENTRY))
#define	ENTRY_NOTFOUND		-1				// not found in the table

// table size is limited to by memory defintion
static MESH_ENTRY MeshTable[MAX_MESH_ENTRIES] __attribute__((section("MESHTABLE")));
int nMeshEntries = 0;
int lastEntryNum = 0;

BEACON_HEADER 		mesh_bcn_hdr;			// beacon header
uint32_t 			seqNum;					// sequence number received

// forward refs in this module
int findCall(IP400_MAC *call, int start);
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
	// NB: first frame is sent with an all '1's sequence number
	if((entryNum = findCall(&frameData->source, 0)) != ENTRY_NOTFOUND)	{

		// if it is repeated frame with a higher hop count, ignore it
		MeshTable[entryNum].bcnTime.Hours = current.Hours;
		MeshTable[entryNum].bcnTime.Minutes = current.Minutes;
		MeshTable[entryNum].bcnTime.Seconds = current.Seconds;
		MeshTable[entryNum].lastRssi = actRSSI;
		MeshTable[entryNum].nextSeq = frameData->seqNum == 0xFFFFFFFF ? 0 : ++frameData->seqNum;

		//ugly, but necessary
		memset(mesh_bcn_hdr.hdrBytes, 0, sizeof(BEACON_HEADER));
		buf2hdr(&mesh_bcn_hdr, frameData->buf);
		memcpy(&MeshTable[entryNum].beacon, &mesh_bcn_hdr, sizeof(BEACON_HEADER));

		// all done: change status to OK
		MeshTable[entryNum].status = MESHTBL_VALID;
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
	newEntry.macAddr = frameData->source;
	newEntry.bcnTime.Hours = current.Hours;
	newEntry.bcnTime.Minutes = current.Minutes;
	newEntry.bcnTime.Seconds = current.Seconds;
	newEntry.nextSeq = frameData->seqNum == 0xFFFFFFFF ? 0 : frameData->seqNum + 1;
	newEntry.lastRssi = actRSSI;
	newEntry.flags = frameData->flagfld.flags;

	GetVPNAddrFromMAC(&newEntry.macAddr, &ipAddr);

	if(isBeacon)	{
		buf2hdr(&mesh_bcn_hdr, frameData->buf);
		memcpy(&newEntry.beacon, &mesh_bcn_hdr, sizeof(BEACON_HEADER));
	} else {
		memset(&newEntry.beacon, 0, sizeof(BEACON_HEADER));
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

	if((entryNum = findCall(&frameData->source, 0)) != ENTRY_NOTFOUND)	{
		// rebooted
		if(frameData->seqNum == 0xFFFFFFFF)	{
			MeshTable[entryNum].nextSeq = 0;
			return TRUE;
		}

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
 * Update the status based on last heard
 */
void UpdateMeshStatus(void)
{
	int timeSinceLastBeacon;

	for(int i=0;i<nMeshEntries;i++)	{

		switch(MeshTable[i].status)	{

		case MESHTBL_UNUSED:
			break;

		case MESHTBL_VALID:
			timeSinceLastBeacon = getElapsed(&MeshTable[i].bcnTime);
			if(timeSinceLastBeacon > MAX_MISSING)
				MeshTable[i].status = MESHTBL_LOST;
			break;

		case MESHTBL_LOST:
			timeSinceLastBeacon = getElapsed(&MeshTable[i].bcnTime);
			if(timeSinceLastBeacon > MAX_LOST)
				MeshTable[i].status = MESHTBL_UNUSED;

		}
	}
}

/*
 * Check if the frame can be accepted.
 *	There are four variants:
 *	1)	Broadcast
 *	2)  All Call
 *	3)  AX25 compatible
 *	4)  My VPN address
 */
BOOL Mesh_Accept_Frame(void *rxFrame, uint32_t rssi)
{
	IP400_FRAME *frameData = (IP400_FRAME *)rxFrame;
	char decCall[30];
	uint16_t vpnAddr;

	uint16_t myIPAddr = GetVPNLowerWord();

	// 1) first variant: broadcast to all stations: accept all
	if((frameData->dest.callbytes.callsign.bytes[0] == BROADCAST_ADDR)
		&&	(frameData->dest.callbytes.callsign.bytes[1] == BROADCAST_ADDR))
		return TRUE;

	// To continue, unpack the callsign and compare first BOOL callDecode(IP400_FRAME *frame, char *callsign, uint16_t *ipAddr, CallSignSource source)
	callDecode(frameData, decCall, &vpnAddr, DEST_CALLSIGN);
	if(CompareToMyCall(decCall)) {

		// 3) AX25 Compatible
		if(isAX25Enabled())		{
			if(frameData->dest.vpnBytes.ax25vpn.marker == BROADCAST_ADDR)	{
				if(frameData->dest.vpnBytes.ax25vpn.ssid == getAX25SSID())
					return TRUE;
			}
		}
		// 2) All Call
		if(frameData->dest.vpnBytes.encvpn == IP_BROADCAST)
			return TRUE;

		// 4) VPN Address maches..
		else if(frameData->dest.vpnBytes.encvpn == myIPAddr)
			return Check_Sender_Address(rxFrame, rssi);
	}

	// not for me
	return FALSE;
}

// encode a callsign: ensure length is correct
uint32_t EncodeCallSign(char *call, int len)
{
	IP400_FRAME fr;
	uint16_t ipAddr =  0;

	callEncode(call, ipAddr, &fr, SRC_CALLSIGN);

	return fr.source.callbytes.callsign.encoded;

}

// compare the IP addresses. FFFF is considered
// a broadcast to all with the same callsign
BOOL ipCompare(uint16_t tblent, uint16_t compareto)
{
	if(compareto == IP_BROADCAST)
		return TRUE;

	if(tblent == compareto)
		return TRUE;

	// AX,25 compatibility enables SSID instead of VPN address
#if __AX25_COMPATIBILITY
	if(((tblent&AX25_SSID_MASK) == AX25_VPN_BASE) && ((tblent&0xF) == setup_memory.params.setup_data.flags.SSID))
		return TRUE;
#endif

	return FALSE;
}

// find a callsign in the list
int findCall(IP400_MAC *call, int start)
{
	for(int i=start;i<nMeshEntries;i++)	{
		if((MeshTable[i].macAddr.callbytes.callsign.encoded == call->callbytes.callsign.encoded)
		    && ipCompare(MeshTable[i].macAddr.vpnBytes.encvpn, call->vpnBytes.encvpn)
			&& ((MeshTable[i].status == MESHTBL_VALID) || (MeshTable[i].status == MESHTBL_LOST)))
			return i;
	}
	return ENTRY_NOTFOUND;
}

int getNMeshEntries(char *dest_call, int len)
{
	IP400_MAC encoded;
	int nEntries = 0;

	encoded.callbytes.callsign.encoded = EncodeCallSign(dest_call, len);
	for(int i=0;i<nMeshEntries;i++)	{
		if((MeshTable[i].macAddr.callbytes.callsign.encoded == encoded.callbytes.callsign.encoded) && (MeshTable[i].status == MESHTBL_VALID))
			nEntries++;
	}
	return nEntries;
}

// return any MAC entry for a callsign
IP400_MAC *getMeshEntry(char *dest_call, int len)
{
	IP400_MAC encoded;

	encoded.callbytes.callsign.encoded = EncodeCallSign(dest_call, len);
	encoded.vpnBytes.encvpn = IP_BROADCAST;
	if((lastEntryNum=findCall(&encoded, 0)) == ENTRY_NOTFOUND)
		return NULL;

	return(&MeshTable[lastEntryNum].macAddr);
}

// find the next similar entry
IP400_MAC *getNextEntry(char *dest_call, int len)
{
	IP400_MAC encoded;

	// last was not found
	if(lastEntryNum == ENTRY_NOTFOUND)
		return NULL;

	// fell off the end?
	if(++lastEntryNum >= nMeshEntries)
		return NULL;

	// keep looking
	encoded.callbytes.callsign.encoded = EncodeCallSign(dest_call, len);
	encoded.vpnBytes.encvpn = IP_BROADCAST;

	if((lastEntryNum=findCall(&encoded, lastEntryNum)) == ENTRY_NOTFOUND)
		return NULL;

	return(&MeshTable[lastEntryNum].macAddr);

}


char capabilties[50];
// return the capabilities of a node
char *GetCapabilities(SETUP_FLAGS cap, IP400_FLAGS flags)
{
	mesh_bcn_hdr.setup.flags = cap;
	char tmpBuf[50];

	if(mesh_bcn_hdr.hdrBytes[0] == 0)	{
		strcpy(capabilties, "Unknown");
	}

	// modes
	if(cap.fsk)		{
		sprintf(capabilties, "FSK ");
	}
	else if(cap.ofdm)	{
		strcat(capabilties, " OFDM[");
#if __XCVR_OFDM_AB
		strcat(capabilties, ofdm_ab_bandwidth[flags.bandwidth]);
		strcat(capabilties, ",");
		strcat(capabilties, ofdm_ab_modTypes[flags.bitsperCarr]);
		strcat(capabilties, "]");
#endif
	}

	if(cap.AX25)	{
		sprintf(tmpBuf, " AX.25 SSID %d", cap.SSID);
		strcat(capabilties, tmpBuf);
	}


	// repeat mode is on..
	if(cap.repeat)	{
		strcat(capabilties, " RPT");
	}

	return capabilties;
}


// remove spaces in callsign string
void trim(char *string)
{
	while(*string)	{
		if(*string == ' ')
			*string = '\0';
		string++;
	}
}

// list the mesh status: walk the mesh entries
void Mesh_ListStatus(void)
{
	USART_Print_string("Nodes Heard: %d\r\n", nMeshEntries);
	if(nMeshEntries == 0)
		return;

	// process the list
	char decodedCall[20];
	SOCKADDR_IN ipAddr;

	USART_Print_string("Call\tVPN Addr\tStatus\tRSSI\tSeq\tLast Heard\tCapabilities\r\n");

	for(int i=0;i<nMeshEntries;i++)	{

		IP400_FRAME fr;

		switch(MeshTable[i].status)		{

		case MESHTBL_UNUSED:
			break;

		case MESHTBL_VALID:
			fr.source.callbytes.callsign.encoded = MeshTable[i].macAddr.callbytes.callsign.encoded;
			callDecode(&fr, decodedCall, NULL, SRC_CALLSIGN);
			trim(decodedCall);
			GetVPNAddrFromMAC(&MeshTable[i].macAddr, &ipAddr);

			USART_Print_string("%s\t%d.%d.%d.%d\tOK\t%-03d\t%04d\t%02d:%02d:%02d\t%s %d dBm\r\n",
					decodedCall,
					ipAddr.sin_addr.S_un.S_un_b.s_b1, ipAddr.sin_addr.S_un.S_un_b.s_b2,
					ipAddr.sin_addr.S_un.S_un_b.s_b3, ipAddr.sin_addr.S_un.S_un_b.s_b4,
					MeshTable[i].lastRssi,
					MeshTable[i].nextSeq,
					MeshTable[i].bcnTime.Hours, MeshTable[i].bcnTime.Minutes, MeshTable[i].bcnTime.Seconds,
					GetCapabilities(MeshTable[i].beacon.setup.flags, MeshTable[i].flags),
					/*
					 * KLUDGE WARNING: need to modify for all transceiver entries
					 */
					MeshTable[i].beacon.setup.radios[0].txPower);
			break;

		case MESHTBL_LOST:
			fr.source.callbytes.callsign.encoded = MeshTable[i].macAddr.callbytes.callsign.encoded;
			callDecode(&fr, decodedCall, NULL, SRC_CALLSIGN);
			GetVPNAddrFromMAC(&MeshTable[i].macAddr, &ipAddr);

			USART_Print_string("%s\t%d.%d.%d.%d\tLOST\r\n",
					decodedCall,
					ipAddr.sin_addr.S_un.S_un_b.s_b1, ipAddr.sin_addr.S_un.S_un_b.s_b2,
					ipAddr.sin_addr.S_un.S_un_b.s_b3, ipAddr.sin_addr.S_un.S_un_b.s_b4
					);
			break;
		}
	}
	USART_Print_string("\r\n\n");
}

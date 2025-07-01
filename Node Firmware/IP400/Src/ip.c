/*---------------------------------------------------------------------------
	Project:	      IP400

	Module:		      Calculate IP address for a node

	File Name:	      ip.c

	Author:		      MartinA

	Creation Date:	  Mar 3, 2025

	Description:      Calculate the IP address from an IP400 Frame structure

					  This program is free software: you can redistribute it and/or modify
					  it under the terms of the GNU General Public License as published by
					  the Free Software Foundation, either version 2 of the License, or
					  (at your option) any later version, provided this copyright notice
					  is included.

					  Copyright (c) Alberta Digital Radio Communications Society
					  All rights reserved.


	Revision History:

---------------------------------------------------------------------------*/
#include "types.h"
#include "frame.h"
#include "ip.h"
#include "setup.h"

#define		AF_INET				2				// internet address family
#define		IP_10_NETWORK		10				// ip network address
#define		IP_172_NETWORK		172				// alt network address
#define		IP_172_START		16				// start of 172 range

uint8_t		netmask10[] = {
		0xFF, 0xFF, 0xFF, 0xF8
};

uint8_t		netmask172[] = {
		0xFF, 0x30, 0xFF, 0xFF
};

// unique ID
union {
	uint8_t 	bytes[4];
	uint16_t 	halfwords[2];
	uint32_t 	word;
} uniqueID;

#define		NETMASK			0xF8				// mask for last byte

/*
 * Create an IP 172.16.x.x from the compressed callsign field and unique ID
 */
void Get172AddrFromID(IP400_MAC *fr, SOCKADDR_IN *ipaddr)
{
	ipaddr->sin_family = AF_INET;
	ipaddr->sin_addr.S_un.S_addr = 0;

	uniqueID.word = GetDevID0() ^ GetDevID1();

	// first is fixed
	ipaddr->sin_addr.S_un.S_un_b.s_b1 = IP_172_NETWORK & netmask172[0];

	// second byte from the callsign data
	uint8_t b3 = (fr->callbytes.bytes[0] ^ fr->callbytes.bytes[2]) & netmask172[2];
	uint8_t b4 = (fr->callbytes.bytes[1] ^ fr->callbytes.bytes[3]) & netmask172[3];
	uint8_t b2 = (b3 + b4) & 0xf;
	ipaddr->sin_addr.S_un.S_un_b.s_b2 = b2 + IP_172_START;


	// remainder from the unique ID
	ipaddr->sin_addr.S_un.S_un_b.s_b3 = uniqueID.bytes[1];
	ipaddr->sin_addr.S_un.S_un_b.s_b4 = uniqueID.bytes[0];

	// port number from source
	ipaddr->sin_port = 0;
}

/*
 * return the IP Address in an IP_400 MAC
 */
void GetVPNAddrFromMAC(IP400_MAC *fr, SOCKADDR_IN *ipAddr)
{
	ipAddr->sin_family = AF_INET;
	ipAddr->sin_addr.S_un.S_addr = 0;

	// first is fixed
	ipAddr->sin_addr.S_un.S_un_b.s_b1 = IP_172_NETWORK & netmask172[0];

	// second byte from the callsign data
	uint8_t b3 = (fr->callbytes.bytes[0] ^ fr->callbytes.bytes[2]) & netmask172[2];
	uint8_t b4 = (fr->callbytes.bytes[1] ^ fr->callbytes.bytes[3]) & netmask172[3];
	uint8_t b2 = (b3 + b4) & 0xf;
	ipAddr->sin_addr.S_un.S_un_b.s_b2 = b2 + IP_172_START;

	// remainder from the ip Address field
	ipAddr->sin_addr.S_un.S_un_b.s_b3 = fr->vpnBytes.vpn[1];
	ipAddr->sin_addr.S_un.S_un_b.s_b4 = fr->vpnBytes.vpn[0];

	// port number from source
	ipAddr->sin_port = 0;


}

uint16_t GetVPNLowerWord(void)
{
	uniqueID.word = GetDevID0() ^ GetDevID1();

	return uniqueID.halfwords[0];
}



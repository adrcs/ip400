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

#define		NETMASK			0xF8				// mask for last byte

// return the IP address
void GetIP10Addr(IP400_CALL *fr, SOCKADDR_IN *ipaddr)
{
	ipaddr->sin_family = AF_INET;
	
	// first byte is fixed
	ipaddr->sin_addr.S_un.S_un_b.s_b1 = IP_10_NETWORK & netmask10[0];

	// next two are sums of callsign bytes
	ipaddr->sin_addr.S_un.S_un_b.s_b2 = (fr->callbytes.bytes[0] ^ fr->callbytes.bytes[2]) & netmask10[1];
	ipaddr->sin_addr.S_un.S_un_b.s_b3 = (fr->callbytes.bytes[1] ^ fr->callbytes.bytes[3]) & netmask10[2];
	
	// last digit from sum
	int macsum = 0;
	for (int i = 0; i < N_CALL; i++)
		macsum += fr->callbytes.bytes[i];
	ipaddr->sin_addr.S_un.S_un_b.s_b4 = (uint8_t)(macsum & netmask10[3]);

	// port number from source
	ipaddr->sin_port = fr->port;
}

void GetIP172Addr(IP400_CALL *fr, SOCKADDR_IN *ipaddr)
{
	ipaddr->sin_family = AF_INET;
	ipaddr->sin_addr.S_un.S_addr = 0;

	// first byte is fixed
	ipaddr->sin_addr.S_un.S_un_b.s_b1 = IP_172_NETWORK & netmask172[0];

	// next two are sums of callsign bytes
	uint8_t b3 = (fr->callbytes.bytes[0] ^ fr->callbytes.bytes[2]) & netmask172[2];
	uint8_t b4 = (fr->callbytes.bytes[1] ^ fr->callbytes.bytes[3]) & netmask172[3];
	uint8_t b2 = (b3 + b4) & 0xf;

	// compose the address
	ipaddr->sin_addr.S_un.S_un_b.s_b2 = b2 + IP_172_START;
	ipaddr->sin_addr.S_un.S_un_b.s_b3 = b3;
	ipaddr->sin_addr.S_un.S_un_b.s_b4 = b4;

	// port number from source
	ipaddr->sin_port = fr->port;
}

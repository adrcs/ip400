/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      ip.h

	Author:		      MartinA

	Creation Date:	  Jan 26, 2025

	Description:	  Definitions for IP addressing

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/

#ifndef INC_IP_H_
#define INC_IP_H_

#include <stdint.h>

#include "types.h"
#include "config.h"

/* If your port already typedef's sa_family_t, define SA_FAMILY_T_DEFINED
   to prevent this code from redefining it. */
#if !defined(sa_family_t) && !defined(SA_FAMILY_T_DEFINED)
typedef uint8_t sa_family_t;
#endif
/* If your port already typedef's in_port_t, define IN_PORT_T_DEFINED
   to prevent this code from redefining it. */
#if !defined(in_port_t) && !defined(IN_PORT_T_DEFINED)
typedef uint16_t in_port_t;
#endif

// in_addr struct
struct in_addr_t	{
   union {
      struct { uint8_t s_b1,s_b2,s_b3,s_b4; } S_un_b;
      struct { uint16_t s_w1,s_w2; } S_un_w;
      uint32_t S_addr;
   } S_un;
};
typedef struct in_addr_t IN_ADDR;

/* members are in network byte order */
struct sockaddr_in {
  uint8_t         sin_len;
  sa_family_t     sin_family;
  in_port_t       sin_port;
  IN_ADDR		  sin_addr;
#define SIN_ZERO_LEN 8
  char            sin_zero[SIN_ZERO_LEN];
};

typedef struct sockaddr_in SOCKADDR_IN;

// convert packed frame to IP address
void GetIP10Addr(IP400_CALL *fr, SOCKADDR_IN *ipaddr);
void GetIP172Addr(IP400_CALL *fr, SOCKADDR_IN *ipaddr);

#if __IP_GROUP == IP_10_GROUP
#define GetIPAddr	GetIP10Add
#else
#define GetIPAddr   GetIP172Addr
#endif

#endif /* INC_IP_H_ */

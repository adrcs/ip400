/*---------------------------------------------------------------------------
	Project:	      Ip400Spi

	Module:		      Type definitions

	File Name:	      types.h

	Date Created:	  Jan 9, 2025

	Author:			  MartinA

	Description:      Useful data types

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society


	Revision History:

---------------------------------------------------------------------------*/

#ifndef INC_TYPES_H_
#define INC_TYPES_H_

#include <stdint.h>
#include <stddef.h>

// boolean type
typedef	uint8_t	BOOL;			// boolean
typedef uint8_t BOOLEAN;		// alternate
typedef uint8_t boolean;		// another alternate

#ifndef TRUE
#define TRUE  ((BOOL)1U)
#endif

#ifndef FALSE
#define FALSE ((BOOL)0U)
#endif

#ifndef __CCALL
#ifdef __cplusplus
#define __CCALL extern "C"
#else
#define __CCALL
#endif
#endif

// macros
#define		islower(c)		((c >= 'a') && (c <= 'z'))
#define		toupper(c)		(c-0x20)

BOOL setup_udp_socket(char *hostname, int hostport, int localport);
BOOL send_udp_packet(void *data, uint16_t length);
void close_udp_socket(void);

void send_IP400Frame(uint8_t frametype, int seq);

char *geterrno(int errnum);


#endif /* INC_TYPES_H_ */

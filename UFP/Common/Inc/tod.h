/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      tod.h

	Author:		      MartinA

	Creation Date:	  Jan 23, 2025

	Description:	  <decription here?

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_TOD_H_
#define INC_TOD_H_

#include <stdint.h>

// TOD struct
typedef struct tod_t {
	uint8_t		Seconds;
	uint8_t		Minutes;
	uint8_t		Hours;
} TIMEOFDAY;

// links in
void  TOD_10SecTimer(void);				// 10 second timer
void  getTOD(TIMEOFDAY *time);
BOOL  setTOD(char *todString);

int getElapsed(TIMEOFDAY *time);

#endif /* INC_TOD_H_ */

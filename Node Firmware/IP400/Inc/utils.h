/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      utils.h

	Author:		      MartinA

	Creation Date:	  Jan 20, 2025

	Description:	  Utility routines

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_UTILS_H_
#define INC_UTILS_H_

/*
 * unsigned conversion routines
 */
uint8_t A2_uint8_t(char *s);
uint16_t A2_uint16_t(char *s);
uint32_t A2_uint32_t(char *s);

/*
 * Signed integer
 */
// replacement for missing itoa
int ascii2Dec(char *dec);

/*
 * Signed double
 */
// and its double counterpart...
double ascii2double(char *val);

// hex to ascii
void hex2ascii(uint8_t hex, char *buf);

// check an entry for floating point
BOOL isfloat(char *val);

// case/numeric checkers
BOOL isUpper(char c);
BOOL isLower(char c);
BOOL isNumeric(char c);

// useful in parsing NMEA sentences
int explode_string(char *str, char *strp[], int limit, char delim, char quote);


#endif /* INC_UTILS_H_ */

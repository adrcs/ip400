/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    utils.c

	Author:		    MartinA

	Description:	utility routines

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History:

---------------------------------------------------------------------------*/
#include <math.h>

#include "types.h"
#include "utils.h"

/*
 * Unsigned conversion routines
 */
// convert an ascii string to a uint8
uint8_t A2_uint8_t(char *s)
{
    uint8_t val = 0, newval;

    while(*s)   {
        newval = (uint8_t)(val << 3);              //*8
        newval += val << 1;             //*2 + *8  = *10
        val = newval + (*s++ - '0');    // *10 + new value
    }
    return val;
}

// convert an ascii string to a uint16
uint16_t A2_uint16_t(char *s)
{
    uint16_t val = 0, newval;

    while(*s)   {
        newval = val << 3;              //*8
        newval += val << 1;             //*2 + *8  = *10
        val = newval + (*s++ - '0');    // *10 + new value
    }
    return val;
}

// convert an ascii string to a uint32
// same code, wider int
uint32_t A2_uint32_t(char *s)
{
    uint32_t val = 0, newval;

    while(*s)   {
        newval = val << 3;              //*8
        newval += val << 1;             //*2 + *8  = *10
        val = newval + (*s++ - '0');    // *10 + new value
    }
    return val;
}

/*
 * Signed integer conversion
 */
// ascii to signed decimal
int ascii2Dec(char *dec)
{
	int retval = 0;
	int sgn = 1;
	while (*dec)	{
		if(*dec == '-')
			sgn = -1;
		else retval = retval*10 + (*dec - '0');
		dec++;
	}

	return retval*sgn;
}

// nibble to ascii
char nib2ascii(uint8_t nib)
{
	char anib = (char)(nib + '0');
	anib = (anib > '9') ? anib+('A'-'9') : anib;
	return anib;
}

// hex to ascii
void hex2ascii(uint8_t hex, char *buf)
{
	*buf++= nib2ascii(hex>>4);
	*buf = nib2ascii(hex & 0xf);
}

// see if an entry is floating point
BOOL isfloat(char *val)
{
	while(*val)
		if(*val++ == '.')
			return 1U;
	return 0U;
}

// is an upper case character
BOOL isUpper(char c)
{
	if((c >= 'A') && (c <= 'Z'))
		return 1U;
	return 0U;
}

// is lower case
BOOL isLower(char c)
{
	if((c >= 'a') && (c <= 'z'))
		return 1U;
	return 0U;
}

// is numeric
BOOL isNumeric(char c)
{
	if((c >= '0') && (c <= '9'))
		return 1U;
	return 0U;
}

/*
 * signed double conversion
 */
double ascii2double(char *val)
{
	double retval = 0;
	int power = -1, inc = 1;
	int sgn = 1;

	while(*val)	{

		switch(*val)	{

		case '-':
			sgn = -1;
			break;

		case '.':
			power = -1;
			inc = -1;
			break;

		default:
			if(inc > 0)	{
				retval = retval*10 + (*val - '0');
			} else {
				retval += pow(10.0, power) * (*val - '0');
				power += inc;
			}
			break;
		}
		val++;
	}
	return retval * sgn;
}

/*
 * String manipulation
 */
// Your basic linux-stle (argv, argc) parser based on delimiters
// string is destroyed
int explode_string(char *str, char *strp[], int limit, char delim, char quote)
{
int     i,l,inquo;

        inquo = 0;
        i = 0;
        strp[i++] = str;
        if (!*str)
           {
                strp[0] = 0;
                return(0);
           }
        for(l = 0; *str && (l < limit) ; str++)
        {
		if(quote)
		{
                	if (*str == quote)
                   	{
                        	if (inquo)
                           	{
                                	*str = 0;
                                	inquo = 0;
                           	}
                        	else
                           	{
                                	strp[i - 1] = str + 1;
                                	inquo = 1;
                           	}
			}
		}
                if ((*str == delim) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
        }
        strp[i] = 0;
        return(i);

}


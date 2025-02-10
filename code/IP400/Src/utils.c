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

// ascii to decimal
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

// convert an ascii string to a double
// until we encounter the decimal place, treat it the same as an integer.
// after that, scale each digit down appropriately
//
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


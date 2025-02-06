/*---------------------------------------------------------------------------
	Project:	      MMDLTX

	Module:		      Logger

	File Name:	      audio.c

	Author:		      MartinA

	Revision:	      1.00

	Description:      Log an error message. On the nucleo, we send it out to the
					  console UAR/T, on the E04, it goes out to the LPUART which
					  is connected to the VCOM port.

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.


	Revision History:

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdarg.h>

#include "usart.h"

char errmsg[200];

// need to figure out how to talk to UART on board...
void logger(int severity, char *format, ...)
{

	// process the arg list
    va_list argptr;
    va_start(argptr, format);
    vsprintf(errmsg,format, argptr);
    va_end(argptr);

	switch(severity)	{

	case LOG_NOTICE:
		USART_Print_string("Notice: %s\r\n", errmsg);
		break;

	case LOG_ERROR:
		USART_Print_string("Notice: %s\r\n", errmsg);
		break;

	case LOG_SEVERE:
		USART_Print_string("Notice: %s\r\n", errmsg);
		break;
	}

}



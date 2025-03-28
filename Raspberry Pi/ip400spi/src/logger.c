/*---------------------------------------------------------------------------
	Project:	      Ip400spi

	Module:		      Logger

	File Name:	      logger.c

	Author:		      MartinA

	Revision:	      1.00

	Description:      Log error messages in the system

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdarg.h>

#include "types.h"
#include "logger.h"

BOOL logDebug = FALSE;

void openLog(BOOL debug)
{
	logDebug = debug;
}

// log an error or info message
void logger(int severity, char *format, ...)
{
	switch(severity)	{

	case LOG_DEBUG:
		if(!logDebug)
			return;
		fprintf(stderr, "DEBUG: ");
		break;

	case LOG_NOTICE:
		fprintf(stderr, "Notice: ");
		break;

	case LOG_ERROR:
		fprintf(stderr, "ERROR: ");
		break;

	case LOG_FATAL:
		fprintf(stderr, "FATAL: ");
		break;
	}

	// process the arg list
    va_list argptr;
    va_start(argptr, format);
    vfprintf(stderr, format, argptr);
    va_end(argptr);
}



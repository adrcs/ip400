/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    tod.c

	Author:		    MartinA

	Description:	Time of Day clock

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
#include "tod.h"
#include "utils.h"

TIMEOFDAY	tod = { 0, 0, 0 };			// where we hold the TOD

// routines
void  TOD_10SecTimer(void)				// 10 second timer
{
	// handle seconds
	tod.Seconds += 10;
	if(tod.Seconds >= 60)	{
		tod.Minutes += tod.Seconds/60;
		tod.Seconds %= 60;
	}

	// handle minutes
	if(tod.Minutes >= 60)	{
		tod.Hours += tod.Minutes/60;
		tod.Minutes %= 60;
	}

	// handle hours
	if(tod.Hours > 24)
		tod.Hours %= 24;
}

// return pointer to Time of Day
void getTOD(TIMEOFDAY *time)
{
	time->Hours = tod.Hours;
	time->Minutes = tod.Minutes;
	time->Seconds = tod.Seconds;
}

// set the TOD from HH:MM string
// somewhat brute force parser
BOOL  setTOD(char *todString)
{
	char *todValues[5];

	int nParams = explode_string(todString, todValues, 5, ':', '"');
	if(nParams != 2)
		return FALSE;

	int nHours = ascii2Dec(todValues[0]);
	if((nHours < 0) || (nHours > 24))
		return FALSE;

	int nMins = ascii2Dec(todValues[1]);
	if((nHours < 0) || (nHours > 60))
		return FALSE;

	tod.Hours = nHours;
	tod.Minutes = nMins;
	tod.Seconds = 0;

	return TRUE;
}

// get the elapsed time between two TOD and timestamp (in seconds)
int getElapsed(TIMEOFDAY *time)
{
	// convert current tod to seconds
	int todSecs = (tod.Hours*3600) + (tod.Minutes * 60) + tod.Seconds;
	int timeSecs =  (time->Hours*3600) + (time->Minutes * 60) + time->Seconds;

	// calculate diff: if negative add one day
	int diff = todSecs - timeSecs;
	if(diff < 0)
		diff += 23*3600 + 59*60 + 59;

	return diff;
}

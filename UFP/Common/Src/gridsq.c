/*---------------------------------------------------------------------------
	Project:	      IP400 Unified Firmware Platform

	Module:		      Convert a lat/long into a grid square

	File Name:	      gridsq.c

	Date Created:	  Mar 22, 2026

	Author:			  MartinA

	Description:      Conversion routine for lat/long to grid square.
					  Courtesy of ham.stackexchange.com
					  URL: https://ham.stackexchange.com/questions/221/how-can-one-convert-from-lat-long-to-grid-square

					  Copyright © 2024-26, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/
#include <stdio.h>

#include "setup.h"
#include "utils.h"

char *GetGridSq(char *latitude, char *longitude)
{
	static char GridSq[7];		// grid square string

	double dlat = ascii2double(latitude);
	double dlong = ascii2double(longitude);

	uint8_t o1, o2, o3;
	uint8_t a1, a2, a3;
	double remainder;

	// process longitude
	remainder = dlong + 180.0;
	o1 = (uint8_t)(remainder / 20.0);
	remainder = remainder - (double)o1 * 20.0;
	o2 = (uint8_t)(remainder / 2.0);
	remainder = remainder - 2.0 * (double)o2;
	o3 = (uint8_t)(12.0 * remainder);

	// latitude
	remainder = dlat + 90.0;
	a1 = (uint8_t)(remainder / 10.0);
	remainder = remainder - (double)a1 * 10.0;
	a2 = (uint8_t)(remainder);
	remainder = remainder - (double)a2;
	a3 = (uint8_t)(24.0 * remainder);

	GridSq[0] = (char)o1 + 'A';
	GridSq[1] = (char)a1 + 'A';
	GridSq[2] = (char)o2 + '0';
	GridSq[3] = (char)a2 + '0';
	GridSq[4] = (char)o3 + 'a';
	GridSq[5] = (char)a3 + 'a';
	GridSq[6] = (char)0;

	return GridSq;
}

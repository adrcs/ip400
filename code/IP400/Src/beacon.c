/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    beacon.c

	Author:		    MartinA

	Description:	Beacon task. Sends a beacon frame periodically, based on timer in the setup
					structure. The beacon include position data in a readable format, which
					can come from a GPS receiver or the setup struct. Lat/Long data
					is sent in DDMM.MMMMM format.

					Define __ENABLE_GPS to enable the code.

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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <cmsis_os2.h>
#include <FreeRTOS.h>
#include <stm32wl3x_hal.h>

#include "frame.h"
#include "tasks.h"
#include "setup.h"
#include "utils.h"
#include "tod.h"

// config
#define	__ENABLE_GPS	0				// set to 1 if you have GPS attached
#define	__SPEED_DAEMON	0				// send beacon every 5 seconds for testing purposes

// local defines
#define	MAX_BEACON		80
#define	GPS_FIX_LEN		20				// gps fix length
// NMEA GGA Sentence fields
#define	GGA_TAG			0				// message tag
#define	GGA_TIMESTAMP	1				// time of fix
#define	GGA_LATITUDE	2				// latitude
#define	GGA_NS_HEMI		3				// latitude hemisphere
#define	GGA_LONGITUDE	4				// longitude
#define	GGA_EW_HEMI		5				// longitude hemisphere
// hemispheres
enum {
	N_HEMI=0,		// North
	S_HEMI,		// South
	E_HEMI,		// East
	W_HEMI,		// West
	N_HEMIS
};

uint8_t hemispheres[N_HEMIS] = {
		'N', 'S', 'E', 'W'
};

uint32_t	timerInitValue;			// value to initialize timer
uint32_t	timerCtrValue;			// current counter

BEACON_HEADER	beacon_hdr;			// beacon header
uint8_t		bcnPayload[MAX_BEACON];	// beacon payload

BOOL gpsMessageRx = FALSE;

#if __ENABLE_GPS
#define	GPS_BUFFER_SIZE		200
// LPUART for GPS receiver
extern hlpuart1 UART_HandleTypeDef;	// HAL Handle to LPUART
uint8_t GPSMsgBuf[GPS_BUFFER_SIZE];
char GPSLat[GPS_FIX_LEN];
char GPSLong[GPS_FIX_LEN];
char GPSFixTime[GPS_FIX_LEN];
BOOL haveGPSFix = FALSE;
char *gpsFlds[GPS_FIX_LEN];
#else
TIMEOFDAY wallClockTime;
#endif

// fwd refs here..
void GPSFormat(char *buffer, double value, uint8_t hePos, uint8_t heNeg);

// initialization: calculate the init value in
// quanta of MAIN_TASK_SCHED
void Beacon_Task_init(void)
{
// higher speed for testing...
#if __SPEED_DAEMON
	timerInitValue = 5  *1000/MAIN_TASK_SCHED;				// every 5 seconds for testing
	timerCtrValue = 0;
#else
	uint32_t timerTick = 60 * 1000/MAIN_TASK_SCHED;
	timerInitValue = setup_memory.params.setup_data.beaconInt * timerTick;
	timerCtrValue = 0;										// set to timerInitValue to wait
#endif

	// GPS Init
#if __ENABLE_GPS
    HAL_LPUART_Receive_DMA(&hlpuart1, (uint8_t*)GPSMsgBuf, GPS_BUFFER_SIZE);
    gpsMessageRx = FALSE;
    haveGPSFix = FALSE;
#endif

}

// this runs every MAIN_TASK_SCHED ms
void Beacon_Task_exec(void)
{
#if __ENABLE_GPS
	if(gpsMessageRx)	{
	    gpsMessageRx = FALSE;
	    ifprocessGPSMessage(GPSMsgBuf, GPS_BUFFER_SIZE))
			haveGPSFix = TRUE;
	}
#endif

	if(timerCtrValue > 0)	{
		timerCtrValue--;
		return;
	}
	timerCtrValue = timerInitValue;

	// start with the header
	beacon_hdr.setup.flags = setup_memory.params.setup_data.flags;
	beacon_hdr.setup.txPower = setup_memory.params.radio_setup.outputPower;
	uint8_t *buf = bcnPayload;

	// brute force copy: compiler rounds SETUP_FLAGS to 32 bits
	*buf++ = beacon_hdr.hdrBytes[0];
	*buf++ = beacon_hdr.hdrBytes[4];

	char *pPayload = (char *)buf;
	char *p2 =  pPayload;

#if __ENABLE_GPS
	//GPS generated payload
	if(haveGPSFix)		{
		strcpy(pPayload, "GPS,");
		strcat(pPayload, GPSLat);
		strcat(pPayload, GPSLong);
		strcat(pPayload, GPSFixTime);
	} else {
#else
	// setup struct generated payload
	strcpy(pPayload, "FXD,");
	pPayload += strlen(pPayload);
	double dlat = ascii2double(setup_memory.params.setup_data.latitude);
	GPSFormat(pPayload, dlat, N_HEMI, S_HEMI);
	strcat(pPayload, ",");
	pPayload += strlen(pPayload);
	double dlong = ascii2double(setup_memory.params.setup_data.longitude);
	GPSFormat(pPayload, dlong, E_HEMI, W_HEMI);
	strcat(pPayload, ",");
	pPayload += strlen(pPayload);

	// Use RTC for time
	getTOD(&wallClockTime);
	sprintf(pPayload, "%02d%02d%02d", wallClockTime.Hours, wallClockTime.Minutes, wallClockTime.Seconds);
	strcat(pPayload, ",");

	// home grid square
	strcat(pPayload, setup_memory.params.setup_data.gridSq);
#endif

	// firmware version
	int pos = strlen((char *)p2);
	buf[pos++] = def_params.params.FirmwareVerMajor + '0';
	buf[pos++] = def_params.params.FirmwareVerMinor + '0';
	buf[pos++] = '\0';			// null terminated

	// time to send a beacon frame..
	SendBeaconFrame(setup_memory.params.setup_data.stnCall, bcnPayload, pos);
}

/*
 * Format the +/-ddd.dddd lat/long format into DDMM.MMMMM format
 */
void GPSFormat(char *buffer, double value, uint8_t hePos, uint8_t heNeg)
{
	// Set hemi, get abs value
	uint8_t hemi = value > 0.00 ? hePos : heNeg;
	value = fabs(value);

	// separate whole and fractional
	int whole = (int)value;
	double fract = value - (double)whole;

	// calculate minutes and fraction
	double dmin = 60.0 * fract;
	dmin = round(dmin * 100.0)/100.0;

	int min = floor(dmin);
	int ifract = (int)ceil((dmin-min) * 100000);

	sprintf(buffer, "%d%02d.%05d%c", whole, min, ifract, hemispheres[hemi]);
}

#if __ENABLE_GPS
/*
 * Process an inbound GPS message
 */
BOOL processGPSMessage(GPSMsgBuf, GPS_BUFFER_SIZE)
{
	int limit = (int)strlen(GPSMsgBuf);
	nParams = explode_string(GPSMsgBuf, gpsFlds, limit, ',', '"');
	if (nParams == 0)
		return FALSE;

	//brute force position fix
	if(strcmp(gpsFlds[GGA_TAG[strlen(GGA_TAG)-strlen("GGA")]],"GGA"))
		return FALSE;

	strpy(GPSFixTime, gpsFlds[GGA_TIMESTAMP]);

	strcpy(GPSLat, gpsFlds[GGA_LATITUDE]);
	strcat(GPSLat, gpsFlds[GGA_NS_HEMI]);

	strcpy(GPSLong, gpsFlds[GGA_LONGITUDE]);
	strcat(GPSLong, gpsFlds[GGA_EW_HEMI]);

	return TRUE;
}

/*
 * callback from GPS receiver when rx is complete
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    HAL_LPUART_Receive_DMA(&hlpuart1, (uint8_t*)GPSMsgBuf, GPS_BUFFER_SIZE);
    gpsMessageRx = TRUE;
	return;
}
#endif

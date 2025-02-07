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

#include "config.h"
#include "frame.h"
#include "tasks.h"
#include "setup.h"
#include "utils.h"
#include "tod.h"
#include "usart.h"

// config
#define	__SPEED_DAEMON	0				// send beacon every 5 seconds for testing purposes

// local defines
#define	MAX_BEACON			80			// max beacon string
#define	GPS_FIX_LEN			20			// gps fix length
#define	GPS_BFR_SIZE		140			// GPS buffer size

// NMEA GGA Sentence fields
char *nmeaMsgTag = "RMC";				// sentence we are processing
enum {
	NMEA_TAG=0,			// message tag
	NMEA_TIMESTAMP,		// time of fix'
	NMEA_STATUS,		// status
	NMEA_LATITUDE,		// latitude
	NMEA_NS_HEMI,		// latitude hemisphere
	NMEA_LONGITUDE,		// longitude
	NMEA_EW_HEMI,		// longitude hemisphere
	NMEA_MIN_FLDS		// minimum fields in GPS message
};

// NMEA processing states
enum {
	NMEA_STATE_SOM=0,	// start of message
	NMEA_STATE_MSG,		// in the message
};

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
BOOL GPSBusy=FALSE;
TIMEOFDAY wallClockTime;

#if __ENABLE_GPS
// data for GPS
uint8_t GPSMsgBuf[GPS_BFR_SIZE];
uint8_t GPSEchoBuf[GPS_BFR_SIZE];
uint8_t GPSProcBuf[GPS_BFR_SIZE];
uint8_t *GPSBufPtr;
uint8_t GPSMsgSize;
uint8_t NMEAState;
//
BOOL haveGPSFix = FALSE;
BOOL gpsEchoReady=FALSE;
//
char GPSLat[GPS_FIX_LEN];
char GPSLong[GPS_FIX_LEN];
char GPSFixTime[GPS_FIX_LEN];
char *gpsFlds[GPS_BFR_SIZE];

// fwd Refs in this module in GPS mode
BOOL processGPSMessage(uint8_t *GPSMsgBuf, uint8_t bufferSize);
#endif

//fwd refs w/o GPS
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
    gpsMessageRx = FALSE;
    haveGPSFix = FALSE;
    gpsEchoReady = FALSE;
    GPSBufPtr = GPSMsgBuf;
    GPSBusy = FALSE;
    NMEAState=NMEA_STATE_SOM;
#endif

}

// this runs every MAIN_TASK_SCHED ms
void Beacon_Task_exec(void)
{
#if __ENABLE_GPS
	if(gpsMessageRx)	{
		// process the message
		if(processGPSMessage(GPSProcBuf, (uint8_t)strlen((char *)GPSProcBuf)))	{
			haveGPSFix = TRUE;
	    }
        gpsMessageRx = FALSE;
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
		strcat(pPayload, ",");
		strcat(pPayload, GPSLong);
		strcat(pPayload, ",");
		strcat(pPayload, GPSFixTime);
		strcat(pPayload, ",");
	} else {
#endif
	// setup struct generated payload
	strcpy(pPayload, "FXD,");
	pPayload += strlen(pPayload);
	double dlat = ascii2double(setup_memory.params.setup_data.latitude);
	GPSFormat(pPayload, dlat, N_HEMI, S_HEMI);
	strcat(pPayload, ",");
	pPayload += strlen(pPayload);
	double dlong = ascii2double(setup_memory.params.setup_data.longitude);
	GPSFormat(pPayload, dlong, E_HEMI, W_HEMI);
	strcat(pPayload, ",,");
#if __ENABLE_GPS
	}
#endif
	pPayload += strlen(pPayload);

	// Use RTC for time
	getTOD(&wallClockTime);
	sprintf(pPayload, "%02d%02d%02d", wallClockTime.Hours, wallClockTime.Minutes, wallClockTime.Seconds);
	strcat(pPayload, ",");

	// home grid square
	strcat(pPayload, setup_memory.params.setup_data.gridSq);

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



/*
 * Process GPS data from LPUART: runs at a higher priority
 */
void GPS_Task_exec(void)
{
#if __ENABLE_GPS
	char c;
	int nBytesinBuff;

	if((nBytesinBuff=gpsbuffer_bytesInBuffer()) == 0)
		return;

	for(int i=0;i<nBytesinBuff;i++)		{
		c = (char)gpsbuffer_get(0);

		switch(NMEAState)	{

		// waiting for a start of message
		case NMEA_STATE_SOM:
			if(c != '$')
				break;
			GPSBufPtr = GPSMsgBuf;
			NMEAState=NMEA_STATE_MSG;
			break;

		// collecting chars in a message
		case NMEA_STATE_MSG:
			*GPSBufPtr++ = c;
			GPSMsgSize = GPSBufPtr - GPSMsgBuf;
			if((c == '*') || (GPSMsgSize >= GPS_BFR_SIZE)) {
				*GPSBufPtr = '\0';
				GPSMsgSize++;
				memcpy(GPSEchoBuf, GPSMsgBuf, GPSMsgSize);
				gpsMessageRx = TRUE;
				memcpy(GPSProcBuf, GPSMsgBuf, GPSMsgSize);
				memcpy(GPSEchoBuf, GPSMsgBuf, GPSMsgSize);
				gpsEchoReady = TRUE;
				NMEAState=NMEA_STATE_SOM;
			}
			break;
		}
	}
#else
		return;				// unused code
#endif
}

#if __ENABLE_GPS
/*
 * Process an inbound GPS message
 */
BOOL processGPSMessage(uint8_t *GPSdata, uint8_t bufferSize)
{
	char *msg = (char *)GPSdata;
	uint8_t nParams = explode_string(msg, gpsFlds, bufferSize, ',', '"');
	if ((nParams == 0) || (nParams < NMEA_MIN_FLDS))
		return FALSE;

	//	brute force position fix
	// check for message with GGA at the end
	char *gpsTag = gpsFlds[NMEA_TAG];
	gpsTag += strlen(gpsTag)-strlen(nmeaMsgTag);
	if(strcmp(gpsTag, nmeaMsgTag))
		return FALSE;

	strcpy(GPSFixTime, gpsFlds[NMEA_TIMESTAMP]);

	strcpy(GPSLat, gpsFlds[NMEA_LATITUDE]);
	strcat(GPSLat, gpsFlds[NMEA_NS_HEMI]);

	strcpy(GPSLong, gpsFlds[NMEA_LONGITUDE]);
	strcat(GPSLong, gpsFlds[NMEA_EW_HEMI]);

	return TRUE;
}

/*
 * echo the last GPS message on the console
 */
void GPSEcho(void)
{
	if(!gpsEchoReady)
		return;

	USART_Print_string("%s\r\n", (char *)GPSEchoBuf);
	gpsEchoReady=FALSE;
}
#endif

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
#include <main.h>

#include "config.h"
#include "frame.h"
#include "setup.h"
#include "utils.h"
#include "tod.h"
#include "usart.h"
#include "kiss.h"
#include "xcvr.h"

#include "tasks.h"

#if defined(__NUCLEOCC2) || defined(__PI_BOARD)
#include "wl33.h"
#endif

// config
#define	__SPEED_DAEMON	0				// send beacon every 5 seconds for testing purposes
#define	__NO_STARTUP	1				// no startup beacon

// local defines
#define	MAX_BEACON			80			// max beacon string
#define	GPS_FIX_LEN			20			// gps fix length
#define	GPS_BFR_SIZE		140			// GPS buffer size

#if __ENABLE_GPS
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

#define	N_NMEA_SEN		14		// number of NMEA sentences
enum	{
	NMEA_CMD_DISABLED=0,		// disabled
	NMEA_CMD_ONCE,				// once per fix
	NMEA_CMD_2FIX,				// once every 2 fixes
	NMEA_CMD_3FIX,				// once every 3 fixes
	NMEA_CMD_4FIX,				// once every 4 fixes
	NMEA_CMD_5FIX,				// once every 5 fixes
};

char *nmeaCmd = "$PMTK314";		// command
uint8_t	senCmds[N_NMEA_SEN] = {
	NMEA_CMD_DISABLED, 			// 0:GLL disabled
	NMEA_CMD_ONCE,				// 1: RMC once
	NMEA_CMD_DISABLED,			// 2: VTG disabled
	NMEA_CMD_DISABLED,			// 3: GGA disabled
	NMEA_CMD_DISABLED,			// 4: GSA disabled
	NMEA_CMD_DISABLED,			// 5: GSV disabled
	NMEA_CMD_DISABLED,			// 6 not used
	NMEA_CMD_DISABLED,			// 7
	NMEA_CMD_DISABLED,			// 13
	NMEA_CMD_DISABLED,			// 14
	NMEA_CMD_DISABLED,			// 15
	NMEA_CMD_DISABLED,			// 16
	NMEA_CMD_DISABLED,			// 17
	NMEA_CMD_DISABLED 			// 18
};

#define	BEACON_DISTANCE			5				// beacon every 5 km
#define	PI						(double)3.141592653562795
#endif

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
char cmdBuf[MAX_BEACON];
//
BOOL haveGPSFix = FALSE;
BOOL gpsEchoReady=FALSE;
//
char GPSLat[GPS_FIX_LEN];
char GPSLong[GPS_FIX_LEN];
char GPSFixTime[GPS_FIX_LEN];
char *gpsFlds[GPS_BFR_SIZE];

// GPS Fix struct
typedef struct gps_fix_t	{
	double		dlat;
	double		dlong;
} GPSFIX;

GPSFIX currFix, oldFix;

// fwd Refs in this module in GPS mode
BOOL processGPSMessage(uint8_t *GPSMsgBuf, uint8_t bufferSize);
double distanceInKm(GPSFIX gps1, GPSFIX gps2);
void sendGPSCmd(void);
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
#if __NO_STARTUP
	timerCtrValue = timerInitValue;							// set to timerInitValue to wait for beacon time
#else
	timerCtrValue = 0;										// immdediate beacon on startup
#endif
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
			if(haveGPSFix)	{
				oldFix = currFix;
				currFix.dlat = ascii2double(GPSLat);
				currFix.dlong = ascii2double(GPSLong);
			} else {
				haveGPSFix = TRUE;
				currFix.dlat = ascii2double(GPSLat);
				currFix.dlong = ascii2double(GPSLong);
				oldFix = currFix;
			}
	    } else {
	    	gpsMessageRx = FALSE;
	    }
	}
	// send a beacon if he has moved more than BEACON_DISTANCE
	double dist = distanceInKm(oldFix, currFix);
	if ((int)dist > BEACON_DISTANCE)
		timerCtrValue = 0;

#else
// if GPS is not using LPUART, let kiss have it if enabled
#if KISS_ON_LPUART
	processKissFrame();
#endif
#endif

	if(timerCtrValue > 0)	{
		timerCtrValue--;
		return;
	}
	timerCtrValue = timerInitValue;
	SendBeacon();

#if __ENABLE_GPS
	// send a command to the GPS every beacon interval
	sendGPSCmd();
#endif
}

/*
 * convert a beacon header to a payload
 * return the length of the payload
 */
//  used for frequencies
union	{
	uint32_t	bigint;
	uint8_t		bytes[sizeof(uint32_t)];
} big2Bytes;

// used for flags
union {
	SETUP_FLAGS flags;
	uint8_t		bytes;
} flags2bytes;
//
int hdr2buf(BEACON_HEADER *beacon_hdr, uint8_t *bfraddr)
{
	uint8_t *buf = bfraddr;

	*buf++ = beacon_hdr->setup.nXcvrs;				// num xcvrs
	flags2bytes.flags = beacon_hdr->setup.flags;		// flags
	*buf++ = flags2bytes.bytes;
	*buf++ = beacon_hdr->setup.FirmwareMajor;		// FW major
	*buf++ = beacon_hdr->setup.FirmwareMinor;		// FW minor

	// radio information
	for(int k=0;k<MAX_XCVRS;k++)		{
		*buf++ = beacon_hdr->setup.radios[k].txPower;
		big2Bytes.bigint = beacon_hdr->setup.radios[k].txFrequency;
		for(int j=0;j<sizeof(uint32_t);j++)
			*buf++ = big2Bytes.bytes[j];
		big2Bytes.bigint = beacon_hdr->setup.radios[k].rxFrequency;
		for(int j=0;j<sizeof(uint32_t);j++)
			*buf++ = big2Bytes.bytes[j];
	}

	return (buf-bfraddr);
}
// now the opposite
void buf2hdr(BEACON_HEADER *beacon_hdr, uint8_t *buf)
{

	beacon_hdr->setup.nXcvrs = *buf++;				// num xcvrs
	flags2bytes.bytes = *buf++;
	beacon_hdr->setup.flags = flags2bytes.flags;
	beacon_hdr->setup.FirmwareMajor = *buf++;		// FW major
	beacon_hdr->setup.FirmwareMinor = *buf++;		// FW minor

	// radio information
	for(int k=0;k<N_XCVRS;k++)		{
		beacon_hdr->setup.radios[k].txPower = *buf++;
		for(int j=0;j<sizeof(uint32_t);j++)
			big2Bytes.bytes[j] = *buf++;
		beacon_hdr->setup.radios[k].txFrequency = big2Bytes.bigint;
		for(int j=0;j<sizeof(uint32_t);j++)
			big2Bytes.bytes[j] = *buf++;
		beacon_hdr->setup.radios[k].rxFrequency = big2Bytes.bigint;
	}
}

void SendBeacon(void)
{

	memset(&beacon_hdr, 0, sizeof(BEACON_HEADER));

	// start with the header
	STN_PARAMS *stnParams = GetStationParams();
	beacon_hdr.setup.nXcvrs = N_XCVRS;
	beacon_hdr.setup.flags = stnParams->setup_data.flags;
	beacon_hdr.setup.FirmwareMajor = def_params.params.FirmwareVerMajor + '0';
	beacon_hdr.setup.FirmwareMinor = def_params.params.FirmwareVerMinor + '0';

	for(int i=0;i<beacon_hdr.setup.nXcvrs;i++)		{
		RADIO_SETUP *rsetup = (RADIO_SETUP *)GetRadioSetup(i);
		beacon_hdr.setup.radios[i].txPower = rsetup->outputPower;
		beacon_hdr.setup.radios[i].txFrequency = rsetup->lFrequencyBase;
		beacon_hdr.setup.radios[i].rxFrequency = rsetup->lFrequencyBase;
	}

	/*
	 * Create the beacon payload
	 */
	uint8_t *buf = bcnPayload;

	// move the header in
	buf += hdr2buf(&beacon_hdr, buf);

	// station data
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
	strcat(pPayload, ",");

	// Description field
	int pos = strlen((char *)p2);
	strncat(pPayload, setup_memory.params.setup_data.Description, MAX_DESC);
	pos += MAX_DESC + 2*sizeof(uint8_t) + 2*sizeof(uint32_t);	// account for firmware and freq fields

	// time to send a beacon frame..
	SendBeaconFrame(bcnPayload, pos+1);

	// update the mesh table
	UpdateMeshStatus();
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

	if((nBytesinBuff=DBUART_bytesInBuffer()) == 0)
		return;

	for(int i=0;i<nBytesinBuff;i++)		{
		c = (char)DBUART_buffer_get(0);

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
 * Send a command to the GPS device to limit traffic
 */
void sendGPSCmd(void)
{
	uint8_t cksum = 0;
	char *buf = cmdBuf;
	strcpy(buf, nmeaCmd);
	buf += strlen(nmeaCmd);

	for(int j=1;j<strlen(nmeaCmd);j++)
		cksum ^= nmeaCmd[j];

	for(int i=0;i<N_NMEA_SEN;i++)	{
		*buf = ',';
		cksum ^= *buf++;
		*buf = senCmds[i] + '0';
		cksum ^= *buf++;
	}

	*buf++ = '*';
	hex2ascii(cksum, buf);
	buf += 2;

	*buf++ = '\r';
	*buf++ = '\n';
	*buf = '\0';
	uint16_t buflen = buf - cmdBuf;

	DBUART_Send_String(cmdBuf, buflen);
}
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


/*
 * Calculate the distance between two GPS coordinates
 */
double degreesToRadians(double degrees)
{
    return degrees * PI / 180.0;
}

double distanceInKm(GPSFIX gps1, GPSFIX gps2)
{
    double earthRadiusKm = 6371.0;				// radius of earth

    double dLat = degreesToRadians(gps2.dlat-gps1.dlat);
    double dLon = degreesToRadians(gps2.dlong - gps1.dlong);

    double rlat1  = degreesToRadians(gps1.dlat);
    double rlat2 = degreesToRadians(gps2.dlat);

    double a = sin(dLat/2) * sin(dLat/2) + sin(dLon/2) * sin(dLon/2) * cos(rlat1) * cos(rlat2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    return earthRadiusKm * c;
}
#endif

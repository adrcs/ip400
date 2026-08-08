/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        main.c

        Author:           root

        Creation Date:    Mar. 6, 2025

        Description:      Mainline for SPI daemon

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "types.h"
#include "logger.h"
#include "spidefs.h"
#include "timer.h"

// SPI device
char spiDev[20];

int debugFlag;
char hostname[50];				// name of remote host
uint16_t hostport;				// host port
uint16_t localport;				// my port

// timer or interrupt mode
#define	NO_INTERRUPT	0		// do not use interrupts

// forward refs
void show_help(char *name);

// catch a control-c or other termination
void sighandler(int s)
{
	logger(LOG_ERROR, "\n3000Caught signal %d\n",s);
	stopTimer();
	exit(1);
}

int main(int argc, char *argv[])
{
	int c;

	/*
	// set up control/c catch
	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = sighandler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &si3000gIntHandler, NULL);
	*/

	// set default SPI
	strcpy(spiDev, SPI_0_DEV_0);
	debugFlag = FALSE;

	// parse command line parameters
	while ((c = getopt(argc, argv, "s:d:hn:p:m:")) != -1) {

		// process the command line
		switch((char )c) {

			// SPI device
			case 's':
				strcpy(spiDev, optarg);
				break;

			// host name
			case 'n':
				strcpy(hostname, optarg);
				break;

			// host port
			case 'p':
				sscanf(optarg, "%hd", &hostport);
				break;

			// my port
			case 'm':
				sscanf(optarg, "%hd", &localport);
				break;

			// debug
			case 'd':
				sscanf(optarg, "%x", &debugFlag);
				break;

			// help
			case 'h':
				show_help(argv[0]);
				break;
		}
	}

	// open the log and test enabled
	openLog(debugFlag&DEBUG_LOG);
	logger(LOG_DEBUG, "Debug logging enabled \n");

	int spiDevNum = spi_lookup(spiDev);
	if(spiDevNum == -1)	{
		logger(LOG_FATAL, "Cannot find device name %s\n", spiDev);
		exit(100);
	}

    spi_setup(spiDevNum, SPI_MODE_0, SPI_NBITS, SPI_SPEED, debugFlag&DEBUG_SPI);

	if(!spiTaskInit(spiDevNum))	{
		logger(LOG_FATAL, "Cannot open SPI device %s\n", spiDev);
		exit(100);
	}

	if(!setup_udp_socket(hostname, hostport, localport, debugFlag))	{
		logger(LOG_FATAL, "Cannot create UDP socket to %s:%d\n", hostname, hostport, localport);
		exit(100);
	}

#if NO_INTERRUPT
	while(1)	{
		spiTask();
		usleep(MSTOUS(TIMER_VALUE));
	}
	forced syntax error;		// force error if this mode is invoked
#else
	if(!startTimer(TIMER_VALUE, &spiTask))	{
		logger(LOG_FATAL, "Could not start the SPI task\n");
		exit(101);
	}
#endif

	return EXIT_SUCCESS;
}

void show_help(char *name) {
	fprintf(stderr,
			"Usage: %s -[sdhnpm]\n"
			"-s SPI device name\n"
			"-d  debug mode \n"
			"-h print this help message\n"
			"-n remote host name\n"
			"-p remote port number"
			"-m my port number",
			name);
}

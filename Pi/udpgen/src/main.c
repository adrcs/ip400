/*---------------------------------------------------------------------------
        Project:          UDPGen

        File Name:        main.c

        Author:           martin A

        Creation Date:    Mar. 6, 2025

        Description:      Mainline for UDP packet generator

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

#define	MSTOUS(x)	((int)x*(int)1000)
#define	MSTONS(x)	((long long)x*(long long)1000000)

int nRepeats;					// number of repeats
int debugFlag;
char hostname[50];				// name of remote host
uint16_t hostport;				// host port
uint16_t localport;				// my port
uint8_t frametype;				// type of frame to send
uint16_t timedelay;				// delay between frames

// debug modes
#define	DEBUG_LOG		0x01	// debug log
#define	DEBUG_SPI		0x02	// debug SPI

// forward refs
void show_help(char *name);


int main(int argc, char *argv[])
{
	int c;
	nRepeats = 1;
	timedelay = 0;

	if(argc == 1)	{
		fprintf(stderr, "Usage: %s -n <host> -p <host port < -m <my port> -f <frame type> -r <repeats> -t <delay> -D <debug> -h\n", argv[0]);
		return 0;
	}

	// parse command line parameters
	while ((c = getopt(argc, argv, "n:p:m:f:r:D:t:h")) != -1) {

		// process the command line
		switch((char )c) {

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

			// frame type
			case 'f':
				sscanf(optarg, "%hhd", &frametype);
				break;

			// repeats
			case 'r':
				sscanf(optarg, "%d", &nRepeats);
				break;

			// debug
			case 'D':
				sscanf(optarg, "%d", &debugFlag);
				break;

			// time delay
			case 't':
				sscanf(optarg, "%hd", &timedelay);
				break;

			// help
			case 'h':
				show_help(argv[0]);
				return 0;
		}
	}

	if(!setup_udp_socket(hostname, hostport, localport))	{
		logger(LOG_FATAL, "Cannot create UDP socket to %s:%d\n", hostname, hostport, localport);
		exit(100);
	}

	for(int i=0;i<nRepeats;i++)		{
		// send a frame here
		logger(LOG_DEBUG, "Sending frame %d of %d\n", i+1, nRepeats);
		send_IP400Frame(frametype, i);
		if(timedelay)
			usleep(MSTOUS(timedelay));
	}

	// close up
	close_udp_socket();

	return EXIT_SUCCESS;
}

void show_help(char *name) {
	fprintf(stderr,
			"Usage: %s -[npmfDh]\n"
			"-n remote host name\n"
			"-p remote port number\n"
			"-m my port number\n"
			"-f frame type\n"
			"\t0 - Beacon frame\n"
			"\t1 - Short AX.25 (76)\n"
			"\t2 - Long AX.25  (596)\n"
			"\t3 - Long IP packet (900)\n"
			"\t4 - Large IP packet (1200)\n"
			"\t5 - Huge IP packet (1405)\n"
			"-r repeat frame\n"
			"-D  debug mode \n"
			"-h print this help message\n",
			name);
}

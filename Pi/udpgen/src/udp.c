/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        udp.c

        Author:           root

        Creation Date:    Mar. 7, 2025

        Description:      THis module contains the UDP socket and code

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include "types.h"
#include "logger.h"

// local defines
#define	MAX_BUFFER		1053				// max buffer size
#define SOCKET_ERROR	-1					// socket error

struct udp_threads_t	{
	int					udpsock;			// socket
	struct sockaddr_in 	si_remote;			// where to send it
	// background task
	BOOL				exit;				// exit
	uint8_t				buffer[MAX_BUFFER];	// rx bufferrxSegLen
	int					length;				// length received
} udp_threads;

/*
 * set up the UDP Socket
 */
BOOL setup_udp_socket(char *hostname, int hostport, int localport)
{
	struct sockaddr_in si_me;
	struct hostent ah, *host;

	memset(&ah,0,sizeof(ah));
	host = gethostbyname(hostname);
	if (!host)
	{
		logger(LOG_NOTICE, "Unable to find host %s\n", hostname);
		return FALSE;
	}
	memset((char *) &udp_threads.si_remote, 0, sizeof(struct sockaddr_in));
	udp_threads.si_remote.sin_addr = *(struct in_addr *)host->h_addr;
	udp_threads.si_remote.sin_family = AF_INET;
	udp_threads.si_remote.sin_port = htons(hostport);

	if ((udp_threads.udpsock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
	{
		logger(LOG_NOTICE, "Unable to create new socket for fpga udp_BOOL EnqueSPIFrame(void *buffer, uint16_t length)threads_tconnection\n");
		return FALSE;
	}

	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(localport);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	if (!strncmp(inet_ntoa(udp_threads.si_remote.sin_addr),"127.",4))
		si_me.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (bind(udp_threads.udpsock, (const struct sockaddr *)&si_me, sizeof(si_me))==-1)
	{
		logger(LOG_NOTICE, "Unable to bind to %s:%d for fpga connection\n",
				inet_ntoa(si_me.sin_addr), ntohs(si_me.sin_port));
		return FALSE;
	}
	if (!udp_threads.udpsock) {
		logger(LOG_NOTICE, "Unable to create UDP socket forBOOL setup_udp_socket(char *hostname, int hostport, int localport) %s:%d\n", hostname, localport);
		return FALSE;
	}

    // set the receive timeout on the read to one second
	struct timeval timeout;
	timeout.tv_sec = (__time_t)1;
	timeout.tv_usec =(__suseconds_t)0;
	if (setsockopt (udp_threads.udpsock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)) < 0)	{
			logger(LOG_NOTICE, "setsockopt timeout failed\n");
	}

	logger(LOG_DEBUG, "Sending to %X:%d", si_me.sin_addr.s_addr, si_me.sin_zero);

	return TRUE;
}

/*
 * Close the socket
 */
void close_udp_socket(void)
{
	if(udp_threads.udpsock)
		close(udp_threads.udpsock);
}

/*
 * Send a UDP packet
 */
BOOL send_udp_packet(void *data, uint16_t length)
{
	int stat;
	stat = sendto(udp_threads.udpsock, data, length, 0, (struct sockaddr *)&udp_threads.si_remote,sizeof(struct sockaddr_in));
	if(stat == -1)	{
		logger(LOG_NOTICE, "Error on UDP packet: %d:%s\n", errno, geterrno(errno));
		return FALSE;
	}
	return TRUE;
}


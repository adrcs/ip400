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
#include <pthread.h>

#include "types.h"
#include "logger.h"
#include "spidefs.h"

// local defines
#define	MAX_BUFFER		1024				// max buffer size
#define SOCKET_ERROR	-1					// socket error

struct udp_threads_t	{
	int					udpsock;			// socket
	struct sockaddr_in 	si_remote;			// where to send it
	// background task
	BOOL				exit;				// exit
	pthread_t			rxfunc;				// rx function
	uint8_t				buffer[MAX_BUFFER];	// rx bufferrxSegLen
	int					length;				// length received
} udp_threads;

void *udp_receive_task(void *args);

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

	// start the rx background
	udp_threads.exit = FALSE;
	pthread_create(&udp_threads.rxfunc, NULL, &udp_receive_task, (void *)(&udp_threads));
	return TRUE;
}

/*
 * Close the socket
 */
void close_udp_socket(void)
{
	udp_threads.exit = TRUE;
	pthread_join(udp_threads.rxfunc, NULL);

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
	logger(LOG_DEBUG, "UDP Packet Sent\n");
	return TRUE;
}

/*
 * Receive a UDP packet
 */
void *udp_receive_task(void *args)
{
	struct sockaddr_in si_them;
	unsigned int themlen = sizeof(struct sockaddr_in);

	struct udp_threads_t *s = (struct udp_threads_t *)args;
	SPI_DATA_FRAME *spiFrame;

	while(!s->exit)		{

		if ((s->length = recvfrom(s->udpsock,s->buffer,MAX_BUFFER, 0,
				(struct sockaddr *)&si_them,&themlen)) == SOCKET_ERROR)
		{
			/*
			 * process a non-timeout.
			 */
			if(errno != EAGAIN)	{
				logger(LOG_ERROR, "UDP Receive error %d\n", errno);
			}
		} else {
			// check the packet header first
			if(!isIP400Frame(s->buffer))
				continue;

			// rx a good packet
			if((spiFrame = malloc(s->length)) == NULL)	{
				continue;
			}
			spiFrame->buffer = s->buffer;
			spiFrame->length = s->length;
			EnqueSPIFrame(spiFrame);
		}
	}
	return NULL;
}


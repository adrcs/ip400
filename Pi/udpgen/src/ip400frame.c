/*---------------------------------------------------------------------------
        Project:          udpgen

        File Name:        ip400frame.c

        Author:           Martin A, VE6VH

        Creation Date:    Nov. 3, 2025

        Description:      Send an IP400 frame over UDP

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "frame.h"

/*	frame header def'n		*/
struct frame_header_t	{
	uint8_t	eye[4];						// 'IP4C'			4
	uint8_t	status;						// status byte		5
	uint8_t offset_hi;					// high offset		6
	uint8_t offset_lo;					// low offset		7
	uint8_t	length_hi;					// length hi		81
	uint8_t	length_lo;					// length lo		9
	uint8_t	fromCall[N_CALL];			// from callsign	13
	uint8_t fromVPN[N_IPBYTES];			// from vpn			15
	uint8_t	toCall[N_CALL];				// to callsign		21
	uint8_t toVPN[N_IPBYTES];			// to vpn			23
	uint8_t coding;						// packet coding	24
	uint8_t hopCount;					// hop count		25
	uint8_t flags;						// remaining flags	26
} frame_header;

#define	PACKET_HDR_SIZE		sizeof(frame_header)					// size of packet header
#define	LENGTH_HI(c)		((uint8_t)(c>>8))
#define	LENGTH_LO(c)		((uint8_t)(c&0xFF))

/*
 * UDP cases
 * 1)	Single frame that fits in one SPI exchange					n<500
 * 2)	Fragmented frame into two SPI exchanges						500<n<900
 * 3) 	Split frame: first half fragmented, second single frame		900<n<1400
 * 4) 	Split and fragmented: both halves							1400<4<max udp size
 */
enum {
	BEACON_FRAME,
	SHORT_AX25_FRAME,			// case 1 frame
	LONG_AX25_FRAME,			// case 2
	LONG_DATA_FRAME,			// case 3
	LARGE_DATA_FRAME,			// case 4
	HUGE_DATA_FRAME,			// case 5
	N_FRAME_TYPES
};

// IP 400V2 beacon frame
// udp case 1
#define	BEACON_PAYLOAD		87
#define BEACON_SIZE			(BEACON_PAYLOAD+PACKET_HDR_SIZE)			// sizeof beacon frame

uint8_t Beacon[BEACON_SIZE] = {
	// beacon header
	0x49, 0x50, 0x34, 0x43, 			// IP4C
	0x01, 								// status
	0x00, 0x00, 						// offset hi/lo
	LENGTH_HI(BEACON_PAYLOAD),
	LENGTH_LO(BEACON_PAYLOAD), 			// payload length hi/lo (70 bytes)
	0xda, 0x96, 0xa0, 0xc5,				// from call
	0x01, 0xde, 						// from VPN
	0xff, 0xff, 0xff, 0xff,				// to call
	0xff, 0xff,							// to VPN
	BEACON_PACKET, 						// coding (04: Beacon)
	0x00, 								// hop count
	0x00, 								// flags
	// Payload: 4 + 9 + 9 + 9 + 1 + 56 = 88 bytes
	0x01,								// number of transceivers
	0x69,								// flags
	0x32, 0x30,							// Firmware majpr/minor  (4 bytes)
	0x14, 								// xcvr 1 transmit power (dBm) 9 bytes
	0xf0, 0x9a, 0x91, 0x1a,				// transmit Freq
	0xf0, 0x9a, 0x91, 0x1a,				// receive Freq
	0x00,                               // xcvr 2 transmit power (dBm)  9 bytes
	0x00, 0x00, 0x00, 0x00,             // transmit Freq
	0x00, 0x00, 0x00, 0x00,             // receive Freq
	0x00,								// xcvr 3 transmit power (dBm)  9 bytes
	0x00, 0x00, 0x00, 0x00,             // transmit Freq
	0x00, 0x00, 0x00, 0x00,             // receive Freq
	0x46,  0x58,  0x44,  0x2c,  0x35,  0x31,  0x30,  0x38,  //8
	0x2e,  0x35,  0x38,  0x30,  0x30,  0x31,  0x4e,  0x2c,  //16
	0x31,  0x31,  0x34,  0x31,  0x30,  0x2e,  0x35,  0x36,  //24
	0x30,  0x30,  0x31,  0x57,  0x2c,  0x2c,  0x30,  0x30,  //32
	0x30,  0x35,  0x30,  0x30,  0x2c,  0x44,  0x4f,  0x32,  //40
	0x31,  0x76,  0x64,  0x2c,  'U',   'D',   'P',   'G',   //48
	'e',   'n',   0x20,  0x4e,  0x6f,  0x64,  0x65,  0x00   //56
};

// short AX25 encapsulated frame
// udp case 1
#define	SHORT_AX25_PAYLOAD		52			// payload size
#define SHORT_AX25_SIZE			(SHORT_AX25_PAYLOAD+PACKET_HDR_SIZE)							// sizeof short AX25 encapsulated frame
uint8_t ShortAX25[SHORT_AX25_SIZE] = {
	0x49, 0x50, 0x34, 0x43, 				// IP4C
	0x01,                                   // status
	0x00, 0x00,                             // offset hi/lo
	LENGTH_HI(SHORT_AX25_PAYLOAD),			// length hi/lo
	LENGTH_LO(SHORT_AX25_PAYLOAD),
	0xda, 0x96, 0xa0, 0xc5,                 // from call
	0xa0, 0xff,                             // from VPN
	0xff, 0xff, 0xff, 0xff,    				// to call
	0xff, 0xff,  							// to VPN
	AX_25_PACKET,                           // coding (05: AX.25 Encapsulated)
	0x00,                                   // hop count
	0x00,									// flags (do not repeat frame)
	'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','*','*'		// 52 chars
};

// short AX25 encapsulated frame
// udp case 2
#define	LONG_AX25_PAYLOAD		572
#define LONG_AX25_SIZE			(LONG_AX25_PAYLOAD+PACKET_HDR_SIZE)			// sizeof short AX25 encapsulated frame
uint8_t LongAX25[LONG_AX25_SIZE] = {
	0x49, 0x50, 0x34, 0x43, 		// IP4C
	0x01,                                   // status
	0x00, 0x00,                             // offset hi/lo
	LENGTH_HI(LONG_AX25_PAYLOAD),		// length hi/lo
	LENGTH_LO(LONG_AX25_PAYLOAD),
	0xda, 0x96, 0xa0, 0xc5,                 // from call
	0xa0, 0xff,                             // from VPN
	0xff, 0xff, 0xff, 0xff,    				// to call
	0xff, 0xff,  							// to VPN
	AX_25_PACKET,                           // coding (05: AX.25 Encapsulated)
	0x00,                                   // hop count
	0x00,									// flags (do not repeat frame)
	'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 52 chars
	'1','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 104 chars
	'2','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 156 chars
	'3','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 208 chars
	'4','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 260 chars
	'5','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 312 chars
	'6','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 364 chars
	'7','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 416 chars
	'8','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 468 chars
	'9','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'*','*','T','e','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 520 chars
	'1','0','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I',
	'J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	'>','>','L','a','s','t',' ','M','e','s','s','a','g','e','\r','\n',// 572 chars
};

// long ip encapsulated packet
// udp case 2: fragmented and reassembled
#define LONG_PAYLOAD_SIZE		900			// sizeof short AX25 encap
#define LONG_DATA_SIZE			(LONG_PAYLOAD_SIZE+PACKET_HDR_SIZE)
uint8_t LongIPFrame[LONG_DATA_SIZE] = {
		0x49, 0x50, 0x34, 0x43, 				// IP4C
		0x01,                                   // status
		0x00, 0x00,                             // offset hi/lo
		LENGTH_HI(LONG_PAYLOAD_SIZE),			// length hi/lo
		LENGTH_LO(LONG_PAYLOAD_SIZE),
		0xda, 0x96, 0xa0, 0xc5,                 // from call
		0xa0, 0xff,                             // from VPN
		0xff, 0xff, 0xff, 0xff,    				// to call
		0xff, 0xff,  							// to VPN
		AX_25_PACKET,                           // coding (05: AX.25 Encapsulated)
		0x00,                                   // hop count
		0x00
};

// large ip encapsulated packet
// udp case 3: split packet; fragmented first half, single back half
#define LARGE_PAYLOAD_SIZE		1200			// sizeof short AX25 encap
#define LARGE_DATA_SIZE			(LARGE_PAYLOAD_SIZE+PACKET_HDR_SIZE)
uint8_t LargeIPFrame[LARGE_DATA_SIZE] = {
		0x49, 0x50, 0x34, 0x43, 				// IP4C
		0x01,                                   // status
		0x00, 0x00,                             // offset hi/lo
		LENGTH_HI(LARGE_PAYLOAD_SIZE),			// length hi/lo
		LENGTH_LO(LARGE_PAYLOAD_SIZE),
		0xda, 0x96, 0xa0, 0xc5,                 // from call
		0xa0, 0xff,                             // from VPN
		0xff, 0xff, 0xff, 0xff,    				// to call
		0xff, 0xff,  							// to VPN
		IP_ENCAPSULATED,        	            // coding (05: AX.25 Encapsulated)
		0x00,                                   // hop count
		0x00
};


// huge ip encapsulated packet
#define HUGE_PAYLOAD_SIZE		1410			// huge payload packet
#define HUGE_DATA_SIZE			(HUGE_PAYLOAD_SIZE+PACKET_HDR_SIZE)
uint8_t HugeIPFrame[HUGE_DATA_SIZE] = {
		0x49, 0x50, 0x34, 0x43, 				// IP4C
		0x01,                                   // status
		0x00, 0x00,                             // offset hi/lo
		LENGTH_HI(HUGE_PAYLOAD_SIZE),			// length hi/lo
		LENGTH_LO(HUGE_PAYLOAD_SIZE),
		0xda, 0x96, 0xa0, 0xc5,                 // from call
		0xa0, 0xff,                             // from VPN
		0xff, 0xff, 0xff, 0xff,    				// to call
		0xff, 0xff,  							// to VPN
		IP_ENCAPSULATED,        	            // coding (05: AX.25 Encapsulated)
		0x00,                                   // hop count
		0x00
};

// internals
void send_long_frame(uint8_t *data, int size);

// send a frame based on frame type enum
void send_IP400Frame(uint8_t frametype, int seq)
{
	switch(frametype)	{

	case BEACON_FRAME:
		fprintf(stderr, "Sending Beacon Frame\n");
		send_udp_packet(Beacon, BEACON_SIZE);
		break;

	case SHORT_AX25_FRAME:
		fprintf(stderr, "Sending short AX.25 frame\n");
		sprintf((char *)&ShortAX25[sizeof(frame_header)], "%04d", seq);
		send_udp_packet(ShortAX25, SHORT_AX25_SIZE);
		break;

	case LONG_AX25_FRAME:
		fprintf(stderr, "Sending long (%d) AX.25 frame\n", (int)LONG_AX25_SIZE);
		sprintf((char *)&LongAX25[sizeof(frame_header)], "%04d", seq);
		send_udp_packet(LongAX25, LONG_AX25_SIZE);
		break;

	case LONG_DATA_FRAME:
		fprintf(stderr, "Sending long (%d) AX.25 frame\n", (int)LONG_DATA_SIZE);
		sprintf((char *)&LongAX25[sizeof(frame_header)], "%04d", seq);
		send_long_frame(LongIPFrame, LONG_DATA_SIZE);
		break;

	case LARGE_DATA_FRAME:
		fprintf(stderr, "Sending large (%d) IP data frame\n", (int)LARGE_DATA_SIZE);
		sprintf((char *)&LongAX25[sizeof(frame_header)], "%04d", seq);
		send_long_frame(LargeIPFrame, LARGE_DATA_SIZE);
		break;

	case HUGE_DATA_FRAME:
		fprintf(stderr, "Sending Huge (%d) IP data frame\n", (int)HUGE_DATA_SIZE);
		sprintf((char *)&LongAX25[sizeof(frame_header)], "%04d", seq);
		send_long_frame(HugeIPFrame, HUGE_DATA_SIZE);
		break;

	default:
		fprintf(stderr,"Invalid frame type: max %d\n", N_FRAME_TYPES);
		return;
	}
}

// send an long frame: keep payload in printable range
void send_long_frame(uint8_t *data, int size)
{
	uint8_t c = '0';
	char *endMessage = ">>END OF PACKET";
	size_t msgSize = strlen(endMessage);

	// fill the payload field
	for(int i=0;i<size-PACKET_HDR_SIZE;i++)
	{
		data[PACKET_HDR_SIZE+i] = c;
		if(((i%10) == 0) && (i != 0))	{
			if(c < '9')
				c++;
			else
				c = '0';
		}
	}
	// ensure that next packet does not get overwritten
	strcpy((char *)&data[size-msgSize-1], endMessage);
	send_udp_packet(data, size);
}

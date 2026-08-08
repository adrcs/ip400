/*---------------------------------------------------------------------------
	Project:	      IP400 Node

	Module:		      Frame Definitions

	File Name:	      frame.h

	Date Created:	  Jan 8, 2025

	Author:			  MartinA

	Description:      Definitions for V2 frames

					  Copyright © 2024-26, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/

#ifndef FRAMEV2_H_
#define FRAMEV2_H_

#include <stdint.h>
#include "types.h"
#include "kiss.h"

// frame defines
#define	N_CALL				4			// octets in the excess-40 compressed callsign
#define	N_IPBYTES			2			// number of IP address bytes
#define	N_FLAGS				2			// flag bytes
#define	MAX_CALL			6			// max callsign size
#define	PAYLOAD_MIN			56			// min octets in payload
#define PAYLOAD_MAX			500			// max octets in payload
#define	MAX_CALL_BUFFER		20			// max buffering for callsign in payload
#define	N_FEC				4			// number of bytes in the FEC
#define	BROADCAST_ADDR		0xFF		// broadcast address
#define	N_EYE				4			// bytes in the eye 'IP4C'
#define	MAX_CHUNKS			2			// maximum callsign 'chunks'
#define	BFR_SIZE			2048		// tx/rx buffer size

// callsign field
#define	ALL_CALL_VALUE		(uint32_t)0xFFFFFFFF
#define	MAX_CALL_VALUE		(uint32_t)0xF432FFFF
#define	MAX_HOPS			2		// max hops

typedef enum frag_flag_e	{
	FRAG_SELFCONTAINED=0,			// self-contained frame
	FRAG_FIRST_FRAG, 				// first fragment
	FRAG_MIDDLE_FRAG,				// middle fragment
	FRAG_END_FRAG					// end fragment
} FRAG_FLAGS;

// AX.25 compatibility definitions
#define	AX25_VPN_MASK		0xFFE0		// mask for VPN base
#define	AX25_VPN_BASE		0xFFA0		// base for VPN address
#define	AX25_SSID_MASK		0xFFF0		// mask for SSID value
#define	AX25_C_H_MASK		0x0010		// mask for C/H bit
//
#define	AX25_BASE_MSB		0xFF		// MSB of base address
#define	AX25_BASE_LSB		0xA0		// LSB of base address

// VPN Address in AX25 compatibility mode
typedef	struct	ax25_vpn_addr_t {
		unsigned	char ssid:		4;		// SSID
		unsigned	char c_h_bit:	1;		// C or H bit
		unsigned	char hdr:		3;		// header: constant '101'
		unsigned	char marker:	8;		// marker field 'FF'
} AX25_VPNADDR;

// compressed callsign
typedef struct callsign_t	{
	union {
		uint8_t		bytes[N_CALL];	// compressed callsign
		uint32_t	encoded;
	} callsign;
} IP400_CALL;

// IP 400 MAC address
typedef	struct ip400_mac_t	{
	IP400_CALL			callbytes;		// callsign bytes
	union {
		uint8_t			vpn[N_IPBYTES];	// raw VPN bytes
		AX25_VPNADDR	ax25vpn;		// ax.25 format
		uint16_t		encvpn;			// encoded vpn address
	} vpnBytes;
} IP400_MAC;

// header flags
typedef struct frame_flags_t {
	unsigned fragmentation:	2;		// fragmentation
	unsigned coding:		4;		// data type
	unsigned bandwidth:		2;		// OFDM bandwidth
	unsigned FEC:			1;		// FEC applied
	unsigned bitsperCarr:	3;		// bits per carrier
	unsigned hoptable:		1;		// hop table is included
	unsigned srcExt:		1;		// source call is extended
	unsigned destExt:		1;		// dest call is extended
	unsigned payloadMSB:	1;		// MSB of payload size
} IP400_FLAGS;

// hop table flags
typedef struct hop_flags_t	{
	unsigned valid:			1;			// callsign field is valid
	unsigned hbr:			1;			// has been repeated
	unsigned unused:		6;			// unused bite
} HOP_FLAGS;

// hop table
typedef struct hoptable_t	{
	IP400_MAC 	rptCalls[MAX_HOPS];					// repeater callsigns	6 x 2 		12
	union	{
		HOP_FLAGS	flags;							// hop table flags		1
		uint8_t		flagbyte;						// flags as bytes		1
	} hopflags[MAX_HOPS];							//									2
	uint8_t		unused[13];							// unused bytes						14 + 13 = 27
	uint16_t	CCITT16Crc;
} HOPTABLE;

// complete frame header
typedef struct ip400_frame_t	{
	IP400_MAC	source;				// source call sign				6
	IP400_MAC	dest;				// destination call sign		6
	union {
		IP400_FLAGS	flags;				// flag bit field
		uint16_t	allflags;			// all flags
		uint8_t		flagBytes[N_FLAGS];	// flag bytes
	} flagfld;						//								2
	uint8_t		length;				// data length					1
	uint32_t	seqNum;				// packet sequence number		4
	IP400_CALL	srcExt;				// extended source callsign		4
	IP400_CALL	destExt;			// extended dest callsign		4
	void 		*buf;				// Payload data					2	// replaced by CRC-16 on transmission
	void		*hopTable;			// hop table address			29 bytes for transmission
} IP400_FRAME;

// min/max payload sizes
#define	IP_400_CALL_SIZE	N_CALL
#define	IP_400_MAC_SIZE		(N_CALL + N_IPBYTES)
#define	IP_400_FLAG_SIZE	sizeof(uint16_t)
#define	IP_400_HDR_SIZE		(2*IP_400_CALL_SIZE + IP_400_FLAG_SIZE)
#define	IP_400_LEN_SIZE		sizeof(uint8_t)
#define	IP_400_CRC_SIZE		2

// packet data type
typedef enum {
		UTF8_TEXT_PACKET=0,			// Text packet (chat application)
		DATA_PACKET,				// data packet
		BEACON_PACKET,				// beacon packet
		AX_25_PACKET,				// AX.25 encapsulated packet
		IP_ENCAPSULATED,			// IP encapsulated packet
		ECHO_REQUEST,				// echo request frame
		ECHO_RESPONSE,				// echo response frame
		LOCAL_COMMAND,				// local command frame
		USER_DEFINED				// user defined frame types
} IP400DataType;

// callsign fields
typedef enum	{
	SRC_CALLSIGN=0,					// dest for encode is source callsign
	DEST_CALLSIGN,					// dest for encode is dest callsign
	RPTR_SLOT1,						// repeater call slot 1
	RPTR_SLOT2						// repeater call slot 2
} CallSignSource;

// frame status
typedef struct frame_stats_t {
	uint32_t		duplicates;					// duplicates
	uint32_t		nProcessed;					// frames for processing..
	uint32_t		nBeacons;					// number of beacons processed
	uint32_t		nChat;						// number of chat frames
	uint32_t		nKiss;						// number of KISS frames
	uint32_t		nUndecoded;					// not decoded
	uint32_t		nEchoReq;					// echo request
	uint32_t		nEchoResp;					// echo response
	uint32_t		nRepeated;					// repeated frames
	uint32_t		Unknown;					// cannot decode
	uint32_t		nWereMine;					// these were my frames
	uint32_t		nRejected;					// rejected frames
} FRAME_STATS;

// Radio stats for all radio types
typedef struct radio_stats_t {
	uint32_t 		radioFSM;					// radio FSM status
	char *			fsmState;					// readable FSM status
	char *			codeState;					// radio state
	uint32_t		SyncCount;					// OFDM Sync count
	uint32_t		TxFrameCnt;					// transmit frame count
	uint32_t		RxFrameCnt;					// good rx frame count
	uint32_t		CRCErrors;					// CRC Errors
	uint32_t		TimeOuts;					// Timeouts
	uint32_t		lastRSSI;					// last RSSI reading
	uint32_t		unprocessed;				// unprocessed frames
	uint32_t		dequeued;					// frames dequeued
	void			*bfrStatus;					// pointr to a buffer status struct
} RADIO_STATS;

// interpretations
extern char *ofdm_ab_modTypes[];
extern char *ofdm_ab_bandwidth[];
extern char *fec_methods[];

// links in
uint8_t getFrameStatus(void);
void SendBeacon(void);

// references
uint8_t macEncode(char *callsign, uint16_t ipAddr, IP400_MAC *mac);
BOOL callEncode(char *callsign, uint16_t ipAddr, IP400_FRAME *frame, CallSignSource dest);
void ax25Encode(AX25_ADDR *ax25addr, IP400_FRAME *frame, CallSignSource dest);
BOOL callDecode(IP400_FRAME *frame, char *callsign, uint16_t *ipAddr, CallSignSource source);
void EncodeChunk(char *src, int len, uint32_t *enc);

// frame senders
BOOL SendBeaconFrame(uint8_t *payload, int bcnlen);
BOOL SendTextFrame(char *srcCall, uint16_t srcPort, char *destCall, uint16_t dstPort, char *buf, uint16_t length, BOOL repeat);
BOOL SendEchoReqFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, char *buf, uint16_t length, BOOL repeat);
BOOL SendDataFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, uint8_t *buf, uint16_t length, uint8_t coding, BOOL repeat);
//
void SendSPIFrame(void *spiHdr, uint8_t *payload, int len);
BOOL SendKissFrame(IP400_FRAME *kissFrame, uint8_t port);
void ProcessRxKissFrame(IP400_FRAME *rFrame);		// send a frame over KISS
//
BOOL EnqueChatFrame(void *Frame);				// queue a chat frame

FRAME_STATS *GetFrameStats(void);				// return the frame stats
//
uint8_t getFrameStatus(void);					// get the frame status
BOOL FrameisMine(IP400_FRAME *frame);
void RepeatFrame(IP400_FRAME *frame);
void ProcessRxFrame(IP400_FRAME *rframe, int rawLength);
void DeleteFrame(IP400_FRAME *fr);
//
// lookup a frame in the mesh table
int getNMeshEntries(char *dest_call, int len);
IP400_MAC *getMeshEntry(char *dest_call, int len);
IP400_MAC *getNextEntry(char *dest_call, int len);

#endif /* FRAME_H_ */

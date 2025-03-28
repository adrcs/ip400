/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	Module:		      <module description here>

	File Name:	      frame.h

	Date Created:	  Jan 8, 2025

	Author:			  MartinA

	Description:      <what it does>

					  Copyright © 2024-25, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/

#ifndef FRAME_H_
#define FRAME_H_

#include <stdint.h>
#include "types.h"

// frame defines
#define	N_CALL				4			// octets in the excess-40 compressed callsign
#define	MAX_CALL			6			// max callsign size
#define	EXT_CALL			4*MAX_CALL	// extended call sign field
#define	PAYLOAD_MIN			56			// min octets in payload
#define PAYLOAD_MAX			1053		// max octets in payload
#define	MAX_CALL_BUFFER		20			// max buffering for callsign in payload
#define	N_FEC				4			// number of bytes in the FEC
#define	BROADCAST_ADDR		0xFF		// broadcast address

#define	MAX_HOP_COUNT		15			// max hop count

// transmit states
enum	{
		TX_IDLE=0,		// idle - waiting for work
		TX_SENDING,		// sending a frame
		TX_DONE			// done
};

// radio error register
#define	SEQ_COMPLETE_ERR	0x8000		// Sequencer error
#define	SEQ_ACT_TIMEOUT		0x4000		// Sequencer action timeout
#define	PLL_CALAMP_ERR		0x0800		// VCO amplitude calibration error
#define	PLL_CALFREQ_ERR		0x0400		// VCO frequency calibration error
#define	PLL_UNLOCK_ERR		0x0200		// PLL is unlocked
#define	PLL_LOCK_FAIL		0x0100		// PLL lock failure
#define	DBM_FIFO_ERR		0x0020		// Data buffer failure
#define	N_RADIO_ERRS		7			// number of the above

// radio FSM states
enum 	fsm_states_e {
		FSM_IDLE=0,				// idle
		FSM_ENA_RF_REG,			// enable RF registers
		FSM_WAIT_ACTIVE2,		// wait for active 2
		FSM_ACTIVE2,			// active 2
		FSM_ENA_CURR,			// enable current
		FSM_SYNTH_SETUP,		// synth setup
		FSM_CALIB_VCO,			// VCO calibration
		FSM_LOCKRXTX,			// lock Rx and Rx
		FSM_LOCKONTX,			// lock on Rx
		FSM_EN_PA,				// enable PA
		FSM_TX,					// transmit
		FSM_PA_DWN_ANA,			// Analog power down
		FSM_END_TX,				// end transmit
		FSM_LOCKONRX,			// lock on Rx
		FSM_EN_RX,				// Enable Rx
		FSM_EN_LNA,				// enable LNA
		FSM_RX,					// recieve
		FSM_END_RX,				// end rx
		FSM_SYNTH_PWDN,			// synth power down
		FSM_N_FSM_STATES
};

// header flags
typedef struct frame_flags_t {
	unsigned hop_count: 	4;		// hop count
	unsigned coding:		4;		// coding method
	unsigned compression: 	2;		// compression method
	unsigned hoptable:		1;		// hop table is included
	unsigned srcExt:		1;		// source call is extended
	unsigned destExt:		1;		// dest call is extended
	unsigned command:		1;		// command packet
	unsigned noconnect:		1;		// connectionless
	unsigned repeat:		1;		// repeat flag
} IP400_FLAGS;

// callsign field
typedef	struct callsign_t	{
	union {
		uint8_t		bytes[N_CALL];	// compressed callsign
		uint32_t	encoded;
	} callbytes;
	uint16_t	port;				// port information
} IP400_CALL;

// hop table
typedef struct hoptable_t	{
	union	{
		uint8_t		bytes[N_CALL];
		uint32_t	encoded;
	} hopAddr;
} HOPTABLE;

// complete frame
typedef struct ip400_frame_t	{
	IP400_CALL	source;				// source call sign
	IP400_CALL	dest;				// destination call sign
	union {
		IP400_FLAGS	flags;			// flag bit field
		uint16_t	allflags;		// all flags
	} flagfld;
	uint32_t	seqNum;				// packet sequence number
	void 		*buf;				// data to send
	uint16_t	length;				// data length
	void		*hopTable;			// hop table address
} IP400_FRAME;

// min/max payload sizes
#define	IP_400_PORT_SIZE	sizeof(uint16_t)
#define	IP_400_CALL_SIZE	(N_CALL + IP_400_PORT_SIZE)
#define	IP_400_FLAG_SIZE	sizeof(uint16_t)
#define	IP_400_HDR_SIZE		(2*IP_400_CALL_SIZE + IP_400_FLAG_SIZE)
#define	IP_400_LEN_SIZE		sizeof(uint16_t)
#define	MIN_FRAME_SIZE		sizeof(IP400_FRAME) + PAYLOAD_MIN + N_FEC
#define MAX_FRAME_SIZE		MIN_FRAME_SIZE - PAYLOAD_MIN + PAYLOAD_MAX

// packet coding (type)
typedef enum {
		UTF8_TEXT_PACKET=0,		// Text packet (chat application)
		COMPRESSED_AUDIO,		// compressed audio packet
		COMPREESSD_VIDEO,		// compressed video packet
		DATA_PACKET,			// data packet
		BEACON_PACKET,			// beacon packet
		IP_ENCAPSULATED,		// IP encapsulated packet
		AX_25_PACKET,			// AX.25 encapsulated packet
		RFC4733_DTMF,			// DTMF packet
		DMR_FRAME,				// DMR Frame
		DSTAR_FRAME,			// Dstar Frame
		P25_FRAME,				// TIA project 25
		NXDN_FRAME,				// NXDN
		M17_FRAME,				// M17
		TBD_1,
		TBD_2,
		LOCAL_COMMAND			// local command frame
} IP400FrameType;

// audio compression types
enum {
		AUDIO_RAW,				// raw 16-bit PCM
		AUDIO_ULAW,				// mu law compression
		AUDIO_CODEC2,			// codec 2 encoded
		AUDIO_AMBE				// AMBE encoded
};

// H.246 Video compression
enum {
		H264_240_180_24,		// H.264: 240x180, 24FPS
		H264_320_240_24,		// H.264: 320x240, 24FPS
		H264_480_360_12,		// H.264: 480x360, 12FPS
		H264_640_480_6			// H.264: 640x480, 6FPS
};

// callsign fields
enum	{
	SRC_CALLSIGN=0,				// dest for encode is source callsign
	DEST_CALLSIGN				// dest for encode is dest callsign
};

// frame stats
typedef struct frame_stats_t {
	uint32_t		TxFrameCnt;					// transmit frame count
	uint32_t		RxFrameCnt;					// good receive frame count
	uint32_t		CRCErrors;					// CRC Errors
	uint32_t		TimeOuts;					// Timeouts
	uint32_t		lastRSSI;					// last RSSI reading
	uint32_t		framesOK;					// processed frames
	uint32_t		dropped;					// rejected frames
	uint32_t		duplicates;					// duplicates
	uint32_t		nBeacons;					// number of beacons processed
	uint32_t		nRepeated;					// repeated frames
} FRAME_STATS;

uint8_t getFrameStatus(void);

// references
uint8_t callEncode(char *callsign, uint16_t port, IP400_FRAME *frame, uint8_t dest, uint8_t offset);
BOOL callDecode(IP400_CALL *encCall, char *callsign, uint16_t *port);

// frame senders
BOOL SendTextFrame(char *srcCall, uint16_t srcPort, char *destCall, uint16_t dstPort, char *buf, uint16_t length, BOOL repeat);
void SendBeaconFrame(char *srcCall, uint8_t *payload, int bcnlen);
void SendSPIFrame(void *spiHdr, uint8_t *payload, int len);
//
BOOL EnqueChatFrame(void *Frame);				// queue a chat frame
FRAME_STATS *GetFrameStats(void);				// return the frame stats
uint32_t GetRadioStatus(void);					// get the radio status
uint8_t GetFSMState(void);						// get FSM state
//
uint8_t getFrameStatus(void);					// get the frame status

#endif /* FRAME_H_ */

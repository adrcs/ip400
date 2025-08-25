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
#define	N_IPBYTES			2			// number of IP address bytes
#define	MAX_CALL			6			// max callsign size
#define	EXT_CALL			4*MAX_CALL	// extended call sign field
#define	PAYLOAD_MIN			56			// min octets in payload
#define PAYLOAD_MAX			1053		// max octets in payload
#define	MAX_CALL_BUFFER		20			// max buffering for callsign in payload
#define	N_FEC				4			// number of bytes in the FEC
#define	BROADCAST_ADDR		0xFF		// broadcast address

#define	MAX_HOP_COUNT		15			// max hop count

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
typedef enum 	fsm_states_e {
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
} SubGFSMState;

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

// IP 400 MAC address
typedef	struct ip400_mac_t	{
	union {
		uint8_t		bytes[N_CALL];	// compressed callsign
		uint32_t	encoded;
	} callbytes;
	union {
		uint8_t		vpn[N_IPBYTES];
		struct	{
			uint8_t	ax25Marker;		// marker byte
			uint8_t ax25SSID;		// SSID
		} AX25;
		uint16_t	encvpn;			// encoded vpn address
	} vpnBytes;
} IP400_MAC;

// hop table
typedef struct hoptable_t	{
	IP400_MAC hopAddr;
} HOPTABLE;

// complete frame
typedef struct ip400_frame_t	{
	IP400_MAC	source;				// source call sign
	IP400_MAC	dest;				// destination call sign
	union {
		IP400_FLAGS	flags;			// flag bit field
		uint16_t	allflags;		// all flags
	} flagfld;
	uint16_t	length;				// data length
	uint32_t	seqNum;				// packet sequence number
	void 		*buf;				// data to send
	void		*hopTable;			// hop table address
} IP400_FRAME;

// min/max payload sizes
#define	IP_400_CALL_SIZE	(N_CALL + N_IPBYTES)
#define	IP_400_FLAG_SIZE	sizeof(uint16_t)
#define	IP_400_HDR_SIZE		(2*IP_400_CALL_SIZE + IP_400_FLAG_SIZE)
#define	IP_400_LEN_SIZE		sizeof(uint16_t)

#define	MIN_FRAME_SIZE		sizeof(IP400_FRAME) + PAYLOAD_MIN + N_FEC
#define MAX_FRAME_SIZE		MIN_FRAME_SIZE - PAYLOAD_MIN + PAYLOAD_MAX

// PRBS defines
#define	PRBS_LEN			127
#define	PRBS_REPETITION		8
#define	PRBS_FRAME_SIZE		(PRBS_LEN*PRBS_REPETITION)

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
		ECHO_REQUEST,			// echo request frame
		ECHO_RESPONSE,			// echo response frame
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
// tx test modes
typedef enum {
	SUBG_TEST_OFF=0,			// test mode off
	SUBG_TEST_CW,				// CW mode
	SUBG_TEST_PRBS				// send PRBS
} SubGTestMode;

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

// links in
uint8_t getFrameStatus(void);
void SendBeacon(void);

// references
uint8_t callEncode(char *callsign, uint16_t port, IP400_FRAME *frame, uint8_t dest, uint8_t offset);
BOOL callDecode(IP400_MAC *encCall, char *callsign, uint16_t *port);
void EncodeChunk(char *src, int len, uint32_t *enc);

// frame senders
BOOL SendBeaconFrame(uint8_t *payload, int bcnlen);
BOOL SendTextFrame(char *srcCall, uint16_t srcPort, char *destCall, uint16_t dstPort, char *buf, uint16_t length, BOOL repeat);
BOOL SendDataFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, uint8_t *buf, uint16_t length, uint8_t coding, BOOL repeat);
BOOL SendEchoReqFrame(char *srcCall, uint16_t srcIPAddr, char *destCall, uint16_t dstIPAddr, char *buf, uint16_t length, BOOL repeat);
//
void SendSPIFrame(void *spiHdr, uint8_t *payload, int len);
//
BOOL EnqueChatFrame(void *Frame);				// queue a chat frame
FRAME_STATS *GetFrameStats(void);				// return the frame stats
uint32_t GetRadioStatus(void);					// get the radio status
SubGFSMState GetFSMState(void);					// get FSM state
char *getSubGState(void);						// get subg state
//
uint8_t getFrameStatus(void);					// get the frame status
BOOL FrameisMine(IP400_FRAME *frame);
void RepeatFrame(IP400_FRAME *frame);
void ProcessRxFrame(IP400_FRAME *rframe, int rawLength);
void QueueTxFrame(IP400_FRAME *txframe);
//
void setSubgTestMode(SubGTestMode mode);
//
// lookup a frame in the mesh table
int getNMeshEntries(char *dest_call, int len);
IP400_MAC *getMeshEntry(char *dest_call, int len);
IP400_MAC *getNextEntry(char *dest_call, int len);

#endif /* FRAME_H_ */

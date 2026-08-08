/*---------------------------------------------------------------------------
	Project:	      NucleoCCV2

	Module:		      Definitins for WL33 radio

	File Name:	      wl33.h

	Date Created:	  Feb 27, 2026

	Author:			  MartinA

	Description:      Radio specific definitions for WL33

					  Copyright © 2024-26, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/
#ifndef WL33_H_
#define WL33_H_

#include <stdint.h>

// max on-air frame size
#define	MAX_FRAME_SIZE		2047
#define	MIN_ON_AIR_SIZE		1054		// minimum frame length

// radio error register
#define	SEQ_COMPLETE_ERR	0x8000		// Sequencer error
#define	SEQ_ACT_TIMEOUT		0x4000		// Sequencer action timeout
#define	PLL_CALAMP_ERR		0x0800		// VCO amplitude calibration error
#define	PLL_CALFREQ_ERR		0x0400		// VCO frequency calibration error
#define	PLL_UNLOCK_ERR		0x0200		// PLL is unlocked
#define	PLL_LOCK_FAIL		0x0100		// PLL lock failure
#define	DBM_FIFO_ERR		0x0020		// Data buffer failure
#define	N_RADIO_ERRS		7			// number of the above

// PRBS defines
#define	PRBS_LEN			127
#define	PRBS_REPETITION		8
#define	PRBS_FRAME_SIZE		(PRBS_LEN*PRBS_REPETITION)

// max power varies by board type
#ifdef __NUCLEOCC2
#define MAX_OUPUT_POWER 		14				// max output power
#define	PA_DRIVE_MODE			PA_DRV_TX_HP	// drive mode
#else
#define MAX_OUPUT_POWER 		20				// max output power
#define	PA_DRIVE_MODE			PA_DRV_TX_TX_HP	// drive mode
#endif

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
} wl33FSMState;


// links in from the xcvr abstraction
void wl33_Init(void);			// Initialize
void wl33_Process(void);			// processing loop
void *wl33_GetSetup(void);
void wl33_RadioSetup(void *);
void wl33_PrintRadioSetup(void);
//
void wl33_QTxFrame(void *);			// queue tx frame
void wl33_TestMode(uint8_t  mode);
void *Getwl33Stats(void);





#endif /* WL33_H_ */

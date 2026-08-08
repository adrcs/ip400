/*---------------------------------------------------------------------------
	Project:	    WL33_NUCLEO_UART

	File Name:	    menu.c

	Author:		    MartinA

	Description:	Menu handler

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				    Copyright (c) Alberta Digital Radio Communications Society
				    All rights reserved.

	Revision History:

---------------------------------------------------------------------------*/
#include <string.h>
#include <malloc.h>
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "main.h"

#include "types.h"
#include "usart.h"
#include "setup.h"
#include "utils.h"
#include "tod.h"
#include "config.h"
#include "ip.h"
#include "frame.h"
#include "kiss.h"
#include "memory.h"
#include "spi.h"
#include "xcvr.h"
#include "bfrmgr.h"
#include "tasks.h"

#if _HAS_FPGA
#include "platform.h"
#endif

//macros
#define	tolower(c)		(c+0x20)
#define	toupper(c)		(c-0x20)

// menu state
uint8_t menuState;		// menu state
enum menu_states_e {
	MENU_OFF,			// off
	MENU_SHOWING,		// showing the menu
	MENU_SELECTING,		// getting a selection
	MENU_SELECTED,		// item has been selected
	MENU_PAUSED			// pausing a while...
};

// key entry state
uint8_t entryState;		// state of keyboard entry
enum	{
	NO_ENTRY=0,			// no entry yet..
	ENTERING,			// entering a value
	VALIDATING,			// validating the entry
};

// menu items
#define	NO_ITEM			-1			// no item selected
#define	MENU_REPAINT	-2			// repaint the menu
#define	MENU_KISSMODE	-3			// kiss frame detected

// control codes..
enum {
	CONTROL_CLEAR=0,			// clear screen
	CONTROL_HOME,				// cursor home
	CONTROL_DOUBLE_TOP,			// double wide and height (top)
	CONTROL_DOUBLE_BOTTOM,		// double wide and height (bottom)
	CONTROL_SINGLE,				// single wide and height
	NCONTROL_CODES				// number of conrol codes
};

struct controlCodes_t	{
	char sequence[MAX_SEQ];		// sequence
	uint8_t	len;				// length
} controlCodes[NCONTROL_CODES] = {
		{{ASCII_ESC, DEC_LEADIN, '2', 'J'}, 4},
		{{ASCII_ESC, DEC_LEADIN, 'H'},      3},
		{{ASCII_ESC, '#', '3'},    		    3},
		{{ASCII_ESC, '#', '4'},    		    3},
		{{ASCII_ESC, '#', '5'},    		    3},
};

// strings common to all menu types
char *whatItem = 	"??Try again->";
char *menuSpacer =  "\r\n\n";
char *pauseString = "Hit enter to continue->";
char *selectItem = 	"Select an item->";

static char menu[100];			// Buffer for file items
int sel_item = 0;				// selected item
uint8_t	activeMenu;				// active menu
uint8_t editMode;				// current entry editing mode
uint8_t	maxEntry;				// max entry length
uint8_t	xcvrNum=0;				// transceiver number

// forward refs in this module
void printMenu(void);		// print the menu
int getMenuItem(void);		// get a menu item
void sendControlCode(uint8_t code);
BOOL pause(void);
void Print_Radio_stats(int index);
void Print_Buffer_stats(int index);
void Print_Frame_stats(FRAME_STATS *stats);
void Print_Memory_Stats(void);
void Print_Radio_errors(uint32_t errs);
void Print_FSM_state(uint8_t xcvr);
uint8_t getEntry(int activeMenu, int item);
uint8_t getKeyEntry(void);
char editEntry(char c);
void sendTextString(char *string);
void printValidator(int activeMenu, int item);
void at86_PrintRadioSetup(void);
void OFDM_AB_PrintRadioSetup(void);
void wl33_PrintRadioSetup(void);

// list of menus
enum	{
	MAIN_MENU=0,		// main menu
	RADIO_MENU,			// radio params menu
	STATION_MENU,		// Station params menu
	DIAGNOSTIC_MENU,	// diagnostics menu
	FLASH_MENU,			// flash menu
#if _HAS_CODEC
	CODEC_MENU,			// codec menu
#endif
#if __XCVR_AT86
	AT86_MENU,			// setup menu for an AT86
#endif
#if __XCVR_OFDM_AB
	OFDM_AB_MENU,		// setup menu for OFDM-AB
#endif
#if __XCVR_WL33
	WL33_MENU,			// setup menu for WL33
#endif
	N_MENUS				// number of menus
};

// menu return functions
enum	{
	RET_MORE=0,			// more to do
	RET_DONE,			// done
	RET_PAUSE			// pause before leaving
};

/*
 * Definitions for data entry
 */
// case values
enum	entry_mode_e {
	ENTRY_ANYCASE,		// upper and lower case
	ENTRY_UPPERCASE,	// upper case only
	ENTRY_LOWERCASE,	// lower case only
	ENTRY_NUMERIC,		// numeric only, 0-9 + '-'
	ENTRY_FLOAT,		// floating point 0-9 + '.' + '-'
	ENTRY_TIME,			// numeric + ':'
	ENTRY_NONE			// none of the above
};
// stuff used in printing..
// modulation types
char *modTypes[] = {
		"2FSK",
		"4FSK",
		"2GFSK",
		"4GFSK",
};
// PA modes
char *paModes[] = {
		"?Undefined",
		"TX 10dBm Max",
		"HP 14dBm Max",
		"TX_HP 20dBm Max"
};
// keys in entry mode
#define	KEY_EOL			0x0D			// carriage return
#define	KEY_ESC			0x1B			// escape key
#define KEY_DEL			0x7F			// delete key
#define	KEY_BKSP		0x08			// backspace key

#define	MAX_ENTRY		40				// max entry chars
#define	MAX_FLDSIZE		MAX_ENTRY-2		// max field size entry
#define	MAX_DATASIZE	MAX_DATAFLD-2	// max data field size
#define	MAX_FLOATSIZE	MAX_DATASIZE+1	// max float entry size
#define	MAX_TIMESIZE	5				// max timesize

BOOL delMode = FALSE;
BOOL processingKiss = FALSE;			// processing a KISS frame
int pos = 0;
char keyBuffer[MAX_ENTRY];

/*
 * The first part of this code contains the menus and executor routines
 * handle the various menu options
 * return TRUE if done, else FALSE
 */

/*
 * executors common to all menu items
 */
// exit the current menu
uint8_t exitMenu(void)
{
	activeMenu = MAIN_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
// set a parameter value
uint8_t setParam(void)
{
	return(getEntry(activeMenu, sel_item));
}

/*
 * Main menu items
 */
// A: print all setup
uint8_t printAllSetup(void)
{
	TIMEOFDAY tod;
	getTOD(&tod);
	SOCKADDR_IN *ipAddr;

	strcpy(menu, getRevID());
	menu[strlen(menu)-1] = '\0';
	USART_Print_string("Firmware version: %d.%d, %s\r\n",def_params.params.FirmwareVerMajor,
			def_params.params.FirmwareVerMinor, menu);

	strcpy(menu, getDateID());
	menu[strlen(menu)-1] = '\0';
	USART_Print_string("Build %s\r\n", menu);

#if _HAS_FPGA
	strcpy(menu, geFPGARevID());
	menu[strlen(menu)] = '\0';
	USART_Print_string("FPGA Code: %s \r\n", menu);
#endif

	USART_Print_string("System time is %02d:%02d:%02d\r\n", tod.Hours, tod.Minutes, tod.Seconds);
	USART_Print_string("Radio ID is %08x%08x\r\n", GetDevID0(), GetDevID1());

	GetMyVPN(&ipAddr);
	USART_Print_string("VPN Address %d.%d.%d.%d\r\n\n",ipAddr->sin_addr.S_un.S_un_b.s_b1, ipAddr->sin_addr.S_un.S_un_b.s_b2,
			ipAddr->sin_addr.S_un.S_un_b.s_b3, ipAddr->sin_addr.S_un.S_un_b.s_b4);
	printStationSetup();
	return RET_PAUSE;
}
// B: list mesh status
uint8_t listMesh(void)
{
	Mesh_ListStatus();
	return RET_PAUSE;
}
// C: enter chat mode
uint8_t chatMode(void)
{
	if(Chat_Task_exec())
		return RET_PAUSE;
	return RET_MORE;
}
// D: set the diagnostic mode
uint8_t setDiagnostics(void)
{
	activeMenu = DIAGNOSTIC_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
// E: Send a beacon frame
uint8_t sendBeacon(void)
{
	SendBeacon();
	return RET_DONE;
}
// R: set the radio entry mode
uint8_t setRadio(void)
{
	activeMenu = RADIO_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
// S: set the station entry mode
uint8_t setStation(void)
{
	activeMenu = STATION_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
// W: write the setup memory
uint8_t writeSetup(void)
{
	if(!UpdateSetup())	{
		USART_Print_string("Error in writing memory: setup may be corrupt\r\n");
	} else {
		USART_Print_string("Memory written successfully\r\n");
	}
	menuState = MENU_OFF;
	return RET_PAUSE;
}

/*
 * radio menu items (most are handled by setParam)
 */
// A: Setup for AT86RF215
#if __XCVR_AT86
uint8_t setAT86(void)
{
	activeMenu = AT86_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
#endif
// B: Setup for OFDM-AB
#if __XCVR_OFDM_AB
uint8_t setOFDMAB(void)
{
	activeMenu = OFDM_AB_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
#endif
// C: Setup for WL33
#if __XCVR_WL33
uint8_t setWL33(void)
{
	activeMenu = WL33_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
#endif
// L: print radio setup
#if __XCVR_AT86
uint8_t print_AT86(void)
{
	at86_PrintRadioSetup();
	return RET_PAUSE;
}
#endif
#if __XCVR_OFDM_AB
uint8_t print_OFDM_AB(void)
{
	OFDM_AB_PrintRadioSetup();
	return RET_PAUSE;
}
#endif
#if __XCVR_WL33
uint8_t print_WL33(void)
{
	wl33_PrintRadioSetup();
	return RET_PAUSE;
}
#endif
// P: apply settings now
uint8_t applySettings(void)
{
	ApplySetup(xcvrNum);
	return RET_PAUSE;
}
/*
 * Station menu items (most are also handle by setParam)
 */
// L: print station setup
uint8_t printStnSetup(void)
{
	printStationSetup();
	return RET_PAUSE;
}

/*
 * Diagnostics menu items
 */
// D: dump radio stats
uint8_t showstats(void)
{
	for(int i=0;i<getNxcvrs();i++)	{
		Print_Radio_stats(i);
		Print_Buffer_stats(i);
	}

	Print_Frame_stats(GetFrameStats());
	return RET_PAUSE;
}
// E: Reset frame stats
uint8_t resetstats(void)
{
	for(int i=0;i<getNxcvrs();i++)	{
		RADIO_STATS *stats = GetRadioStats(i);
		memset(stats, 0, sizeof(RADIO_STATS));
		resetBufferStats(i);
	}

	FRAME_STATS *fr = GetFrameStats();
	memset(fr, 0, sizeof(FRAME_STATS));
	ResetSPIStats();

	USART_Print_string("Statistics reset\r\n\n");
	return RET_PAUSE;
}
// F: set the flash update menu
uint8_t setFlash(void)
{
	activeMenu = FLASH_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
// G: GPS Echo mode
uint8_t gpsEcho(void)
{
#if __ENABLE_GPS
	GPSEcho();
#endif
	return(getKeyEntry());
}
// I: SPI stats
uint8_t spistats(void)
{
	PrintSPIStats();
	return RET_PAUSE;
}
// L: Led Test
uint8_t ledTest(void)
{
	if(LedTest())
		return RET_PAUSE;
	return RET_MORE;
}
// M: memory status
uint8_t memStats(void)
{
	Print_Memory_Stats();
	return RET_PAUSE;
}
// P: Set PRBS diag mode or PRBS mode
#if _HAS_CODEC
uint8_t setCodec(void)
{
	activeMenu = CODEC_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
#else
uint8_t prbsMode(void)
{
	runXcvrTest(xcvrNum,XCVR_TEST_PATTERN);
	sendTextString("Pattern test mode enabled\r\n");
	return RET_PAUSE;
}
#endif

// Q: Set loopback mode
uint8_t loopMode(void)
{
	runXcvrTest(xcvrNum, XCVR_TEST_ANALB);
	sendTextString("Analog Loopback enabled\r\n");
	return RET_PAUSE;
}
// T: Set CW mode/Digital loopback
uint8_t cwMode(void)
{
	runXcvrTest(xcvrNum,XCVR_TEST_CW);
	sendTextString("CW test mode enabled\r\n");
	return RET_PAUSE;
}
// X: Test mode off
uint8_t testOff(void)
{

	runXcvrTest(xcvrNum,XCVR_TEST_OFF);
	sendTextString("Test mode off\r\n");
	return RET_PAUSE;
}
// Z: exit diag mode: turn test modes off
uint8_t exitdiagMenu(void)
{
	for(int i=0;i<getNxcvrs();i++)
		runXcvrTest(xcvrNum,XCVR_TEST_OFF);
	return exitMenu();
}
/*
 * Flash memory updates
 */

// F: Update FPGA Flash
uint8_t fpgaUpdate(void)
{
#if _HAS_FPGA
	USART_Print_string("Start FPGA Upload...");
	FPGALoader(UART_DEBUG);
#else
	USART_Print_string("?No FPGA Installed...\r\n");
#endif
	return RET_PAUSE;
}
// J: program FPGA through USB
uint8_t fpgaUpdateJtag(void)
{
	USART_Print_string("Setting SS..\r\n");
	return RET_PAUSE;
}
// U: program CPU from USB
uint8_t cpuUsb(void)
{
	USART_Print_string("Enable flash load in 10 seconds...\r\n");
	return RET_PAUSE;
}

/*
 * Codec tests (for those equipped
 */
#if _HAS_CODEC
// A: Analog loopback
uint8_t codecAnalog(void)
{
	for(int i=0;i<getNxcvrs();i++)
		runXcvrTest(xcvrNum,CODEC_TEST(CODEC_TEST_ANALOG_LB));
	return RET_PAUSE;
}
// B: Analog loopback with AGC
uint8_t codecAnalogAGC(void)
{
	for(int i=0;i<getNxcvrs();i++)
		runXcvrTest(xcvrNum,CODEC_TEST(CODEC_TEST_ANALOG_LB_AGC));
	return RET_PAUSE;
}
// D: Digital loopback
uint8_t codecDigital(void)
{
	for(int i=0;i<getNxcvrs();i++)
		runXcvrTest(xcvrNum,CODEC_TEST(CODEC_TEST_DIGITAL_LB));
	return RET_PAUSE;
}
// G: AGC
uint8_t codecAGC(void)
{
	for(int i=0;i<getNxcvrs();i++)
		runXcvrTest(xcvrNum,CODEC_TEST(CODEC_TEST_DIGITAL_LB_AGC));
	return RET_PAUSE;
}
// T: 1 KHz tone
uint8_t codecTone(void)
{
	for(int i=0;i<getNxcvrs();i++)
		runXcvrTest(xcvrNum,CODEC_TEST(CODEC_TEST_TONE));
	return RET_PAUSE;
}
#endif

/*
 * main menu definition
 * 1) define the number of line items
 * 2) create menuitms struct
 * 3) add to menucontents struct
 */
struct menuItems_t {
		char	*menuLine;			// text of menu line
		char	selChar;			// character to select it
		uint8_t	(*func)(void);		// processing function
		uint8_t	entryMode;			// entry mode
		uint8_t	fldSize;			// size of entry field
};
//
#define N_MAINMENU	9
struct menuItems_t mainMenu[N_MAINMENU] = {
		{ "List setup parameters\r\n", 'A', printAllSetup, ENTRY_NONE, 0 },
		{ "Mesh Status\r\n", 'B', listMesh, ENTRY_NONE, 0 },
		{ "Chat/Echo Mode\r\n", 'C', chatMode, ENTRY_NONE, 0 },
		{ "Diagnostics\r\n", 'D', setDiagnostics, ENTRY_NONE, 0 },
		{ "Send Beacon\r\n", 'E', sendBeacon, ENTRY_NONE, 0 },
		{ "Set Radio Parameters\r\n", 'R', setRadio, ENTRY_NONE, 0 },
		{ "Set Station Parameters\r\n", 'S', setStation, ENTRY_NONE, 0 },
		{ "Set clock (HH:MM)\r\n\n", 'T', setParam, ENTRY_TIME, MAX_TIMESIZE },
		{ "Write Setup Values to memory\r\n\n", 'W', writeSetup, ENTRY_NONE, 0 }
};
// sub-menu for radio selection
#define	N_RADIOMENU	N_XCVRS+1
struct menuItems_t RadioMenu[N_RADIOMENU] = {
#if __XCVR_WL33
		{ "WL33 FSK Transceiver\r\n", 'W', setWL33, ENTRY_NONE, 0 },
#endif
#if __XCVR_AT86
		{ "AT86RF215 FSK Transceiver\r\n", 'A', setAT86, ENTRY_NONE, 0 },
#endif
#if __XCVR_OFDM_AB
		{ "OFDM-AB Transceiver\r\n", 'B', setOFDMAB, ENTRY_NONE, 0 },
#endif
		{ "Return to main menu\r\n\r\n", 'Z', exitMenu, ENTRY_NONE, 0 }
};

/*
 * Menus for individual radio types
 */
// placeholder for AT86
#if __XCVR_AT86
#define N_AT86MENU	5
struct menuItems_t at86_Menu[N_AT86MENU] = {
		{ "RF Frequency\r\n", 'A', setParam, ENTRY_FLOAT, MAX_FLDSIZE },
		{ "Output Power (dBm)\r\n\r\n", 'B', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "List Settings\r\n", 'L', print_AT86, ENTRY_NONE, 0 },
		{ "Apply Settings\r\n", 'P', applySettings, ENTRY_NONE, 0 },
		{ "Return to main menu\r\n\r\n", 'Z', exitMenu, ENTRY_NONE, 0 }
};
//
// validators for wl33 mmenu
FIELD_VALIDATOR at86_Validators[] = {
		{ MIN_FREQ, 450000000, &setup_memory.params.radio_data[XCVR_AT86_SUBG].lFrequencyBase, uint32_type, 1000000 },
		{ 0, 20, &setup_memory.params.radio_data[XCVR_AT86_SUBG].outputPower, uint8_type, 1 },
};
// Print AT86 radio setup
void at86_PrintRadioSetup(void)
{
	// dump the radio init struct
	uint16_t fWhole = setup_memory.params.radio_data[XCVR_AT86_SUBG].lFrequencyBase/1e6;
	uint16_t fFract = setup_memory.params.radio_data[XCVR_AT86_SUBG].lFrequencyBase/1e3 - fWhole*1e3;
	USART_Print_string("RF Frequency->%d.%d MHz\r\n", fWhole, fFract);

	USART_Print_string("Modulation method->%s\r\n", modTypes[setup_memory.params.radio_data[XCVR_AT86_SUBG].xModulationSelect]);

	uint16_t dWhole = setup_memory.params.radio_data[XCVR_AT86_SUBG].lDatarate/1000;
	uint16_t dFract = setup_memory.params.radio_data[XCVR_AT86_SUBG].lDatarate - dWhole*1000;
	USART_Print_string("Data Rate->%d.%d Kbps\r\n", dWhole, dFract);

	uint16_t pWhole = setup_memory.params.radio_data[XCVR_AT86_SUBG].lFreqDev/1000;
	uint16_t pFract = setup_memory.params.radio_data[XCVR_AT86_SUBG].lFreqDev - pWhole*1000;
	USART_Print_string("Peak Deviation->%d.%d KHz\r\n", pWhole, pFract);

	uint16_t bWhole = setup_memory.params.radio_data[XCVR_AT86_SUBG].lBandwidth/1000;
	uint16_t bFract = setup_memory.params.radio_data[XCVR_AT86_SUBG].lBandwidth - bWhole*1000;
	USART_Print_string("Channel Filter Bandwidth->%d.%d KHz\r\n", bWhole, bFract);

	USART_Print_string("Output Power->%d dBm\r\n", setup_memory.params.radio_data[XCVR_AT86_SUBG].outputPower);
	USART_Print_string("Rx Squelch->%d\r\n\r\n", setup_memory.params.radio_data[XCVR_AT86_SUBG].rxSquelch);
}
#endif
// OFDM audio mode
#if __XCVR_OFDM_AB
#define N_OFDM_ABMENU	10
struct menuItems_t ofdm_ab_Menu[N_OFDM_ABMENU] = {
		{ "Default Constellation\r\n", 'C', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "BandWidth\r\n", 'D', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "Port Select\r\n", 'E', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "ADC Gain\r\n", 'F', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "DAC Gain\r\n", 'G', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "Default FEC\r\n", 'H', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "Tx Key Delay (10ms incr)\r\n\n", 'T', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "List Settings\r\n", 'L', print_OFDM_AB, ENTRY_NONE, 0 },
		{ "Apply Settings\r\n", 'P', applySettings, ENTRY_NONE, 0 },
		{ "Return to main menu\r\n\n\n", 'Z', exitMenu, ENTRY_NONE, 0 }
};
// validators for OFDM_AB mmenu
FIELD_VALIDATOR ofdm_ab_Validators[] = {
		{ 0, 7, &setup_memory.params.radio_data[XCVR_OFDM].xModulationSelect, uint8_type, 1 },
		{ 0, 3, &setup_memory.params.radio_data[XCVR_OFDM].bandwidth, uint8_type, 1 },
		{ 0, 1, &setup_memory.params.radio_data[XCVR_OFDM].portSelect, uint8_type, 1 },
		{ 1, 255, &setup_memory.params.radio_data[XCVR_OFDM].adcGain, uint8_type, 1 },
		{ 0, 9, &setup_memory.params.radio_data[XCVR_OFDM].dacGain, uint8_type, 1 },
		{ 0, 2, &setup_memory.params.radio_data[XCVR_OFDM].defFEC, uint8_type, 1 },
		{ 0, 100, &setup_memory.params.radio_data[XCVR_OFDM].txDelay, uint8_type, 1 },
};
// Print OFDM-AB radio setup
// modulation types
char *ofdm_ab_modTypes[] = {
		"BPSK",
		"QPSK",				// 2 bits/symbol
		"QPSK-8",
		"QAM-16",
		"QAM-32",
		"QAM-64",
		"QAM-128",
		"QAM-256"			// 8 bits/symbol
};
char *ofdm_ab_bandwidth[] = {
		"Narrow Band",
		"Enhanced Narrow Band",
		"Wideband",
		"Enhanced Wideband"
};
char *fec_methods[] = {
		"none",
		"CCITT CRC-16",
		"Trellis Coding"
};
void OFDM_AB_PrintRadioSetup(void)
{
	// dump the radio init struct
	USART_Print_string("Default constellation->%s\r\n", ofdm_ab_modTypes[setup_memory.params.radio_data[XCVR_OFDM].xModulationSelect]);
	USART_Print_string("Bandwidth ->%s\r\n", ofdm_ab_bandwidth[setup_memory.params.radio_data[XCVR_OFDM].bandwidth]);
	USART_Print_string("FEC Method ->%s\r\n", fec_methods[setup_memory.params.radio_data[XCVR_OFDM].defFEC]);
	USART_Print_string("Port Select ->%d\r\n", setup_memory.params.radio_data[XCVR_OFDM].portSelect);
	USART_Print_string("ADC Gain->%d\r\n", setup_memory.params.radio_data[XCVR_OFDM].adcGain);
	USART_Print_string("DAC Gain->%d\r\n", setup_memory.params.radio_data[XCVR_OFDM].dacGain);
	USART_Print_string("Tx Key Delay->%d ms\r\n\n", setup_memory.params.radio_data[XCVR_OFDM].txDelay*XCVR_TASK_SCHED);
}
#endif
// WL33 400 MHz transceiver
#if __XCVR_WL33
#define N_WL33MENU	11
struct menuItems_t wl33_Menu[N_WL33MENU] = {
		{ "RF Frequency\r\n", 'A', setParam, ENTRY_FLOAT, MAX_FLDSIZE },
		{ "Data Rate\r\n", 'B', setParam, ENTRY_FLOAT, MAX_FLDSIZE },
		{ "Peak Deviation\r\n", 'C', setParam, ENTRY_FLOAT, MAX_FLDSIZE },
		{ "Channel Filter BW\r\n", 'D', setParam, ENTRY_FLOAT, MAX_FLDSIZE },
		{ "PA drive mode\r\n", 'E', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "Output Power (dBm)\r\n", 'F', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "Rx Squelch (dBm)\r\n\n", 'G', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "Modulation Method\r\n", 'M', setParam, ENTRY_FLOAT, MAX_FLDSIZE },
		{ "List Settings\r\n", 'L', print_WL33, ENTRY_NONE, 0 },
		{ "Apply Settings\r\n", 'P', applySettings, ENTRY_NONE, 0 },
		{ "Return to main menu\r\n\n\n", 'Z', exitMenu, ENTRY_NONE, 0 }
};
// validators for wl33 mmenu
FIELD_VALIDATOR wl33_Validators[] = {
		{ MIN_FREQ, 450000000, &setup_memory.params.radio_data[XCVR_WL33].lFrequencyBase, uint32_type, 1000000 },
		{ 9600, 600000, &setup_memory.params.radio_data[XCVR_WL33].lDatarate, uint32_type, 1000 },
		{ 12500, 150000, &setup_memory.params.radio_data[XCVR_WL33].lFreqDev, uint32_type, 1000 },
		{ 2600, 1600000, &setup_memory.params.radio_data[XCVR_WL33].lBandwidth, uint32_type, 1000 },
#ifdef __NUCLEOCC2
		{ 2, 2, &setup_memory.params.radio_data[XCVR_WL33].PADrvMode, uint8_type, 1 },
		{ 0,14, &setup_memory.params.radio_data[XCVR_WL33].outputPower, uint8_type, 1 },
#else
		{ 1, 3, &setup_memory.params.radio_data[XCVR_WL33].PADrvMode, uint8_type, 1 },
		{ 0, 20, &setup_memory.params.radio_data[XCVR_WL33].outputPower, uint8_type, 1 },
#endif
		{ -115, 0, &setup_memory.params.radio_data[XCVR_WL33].rxSquelch, int16_type, 1 },
		{ 0, 3,    &setup_memory.params.radio_data[XCVR_WL33].xModulationSelect, uint8_type, 1 }
};
//
void wl33_PrintRadioSetup(void)
{
	// dump the radio init struct
	uint16_t fWhole = setup_memory.params.radio_data[XCVR_WL33].lFrequencyBase/1e6;
	uint16_t fFract = setup_memory.params.radio_data[XCVR_WL33].lFrequencyBase/1e3 - fWhole*1e3;
	USART_Print_string("RF Frequency->%d.%d MHz\r\n", fWhole, fFract);

	USART_Print_string("Modulation method->%s\r\n", modTypes[setup_memory.params.radio_data[XCVR_WL33].xModulationSelect]);

	uint16_t dWhole = setup_memory.params.radio_data[XCVR_WL33].lDatarate/1000;
	uint16_t dFract = setup_memory.params.radio_data[XCVR_WL33].lDatarate - dWhole*1000;
	USART_Print_string("Data Rate->%d.%d Kbps\r\n", dWhole, dFract);

	uint16_t pWhole = setup_memory.params.radio_data[XCVR_WL33].lFreqDev/1000;
	uint16_t pFract = setup_memory.params.radio_data[XCVR_WL33].lFreqDev - pWhole*1000;
	USART_Print_string("Peak Deviation->%d.%d KHz\r\n", pWhole, pFract);

	uint16_t bWhole = setup_memory.params.radio_data[XCVR_WL33].lBandwidth/1000;
	uint16_t bFract = setup_memory.params.radio_data[XCVR_WL33].lBandwidth - bWhole*1000;
	USART_Print_string("Channel Filter Bandwidth->%d.%d KHz\r\n", bWhole, bFract);

	USART_Print_string("Output Power->%d dBm\r\n", setup_memory.params.radio_data[XCVR_WL33].outputPower);
	USART_Print_string("PA Mode->%s\r\n", paModes[setup_memory.params.radio_data[XCVR_WL33].PADrvMode]);
	USART_Print_string("Rx Squelch->%d\r\n\n\n", setup_memory.params.radio_data[XCVR_WL33].rxSquelch);
}
#endif

// station menu
#if __AX25_COMPATIBILITY
#define	NAX25			2
#else
#define	NAX25			0
#endif

#define N_STATIONMENU	9+NAX25
struct menuItems_t stationMenu[N_STATIONMENU] = {
		{ "Callsign\r\n", 'A', setParam, ENTRY_UPPERCASE, MAX_CALL },
		{ "Description\r\n", 'B', setParam, ENTRY_ANYCASE, MAX_DESC },
		{ "Latitude\r\n", 'C', setParam, ENTRY_FLOAT, MAX_FLOATSIZE },
		{ "Longitude\r\n", 'D', setParam, ENTRY_FLOAT, MAX_FLOATSIZE },
		{ "Repeat Mode\r\n", 'F', setParam, ENTRY_UPPERCASE, 1 },
		{ "Beacon Interval\r\n", 'G', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
		{ "Default Modem\r\n", 'M', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
#if __AX25_COMPATIBILITY
		{ "AX.25 Compatibility Mode\r\n", 'H', setParam, ENTRY_UPPERCASE, 1 },
		{ "AX.25 SSID\r\n\n", 'I', setParam, ENTRY_NUMERIC, MAX_FLDSIZE },
#endif
		{ "List Settings\r\n", 'L', printStnSetup, ENTRY_NONE, 0 },
		{ "Return to main menu\r\n\n", 'Z', exitMenu, ENTRY_NONE, 0 }
};
// validators for station menu
FIELD_VALIDATOR stationValidators[] = {
		{ 4, MAX_CALL, &setup_memory.params.setup_data.stnCall, char_type, 0 },
		{ 1, MAX_DESC, &setup_memory.params.setup_data.Description, char_type, 0 },
		{ 2, 14, &setup_memory.params.setup_data.latitude, char_type, 0 },
		{ 2, 14, &setup_memory.params.setup_data.longitude, char_type, 0 },
		{ 0x8, 0, &setup_memory.params.setup_data.flags, yesno_type, 0 },
		{ 1, 100, &setup_memory.params.setup_data.beaconInt, int16_type, 0 },
		{ 0, N_XCVRS-1, &setup_memory.params.setup_data.defModem, int16_type, 0 },
		{ 0x4, 0, &setup_memory.params.setup_data.flags, yesno_type, 0 },
		{ 0, 15, &setup_memory.params.setup_data.flags, uint4_hi, 0 }
};

// diagnostics menu
#define N_DIAGMENU	11
struct menuItems_t diagMenu[N_DIAGMENU] = {
		{ "Show Frame stats\r\n", 'D', showstats, ENTRY_NONE, 0 },
		{ "Reset all stats\r\n", 'E', resetstats, ENTRY_NONE, 0 },
		{ "Flash memory update\r\n", 'F', setFlash, ENTRY_NONE, 0 },
		{ "GPS Echo mode\r\n", 'G', gpsEcho, ENTRY_NONE, 0 },
		{ "SPI Stats\r\n", 'I', spistats, ENTRY_NONE, 0 },
		{ "LED test\r\n", 'L', ledTest, ENTRY_NONE, 0 },
		{ "Memory Status\r\n", 'M', memStats, ENTRY_NONE, 0 },
#if _HAS_CODEC
		{ "Codec Tests\r\n", 'P', setCodec, ENTRY_NONE, 0 },
#else
		{ "Transmit PRBS Sequence\r\n", 'P', prbsMode, ENTRY_NONE, 0 },
#endif
		{ "Transmit CW Mode\r\n", 'T', cwMode, ENTRY_NONE, 0 },
		{ "Transmit Test Off\r\n\n", 'X', testOff, ENTRY_NONE, 0 },
		{ "Return to main menu\r\n\n", 'Z', exitdiagMenu, ENTRY_NONE, 0 }
};

#define _HAS_FPGAZ		1

#if _HAS_FPGA
#define N_FPGA_FLASH	2
#else
#define N_FPGA_FLASH	0
#endif
#define N_FLASHMENU	2+N_FPGA_FLASH

// flash memory updated
struct menuItems_t progMenu[N_FLASHMENU] = {

#if _HAS_FPGA
		{ "Program FPGA Flash using Debug UART\r\n", 'F', fpgaUpdate, ENTRY_NONE, 0 },
		{ "Program FPGA Flash using JTAG\r\n", 'J', fpgaUpdateJtag, ENTRY_NONE, 0 },
#endif
		{ "Program CPU Flash from USB\r\n\n", 'U', cpuUsb, ENTRY_NONE, 0 },
		{ "Return to main menu\r\n\n", 'Z', exitMenu, ENTRY_NONE, 0 }
};


#if _HAS_CODEC
// codec menu
#define N_CODECMENU	6
struct menuItems_t codecMenu[N_CODECMENU] = {
		{ "Analog Loop back AGC OFF\r\n", 'A', codecAnalog, ENTRY_NONE, 0 },
		{ "Analog Loop back AGC ON\r\n", 'B', codecAnalogAGC, ENTRY_NONE, 0 },
		{ "Digital Loop back AGC OFF\r\n", 'C', codecDigital, ENTRY_NONE, 0 },
		{ "Digital Loop back AGC ON\r\n", 'D', codecAGC, ENTRY_NONE, 0 },
		{ "1500Hz Tone Test\r\n\n", 'T', codecTone, ENTRY_NONE, 0 },
		{ "Return to main menu\r\n\n", 'Z', exitdiagMenu, ENTRY_NONE, 0 }
};
#endif

// these need to correspond to the items above

// menu contents
struct menuContents_t {
	char				*title;				// title of the menu
	int 				nMenuLines;			// number of lines
	struct menuItems_t	*menus;				// menu items
	FIELD_VALIDATOR		*validators;		// entry validator
} menuContents[N_MENUS] = {
#if defined(__SUPERNODE_F722)
		{ "      IP400 Advanced Mesh Network Controller Menu\r\n", N_MAINMENU, mainMenu, NULL },
#endif
#if defined(__T20_ADD_ON)
		{ "      IP400 Pi HAT Prototype Menu\r\n", N_MAINMENU, mainMenu, NULL },
#endif
#if defined(__NUCLEOCC2)
		{ "      WL33 Nucleo module Menu\r\n", N_MAINMENU, mainMenu, NULL },
#endif
#if defined(__PI_BOARD)
		{ "      WL33 Mini node Menu\r\n", N_MAINMENU, mainMenu, NULL },
#endif
		{ "      Radio setup menu\r\n", N_RADIOMENU, RadioMenu, NULL },
		{ "      Station Setup menu\r\n", N_STATIONMENU, stationMenu, stationValidators },
		{ "      Diagnostics menu\r\n", N_DIAGMENU, diagMenu, NULL },
		{ "      Flash memory menu\r\n", N_FLASHMENU, progMenu, NULL },
#if _HAS_CODEC
		{ "      Codec Tests\r\n", N_CODECMENU, codecMenu, NULL },
#endif
#if __XCVR_AT86
		{ "      AT86RF215 FSK Transceiver\r\n", N_AT86MENU, at86_Menu, at86_Validators},
#endif
#if __XCVR_OFDM_AB
		{ "      OFDM-AB Transceiver\r\n", N_OFDM_ABMENU, ofdm_ab_Menu, ofdm_ab_Validators},
#endif
#if __XCVR_WL33
		{ "      WL33 FSK Transceiver\r\n", N_WL33MENU, wl33_Menu,  wl33_Validators},
#endif
};

// menu (main) task
void Menu_Task_Init(void)
{
	// start off with the menu showing
	activeMenu = MAIN_MENU;
	menuState = MENU_SHOWING;
	entryState = NO_ENTRY;
#if __INCLUDE_KISS
	if(isAX25Enabled())
		KissInit();
#endif
	processingKiss = FALSE;
}

// send a control code
void sendControlCode(uint8_t code)
{
	USART_Send_String(controlCodes[code].sequence, controlCodes[code].len);
}

// send a control code
void sendTextString(char *string)
{
	strcpy(menu, string);
	USART_Send_String(menu, strlen(string));
}

// process our time slot
void Menu_Task_Exec(void)
{
	int nBytesinBuff = 0;
	char c;
	struct menuItems_t *m;

#if __INCLUDE_KISS
	// processing a KISS frame preempts menu
	if(processingKiss && isAX25Enabled())	{
		processingKiss = processKissFrame();
		return;
	}
#endif

	switch(menuState)	{

	// if there is a return in the console buffer,
	// bring  up the menu
	case MENU_OFF:
		if((nBytesinBuff=USART_databuffer_bytesInBuffer()) == 0)
			return;
		for(int i=0;i<nBytesinBuff;i++)
			if((c=USART_databuffer_get(0)) == ASCII_RET)
				menuState = MENU_SHOWING;
		return;

	case MENU_SHOWING:
		printMenu();
		menuState = MENU_SELECTING;
		return;

	case MENU_SELECTING:
		sel_item = getMenuItem();

		switch(sel_item)		{

		case NO_ITEM:
			return;

		case MENU_KISSMODE:
			processingKiss = TRUE;
			return;

		case MENU_REPAINT:
			menuState = MENU_SHOWING;
			break;

		default:
			menuState = MENU_SELECTED;
			break;

		}
		break;

	case MENU_SELECTED:
		m=menuContents[activeMenu].menus;
		m += sel_item;
		editMode = m->entryMode;
		maxEntry = m->fldSize;
		switch((*m->func)()) {

		case RET_MORE:
			break;

		case RET_PAUSE:
			sendTextString(pauseString);
			menuState = MENU_PAUSED;
			break;

		case RET_DONE:
			menuState = MENU_SHOWING;
			break;
		}
		break;

	case MENU_PAUSED:
		if(pause())
			menuState = MENU_SHOWING;
		break;

	}
}

// print the main menu
void printMenu(void)
{
	struct menuItems_t *m;

	// title
	sendControlCode(CONTROL_CLEAR);
	sendControlCode(CONTROL_HOME);

#ifdef __NUCLEOCC2
	// only use double wide mode with Nucleo
	sendControlCode(CONTROL_DOUBLE_TOP);
	sendTextString(menuContents[activeMenu].title);
	sendControlCode(CONTROL_DOUBLE_BOTTOM);
	sendTextString(menuContents[activeMenu].title);
	sendControlCode(CONTROL_SINGLE);
	sendTextString(menuSpacer);
#else
	sendTextString(menuContents[activeMenu].title);
	sendTextString(menuSpacer);
#endif

	// lines
	int nMenuLines = menuContents[activeMenu].nMenuLines;
	for(int i=0;i<nMenuLines;i++)	{
		m=menuContents[activeMenu].menus;
		m += i;
		menu[0] = m->selChar;
		strcpy(&menu[1], ") ");
		strcat(menu, m->menuLine);
		USART_Send_String(menu, strlen(menu));
	}

	// selection
	sendTextString(selectItem);
}

// get a menu item and dispatch the correct processing routine
int getMenuItem(void)
{
	int nBytesinBuff =0;
	struct menuItems_t *m;
	USART_ELEMENT key;

	if((nBytesinBuff=USART_databuffer_bytesInBuffer()) == 0)
		return NO_ITEM;

	for(int i=0;i<nBytesinBuff;i++)		{
		if((key=USART_databuffer_get(0)) != BUFFER_NO_DATA)	{

			char c = (char)key&0xFF;
			if(c == KEY_EOL)
				return MENU_REPAINT;
	// kiss mode can share the UAR/T
#if KISS_ON_LPUART == 0
			if((c == KISS_FEND) && isAX25Enabled())
				return MENU_KISSMODE;
#endif

			// translate and echo
			c = islower(c) ? toupper(c) : c;
			USART_Send_Char(c);

			// find the correct processing routine
			int nMenuLines = menuContents[activeMenu].nMenuLines;
			for(int j=0;j<nMenuLines;j++)	{
				m=menuContents[activeMenu].menus;
				m += j;
				if(m->selChar == c)	{
					sendTextString(menuSpacer);
					return j;
				}
			}
		}
	}
	sendTextString(whatItem);
	return NO_ITEM;
}

BOOL pause(void)
{
	if(USART_databuffer_bytesInBuffer() == 0)
		return FALSE;

	char c = (char)USART_databuffer_get(0);
	if(c == ASCII_RET)
		return TRUE;

	return FALSE;
}

/*
 * This part pertains to getting an entry and validating it..
 */


// forward refs
uint8_t validateEntry(int activeMenu, int item, char *keyBuffer);

/*
 * Get a key entry: basically stolen from chat.c
 */
uint8_t getKeyEntry(void)
{

	char c;
	char edited;
	int nBytesinBuff;

	if((nBytesinBuff=USART_databuffer_bytesInBuffer()) == 0)
		return RET_MORE;

	for(int i=0;i<nBytesinBuff;i++)		{

		c=(char)USART_databuffer_get(0) & 0xFF;

		if(delMode)	{
			if((c != KEY_DEL) && (c != KEY_BKSP))	{
				USART_Print_string("\\%c", c);
				if(pos < maxEntry)
					keyBuffer[pos++] = c;
				delMode = FALSE;
			} else {
				if(pos > 0)	{
					USART_Print_string("%c", keyBuffer[--pos]);
				} else {
					USART_Print_string("\\\r\n->");
					delMode = FALSE;
				}
			}
			continue;
		} else {
		// processing a key

			switch (c)	{

			// EOL key: sent the packet
			case KEY_EOL:
				USART_Print_string("\r\n");
				keyBuffer[pos++] = '\0';
				return RET_DONE;
				break;

			// escape key: abort the entry
			case KEY_ESC:
				return RET_PAUSE;
				break;

			case KEY_DEL:
			case KEY_BKSP:
				if(pos > 0)	{
					USART_Print_string("\\%c", keyBuffer[--pos]);
					delMode = TRUE;
				} else {
					delMode = FALSE;
				}

				break;

			default:
				if((edited=editEntry(c)) != 0)	{
					// don't echo the entry if it is over the field size
					if(pos < maxEntry)	{
						USART_Send_Char(edited);
						keyBuffer[pos++] = edited;
					}
				}
				break;
			}
		}
	}
	return RET_MORE;
}

/*
 * edit an entry in progress
 */
char editEntry(char c)
{
	switch(editMode)	{

	case ENTRY_ANYCASE:		// upper and lower case
	case ENTRY_NONE:		// none of the above
		return (c);

	case ENTRY_UPPERCASE:	// upper case only
		if(isLower(c))
			return(toupper(c));
		return c;

	case ENTRY_LOWERCASE:	// lower case only
		if(isUpper(c))
			return(tolower(c));
		return c;

	case ENTRY_NUMERIC:		// numeric only, 0-9 + '-'
		if(isNumeric(c) || (c=='-'))
			return c;
		return 0;

	case ENTRY_FLOAT:		// floating point 0-9 + '.' + '-'
		if(isNumeric(c) || (c=='-') || (c=='.'))
			return c;
		return 0;

	case ENTRY_TIME:		// numeric + ':'
		if(isNumeric(c) || (c==':'))
			return c;
		return 0;

	}
	return c;
}

/*
 * Get an entry and validate it
 */
uint8_t getEntry(int activeMenu, int item)
{
	struct menuItems_t *m;

	switch(entryState)	{

	case NO_ENTRY:
		delMode = FALSE;
		pos = 0;
		entryState = ENTERING;
		m=menuContents[activeMenu].menus;
		m += item;
		USART_Print_string("%s", m->menuLine);
		printValidator(activeMenu, item);
		USART_Print_string("->", m->menuLine);
		return RET_MORE;

	case ENTERING:
		int keyStat = getKeyEntry();
		if(keyStat == RET_DONE)	{
			entryState = VALIDATING;
			return RET_MORE;
		}
		return keyStat;

	case VALIDATING:
		entryState = NO_ENTRY;
		return validateEntry(activeMenu, item, keyBuffer);
	}

	return RET_MORE;
}

// print a validator
void printValidator(int activeMenu, int item)
{
	int flMinW, flMaxW;
	int flMinF, flMaxF;
	uint32_t scalar;

	FIELD_VALIDATOR *radioValidators = (FIELD_VALIDATOR *)menuContents[activeMenu].validators;

	switch(activeMenu)	{

	// only item is the clock
	case MAIN_MENU:
		USART_Print_string("(HH:MM:DD)");
		break;

	// these are basically all numeric
	case RADIO_MENU:
		scalar = radioValidators[item].scalar;
		if(scalar > 1)	{
			flMinW = radioValidators[item].MinVal/scalar;
			flMaxW = radioValidators[item].MaxVal/scalar;
			flMinF = radioValidators[item].MinVal % scalar;
			flMaxF = radioValidators[item].MaxVal % scalar;
			USART_Print_string("(%d.%03d to %d.%03d)", flMinW, flMinF, flMaxW, flMaxF);
		} else {
			USART_Print_string("(%d to %d)", radioValidators[item].MinVal, radioValidators[item].MaxVal);
		}
		break;

	// various types
	case STATION_MENU:

		switch(stationValidators[item].type){

		case yesno_type:
			USART_Print_string("(Y or N)");
			break;

		case char_type:
			USART_Print_string("(%d to %d characters)", stationValidators[item].MinVal, stationValidators[item].MaxVal);
			break;

		default:
			USART_Print_string("(%d to %d)", stationValidators[item].MinVal, stationValidators[item].MaxVal);
			break;
		}
		break;
	}
}

uint8_t validateEntry(int activeMenu, int item, char *keyBuffer)
{

	uint32_t newValue, min, max;

	if(strlen(keyBuffer) == 0)
		return RET_DONE;

	FIELD_VALIDATOR *radioValidators = (FIELD_VALIDATOR *)menuContents[activeMenu].validators;

	//NB: the cases here must jive with the menu items
	switch(activeMenu)	{

	case MAIN_MENU:			// the only menu item here is the clock
		setTOD(keyBuffer);
		break;

#if __XCVR_AT86
		case AT86_MENU:			// setup menu for an AT86
#endif
#if __XCVR_OFDM_AB
		case OFDM_AB_MENU:		// setup menu for OFDM-AB
#endif
#if __XCVR_WL33
	case WL33_MENU:			// setup menu for WL33
#endif
		// convert a floating point entry to the required decimal
		if(isfloat(keyBuffer))
			newValue = (uint32_t)(ascii2double(keyBuffer)*radioValidators[item].scalar);
		else 	newValue = ascii2Dec(keyBuffer);
		max = radioValidators[item].MaxVal;
		min = radioValidators[item].MinVal;

		if(radioValidators[item].type == int16_type)	{
			int16_t inewValue = (int16_t)newValue;
			if((inewValue<(int16_t)radioValidators[item].MinVal) || (inewValue>(int16_t)radioValidators[item].MaxVal))	{
				USART_Print_string("Must be in the range of %d to %d\r\n", min, max);
				USART_Print_string("Field not updated\r\n");
				return RET_PAUSE;
			}
		} else	{
			if((newValue<min) || (newValue>max))	{
				USART_Print_string("Must be in the range of %u to %u\r\n", min, max);
				USART_Print_string("Field not updated\r\n");
				return RET_PAUSE;
			}
		}

	switch(radioValidators[item].type){

		case uint8_type:
			uint8_t *v8 = (uint8_t *)radioValidators[item].setupVal;
			*v8 = (uint8_t)newValue;
			break;

		case int16_type:
			int16_t *v16 = (int16_t *)radioValidators[item].setupVal;
			*v16 = (int16_t)newValue;
			break;

		case uint32_type:
			uint32_t *v32 = (uint32_t *)radioValidators[item].setupVal;
			*v32 = (uint32_t)newValue;
			break;
		}

		break;

		case STATION_MENU:
			switch(stationValidators[item].type){

			case uint4_lo:
				newValue = ascii2Dec(keyBuffer);
				uint8_t *v4lo = (uint8_t *)stationValidators[item].setupVal;
				*v4lo = (*v4lo & 0xF0) | (newValue & 0x0F);
				break;

			case uint4_hi:
				newValue = ascii2Dec(keyBuffer);
				uint8_t *v4hi = (uint8_t *)stationValidators[item].setupVal;
				*v4hi = (*v4hi & 0x0F) | (newValue<<4);
				break;

			case int16_type:
				newValue = ascii2Dec(keyBuffer);
				max = stationValidators[item].MaxVal;
				min = stationValidators[item].MinVal;
				if((newValue<min) || (newValue>max))	{
					USART_Print_string("Must be in the range of %d to %d\r\n", min, max);
					USART_Print_string("Field not updated\r\n");
					return RET_PAUSE;
				}
				uint16_t *v8 = (uint16_t *)stationValidators[item].setupVal;
				*v8 = (uint16_t)newValue;
				break;

			case char_type:
				size_t len = strlen(keyBuffer);
				max = stationValidators[item].MaxVal;
				min = stationValidators[item].MinVal;
				if((len<min) || (len>max))	{
					USART_Print_string("String must be %d to %d in length\r\n", min, max);
					USART_Print_string("Field not updated\r\n");
					return RET_PAUSE;
				}
				strcpy((char *)stationValidators[item].setupVal, keyBuffer);
				break;

			case yesno_type:
				if((keyBuffer[0] == 'Y') || (keyBuffer[0] == 'N'))	{
					uint8_t val = keyBuffer[0] == 'Y' ? 1 : 0;
					uint8_t *f8= (uint8_t *)stationValidators[item].setupVal;
					if(val)
						*f8 |= (uint8_t)stationValidators[item].MinVal;
					else
						*f8 &= ~((uint8_t)stationValidators[item].MinVal);
				} else {
					USART_Print_string("Please enter Y or N\r\n");
					USART_Print_string("Field not updated\r\n");
					return RET_PAUSE;
				}
			}
			break;
	}

	// falls to here when done...
	return RET_DONE;
}


/*
 * Print the radio stats
 */
void Print_Radio_stats(int index)
{
	RADIO_STATS *stats = GetRadioStats(index);

	USART_Print_string("Statistics for Radio port %d: '%s'\r\n", index+1, getType(index));
	USART_Print_string("    FSM State: %d: %s\r\n", stats->radioFSM, stats->fsmState);
	USART_Print_string("    Code FSM State: %s\r\n", stats->codeState);
	USART_Print_string("    OFDM Sync Count: %d\r\n", stats->SyncCount);
	USART_Print_string("    Transmitted frames->%d\r\n", stats->TxFrameCnt);
	USART_Print_string("    Received frames->%d\r\n", stats->RxFrameCnt);
	USART_Print_string("    CRC Errors->%d\r\n", stats->CRCErrors);
	USART_Print_string("    Rx Timeouts->%d\r\n", stats->TimeOuts);
	USART_Print_string("    Invalid IP400 Frames->%d\r\n", stats->unprocessed);
	USART_Print_string("    Rx Dequeued Frames->%d\r\n", stats->dequeued);
}

/*
 * print the frame stats
 */
void Print_Frame_stats(FRAME_STATS *stats)
{
	USART_Print_string("Frame Statistics\r\n");

	USART_Print_string("    Frames processed->%d\r\n", stats->nProcessed);
	USART_Print_string("    Duplicate frames->%d\r\n", stats->duplicates);
	USART_Print_string("    Beacon frames->%d\r\n", stats->nBeacons);
	USART_Print_string("    Chat frames->%d\r\n", stats->nChat);
	USART_Print_string("    Kiss frames->%d\r\n", stats->nKiss);
	USART_Print_string("    Undecoded frames->%d\r\n", stats->nUndecoded);
	USART_Print_string("    Echo Request frames->%d\r\n", stats->nEchoReq);
	USART_Print_string("    Echo Reponse frames->%d\r\n", stats->nEchoResp);
	USART_Print_string("    Repeated frames->%d\r\n", stats->nRepeated);
	USART_Print_string("    Unknown frames->%d\r\n", stats->Unknown);
	USART_Print_string("    Frames with my Callsign->%d\r\n", stats->nWereMine);
	USART_Print_string("    Rejected Frames->%d\r\n", stats->nRejected);

}

/*
 * dump all memory statistics
 * uses the mallinfo structure
 */
void Print_Memory_Stats(void)
{
#if defined(__NUCLEOCC2) || defined(__PI_BOARD)
	HeapStats_t pxHeapStats;
	vPortGetHeapStats(&pxHeapStats);

	USART_Print_string("Memory Statistics\r\n\n");

	USART_Print_string("Free Heap size->%d\r\n",pxHeapStats.xAvailableHeapSpaceInBytes);      	/* The total heap size currently available - this is the sum of all the free blocks, not the largest block that can be allocated. */
	USART_Print_string("Largest Free Block (bytes)->%d\r\n",pxHeapStats.xSizeOfLargestFreeBlockInBytes);  	/* The maximum size, in bytes, of all the free blocks within the heap at the time vPortGetHeapStats() is called. */
	USART_Print_string("Smallest free block->%d\r\n",pxHeapStats. xSizeOfSmallestFreeBlockInBytes); 	/* The minimum size, in bytes, of all the free blocks within the heap at the time vPortGetHeapStats() is called. */
	USART_Print_string("Number of free blocks->%d\r\n",pxHeapStats. xNumberOfFreeBlocks);             	/* The number of free memory blocks within the heap at the time vPortGetHeapStats() is called. */
	USART_Print_string("Min EverFree bytes remaining->%d\r\n",pxHeapStats. xMinimumEverFreeBytesRemaining);  	/* The minimum amount of total free memory (sum of all free blocks) there has been in the heap since the system booted. */
#else
	size_t freeHeap = xPortGetFreeHeapSize();
	USART_Print_string("Free Heap size->%d\r\n",freeHeap);
#endif

	PrintAllocStats();
}

/*
 * Tx Buffer status
 */
// buffer state
char *bfrStates[] = {
		"READY",
		"EMPTY",
		"ACTIVE",
		"FULL",
		"UNALLOC"
};
// Timer states
char *tmrStates[] = {
	"NOT RUNNING",
	"RUNNING",
	"EXPIRED"
};

void Print_Buffer_stats(int index)
{
#if defined(__NUCLEOCC2) || defined (__PI_BOARD)
	RADIO_STATS *stats = GetRadioStats(index);
	BUFFER_STATUS *bfrStatus = (BUFFER_STATUS *)stats->bfrStatus;
	if(bfrStatus == NULL)
		return;

	USART_Print_string("Xmit Buffer Stats\r\n");
	USART_Print_string("    State: %s\r\n", bfrStates[bfrStatus->state]);
	USART_Print_string("    Timer State: %s (%d)\r\n", tmrStates[bfrStatus->tmrState], bfrStatus->tmrValue);
	USART_Print_string("    Frames Buffered->%d\r\n", bfrStatus->nFrames);
	USART_Print_string("    Buffers Xmitted->%d\r\n", bfrStatus->nXmitted);
	USART_Print_string("    Tx Buffer size->%d\r\n", bfrStatus->txSize);
	USART_Print_string("    Tx Buffer available->%d\r\n", bfrStatus->txSize-bfrStatus->length);
#endif
}
//
void resetBufferStats(int index)
{
#if defined(__NUCLEOCC2) || defined (__PI_BOARD)
	RADIO_STATS *stats = GetRadioStats(index);
	BUFFER_STATUS *bfrStatus = (BUFFER_STATUS *)stats->bfrStatus;
	if(bfrStatus == NULL)
		return;

	bfrStatus->nFrames = 0;
	bfrStatus->nXmitted = 0;
#endif
}

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

#include "types.h"
#include "usart.h"
#include "streambuffer.h"
#include "setup.h"
#include "tasks.h"
#include "utils.h"
#include "tod.h"
#include "config.h"

// menu state
uint8_t menuState;		// menu state
enum {
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
#define	__MEM_DEBUG		1			// memory debug

// DEC VT100 Escape sequences
#define ASCII_TAB		0x09
#define ASCII_RET		0x0D
#define	ASCII_ESC		0x1B			// escape char
#define	DEC_LEADIN		'['				// lead-in character
#define	MAX_SEQ			6				// max sequence length

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

// forward refs in this module
void printMenu(void);		// print the menu
int getMenuItem(void);		// get a menu item
void sendControlCode(uint8_t code);
BOOL pause(void);
void Print_Frame_stats(FRAME_STATS *stats);
void Print_Memory_Stats(void);
void Print_Radio_errors(uint32_t errs);
void Print_FSM_state(uint8_t state);
uint8_t getEntry(int activeMenu, int item);
uint8_t getKeyEntry(void);

// list of menus
enum	{
	MAIN_MENU=0,		// main menu
	RADIO_MENU,			// radio params menu
	STATION_MENU,		// Station params menu
	N_MENUS				// number of menus
};

// menu return functions
enum	{
	RET_MORE=0,			// more to do
	RET_DONE,			// done
	RET_PAUSE			// pause before leaving
};

/*
 * The first part of this code contains the menus and processing routines
 * handle the various menu options
 * return TRUE if done, else FALSE
 */
uint8_t printAllSetup(void)
{
	TIMEOFDAY tod;
	getTOD(&tod);

	USART_Print_string("Current firmware is at %d.%d\r\n",def_params.params.FirmwareVerMajor,
			def_params.params.FirmwareVerMinor);
	USART_Print_string("System time is %02d:%02d:%02d\r\n\n", tod.Hours, tod.Minutes, tod.Seconds);

	printStationSetup();
	printRadioSetup();
	return RET_PAUSE;
}
//
uint8_t printStnSetup(void)
{
	printStationSetup();
	return RET_PAUSE;
}
//
uint8_t printRadSetup(void)
{
	printRadioSetup();
	return RET_PAUSE;
}
// list mesh status
uint8_t listMesh(void)
{
	Mesh_ListStatus();
	return RET_PAUSE;
}

// enter chat mode
uint8_t chatMode(void)
{
	if(Chat_Task_exec())
		return RET_PAUSE;
	return RET_MORE;
}

// dump the rx stats
uint8_t showstats(void)
{
	Print_Frame_stats(GetFrameStats());
	Print_Radio_errors(GetRadioStatus());
	Print_FSM_state(GetFSMState());
	return RET_PAUSE;
}

// set the radio entry mode
uint8_t setRadio(void)
{
	activeMenu = RADIO_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}

// set the station entry mode
uint8_t setStation(void)
{
	activeMenu = STATION_MENU;
	menuState = MENU_OFF;
	return RET_DONE;
}
// enter chat mode
uint8_t ledTest(void)
{
	if(LedTest())
		return RET_PAUSE;
	return RET_MORE;
}
// write the flash memory
uint8_t writeSetup(void)
{
	if(!UpdateSetup())	{
		USART_Print_string("Error in writing flash: setup may be corrupt\r\n");
	} else {
		USART_Print_string("Flash written successfully\r\n");
	}
	menuState = MENU_OFF;
	return RET_PAUSE;
}
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
 * These are conditional
 */
#if __ENABLE_GPS
uint8_t gpsEcho(void)
{
	GPSEcho();
	return(getKeyEntry());
}
#endif
#if __MEM_DEBUG
// memory statistics
uint8_t memStats(void)
{
	Print_Memory_Stats();
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
};

// main menu
#if __ENABLE_GPS
#define N_GPS		1			// additional menu item for GPS
#else
#define N_GPS		0
#endif

#if __MEM_DEBUG
#define N_MEM		1			// additional menu item for memory
#else
#define N_MEM		0
#endif

#define N_MAINMENU	(11+N_GPS+N_MEM)	// additional menu item for GPS

struct menuItems_t mainMenu[N_MAINMENU] = {
		{ "List setup parameters\r\n", 'A', printAllSetup },
		{ "Mesh Status\r\n", 'B', listMesh },
		{ "Chat Mode\r\n", 'C', chatMode },
		{ "Dump Frame stats\r\n", 'D', showstats },
#if __ENABLE_GPS
		{ "GPS Echo mode\r\n", 'G', gpsEcho },
#endif
		{ "LED test\r\n", 'L', ledTest },
#if __MEM_DEBUG
		{ "Memory Status\r\n", 'M', memStats },
#endif
		{ "Set Radio Parameters\r\n", 'R', setRadio },
		{ "Set Station Parameters\r\n", 'S', setStation },
		{ "Set clock (HH:MM)\r\n\n", 'T', setParam },
		{ "Write Setup Values\r\n", 'W', writeSetup },
		{ "Exit\r\n\n", 'X', exitMenu }
};

// radio menu
#define N_RADIOMENU	8
struct menuItems_t radioMenu[N_RADIOMENU] = {
		{ "RF Frequency\r\n", 'A', setParam },
		{ "Data Rate\r\n", 'B', setParam },
		{ "Peak Deviation\r\n", 'C', setParam },
		{ "Channel Filter BW\r\n", 'D', setParam },
		{ "Output Power (dBm)\r\n", 'E', setParam },
		{ "Rx Squelch (dBm)\r\n\n", 'F', setParam },
		{ "List Settings\r\n", 'L', printRadSetup },
		{ "Return to main menu\r\n\n", 'X', exitMenu }
};

// these need to correspond to the items above

// station menu
#define N_STATIONMENU	8
struct menuItems_t stationMenu[N_STATIONMENU] = {
		{ "Callsign\r\n", 'A', setParam },
		{ "Latitude\r\n", 'B', setParam },
		{ "Longitude\r\n", 'C', setParam },
		{ "Grid Square\r\n", 'D', setParam },
		{ "Repeat Default\r\n", 'E', setParam },
		{ "Beacon Interval\r\n\n", 'F', setParam },
		{ "List Settings\r\n", 'L', printStnSetup },
		{ "Return to main menu\r\n\n", 'X', exitMenu }
};

// menu contents
struct menuContents_t {
	char				*title;				// title of the menu
	int 				nMenuLines;			// number of lines
	struct menuItems_t	*menus;				// menu items
} menuContents[N_MENUS] = {
#if _BOARD_TYPE==NUCLEO_BOARD
		{ "      IP400 Nucleo Main menu\r\n", N_MAINMENU, mainMenu },
#else
		{ "      IP400 Pi menu\r\n", N_MAINMENU, mainMenu },
#endif
		{ "      Radio setup menu\r\n", N_RADIOMENU, radioMenu },
		{ "      Station Setup  menu\r\n", N_STATIONMENU, stationMenu }
};

// menu (main) task
void Menu_Task_Init(void)
{
	// start off with the menu showing
	activeMenu = MAIN_MENU;
	menuState = MENU_SHOWING;
	entryState = NO_ENTRY;
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

	switch(menuState)	{

	// if there is a return in the console buffer,
	// bring  up the menu
	case MENU_OFF:
		if((nBytesinBuff=databuffer_bytesInBuffer()) == 0)
			return;
		for(int i=0;i<nBytesinBuff;i++)
			if((c=databuffer_get(0)) == ASCII_RET)
				menuState = MENU_SHOWING;
		return;

	case MENU_SHOWING:
		printMenu();
		menuState = MENU_SELECTING;
		return;

	case MENU_SELECTING:
		if((sel_item=getMenuItem()) == NO_ITEM)
			return;
		menuState = MENU_SELECTED;
		break;

	case MENU_SELECTED:
		m=menuContents[activeMenu].menus;
		m += sel_item;
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

#if _BOARD_TYPE==NUCLEO_BOARD
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
	char c;

	if((nBytesinBuff=databuffer_bytesInBuffer()) == 0)
		return NO_ITEM;

	for(int i=0;i<nBytesinBuff;i++)		{
		if((c=databuffer_get(0)) != BUFFER_NO_DATA)	{

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
	if(databuffer_bytesInBuffer() == 0)
		return FALSE;

	if(databuffer_get(0) == ASCII_RET)
		return TRUE;

	return FALSE;

}

/*
 * This part pertains to getting an entry and validating it..
 */

// keys in entry mode
#define	KEY_EOL			0x0D		// carriage return
#define	KEY_ESC			0x1B		// escape key
#define KEY_DEL			0x7F		// delete key
#define	KEY_BKSP		0x08		// backspace key

#define	MAX_ENTRY		40			// max entry chars

BOOL delMode = FALSE;
int pos = 0;
char keyBuffer[MAX_ENTRY];

// forward refs
uint8_t validateEntry(int activeMenu, int item, char *keyBuffer);

/*
 * Get a key entry: basically stolen from chat.c
 */
uint8_t getKeyEntry(void)
{

	char c;
	int nBytesinBuff;

	if((nBytesinBuff=databuffer_bytesInBuffer()) == 0)
		return RET_MORE;

	for(int i=0;i<nBytesinBuff;i++)		{

		c=databuffer_get(0);

		if(delMode)	{
			if((c != KEY_DEL) && (c != KEY_BKSP))	{
				USART_Print_string("\\%c", c);
				if(pos < MAX_ENTRY)
					keyBuffer[pos++] = c;
				delMode = FALSE;
			} else {
				if(pos > 0)	{
					USART_Print_string("%c", keyBuffer[--pos]);
				} else {
					USART_Print_string("\\\r\n");
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
				USART_Print_string("\\%c", keyBuffer[--pos]);
				delMode = TRUE;
				break;

			default:
				USART_Send_Char(c);
				if(pos < MAX_ENTRY)
					keyBuffer[pos++] = c;
				break;
			}
		}
	}
	return RET_MORE;
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
		USART_Print_string("%s->", m->menuLine);
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

// data types we are updating
enum {
	uint8_type,		// uint8 field
	int16_type,		// int16 field
	uint32_type,	// uint32 field
	float_type,		// floating point
	char_type,		// character type
	field_type		// field type
};

// struct to hold validation values
typedef struct field_validator_t {
	int			MinVal;				// minimum value
	int			MaxVal;				// maximum value
	void    	*setupVal;			// pointer to setup value
	int			type;				// type of entry
	uint32_t	scalar;				// scalar to convert to decimal
} FIELD_VALIDATOR;

// validators for radio mmenu
FIELD_VALIDATOR radioValidators[] = {
		{ MIN_FREQ, 450000000, &setup_memory.params.radio_setup.lFrequencyBase, uint32_type, 1000000 },
		{ 9600, 600000, &setup_memory.params.radio_setup.lDatarate, uint32_type, 1000 },
		{ 12500, 150000, &setup_memory.params.radio_setup.lFreqDev, uint32_type, 1000 },
		{ 2600, 1600000, &setup_memory.params.radio_setup.lBandwidth, uint32_type, 1000 },
		{ 0, 20, &setup_memory.params.radio_setup.outputPower, uint8_type, 1 },
		{ -115, 0, &setup_memory.params.radio_setup.rxSquelch, int16_type, 1 },
};

FIELD_VALIDATOR stationValidators[] = {
		{ 4, 6, &setup_memory.params.setup_data.stnCall, char_type, 0 },
		{ 2, 14, &setup_memory.params.setup_data.latitude, char_type, 0 },
		{ 2, 14, &setup_memory.params.setup_data.longitude, char_type, 0 },
		{ 6, 6, &setup_memory.params.setup_data.gridSq, char_type, 0 },
		{ 0, 0x8, &setup_memory.params.setup_data.flags, field_type, 0 },
		{ 1, 100, &setup_memory.params.setup_data.beaconInt, int16_type, 0 }
};

uint8_t validateEntry(int activeMenu, int item, char *keyBuffer)
{

	int newValue, min, max;

	//NB: the cases here must jive with the menu items
	switch(activeMenu)	{

	case MAIN_MENU:			// the only menu item here is the clock
		setTOD(keyBuffer);
		break;

	case RADIO_MENU:
		// convert a floating point entry to the required decimal
		if(isfloat(keyBuffer))
				newValue = (int)(ascii2double(keyBuffer)*radioValidators[item].scalar);
		else 	newValue = ascii2Dec(keyBuffer);
		max = radioValidators[item].MaxVal;
		min = radioValidators[item].MinVal;
		if((newValue<min) || (newValue>max))	{
			USART_Print_string("Must be in the range of %d to %d\r\n", min, max);
				return RET_PAUSE;
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

			case int16_type:
				newValue = ascii2Dec(keyBuffer);
				max = stationValidators[item].MaxVal;
				min = stationValidators[item].MinVal;
				if((newValue<min) || (newValue>max))	{
					USART_Print_string("Must be in the range of %d to %d\r\n", min, max);
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
						return RET_PAUSE;
				}
				strcpy((char *)stationValidators[item].setupVal, keyBuffer);
				break;

			case field_type:
				uint8_t val = keyBuffer[0] == 'T' ? 1 : 0;
				uint8_t *f8= (uint8_t *)stationValidators[item].setupVal;
				if(val)
					*f8 |= (uint8_t)stationValidators[item].MinVal;
				else
					*f8 &= (uint8_t)stationValidators[item].MinVal;
				break;
			}
			break;

	}

	// falls to here when done...
	return RET_DONE;
}


/*
 * Print the frame stats
 */
void Print_Frame_stats(FRAME_STATS *stats)
{
	USART_Print_string("Frame Statistics\r\n\n");

	USART_Print_string("Transmitted frames->%d\r\n", stats->TxFrameCnt);
	USART_Print_string("CRC Errors->%d\r\n", stats->CRCErrors);
	USART_Print_string("Rx Timeouts->%d\r\n", stats->TimeOuts);
	USART_Print_string("Frames with good CRC->%d\r\n", stats->RxFrameCnt);
	USART_Print_string("Beacon frames->%d\r\n", stats->nBeacons);
	USART_Print_string("Repeated frames->%d\r\n", stats->nRepeated);

	USART_Print_string("Processed Frames->%d\r\n", stats->RxFrameCnt);
	USART_Print_string("Dropped frames->%d\r\n", stats->dropped);
	USART_Print_string("Duplicate frames->%d\r\n", stats->duplicates);
	USART_Print_string("Repeated frames->%d\r\n", stats->duplicates);


}
/*
 * Print the radio error stats
 */
struct radio_errs_t	{
	uint32_t	mask;
	char		*errmsg;
} radio_errs[N_RADIO_ERRS] = {
		{ SEQ_COMPLETE_ERR, "Sequencer Error" },
		{ SEQ_ACT_TIMEOUT, "Sequencer action timeout" },
		{ PLL_CALAMP_ERR, "VCO Amplitude calibration error" },
		{ PLL_CALFREQ_ERR, "VCO Frequency calibration error" },
		{ PLL_UNLOCK_ERR, "PLL Unlocked" },
		{ PLL_LOCK_FAIL, "PLL Lock failure" },
		{ DBM_FIFO_ERR, "Data buffer FIFO error" }
};
//
void Print_Radio_errors(uint32_t errs)
{
	if(errs == 0){
		USART_Print_string("No radio errors\r\n");
		return;
	}
	for(int i=0;i<N_RADIO_ERRS;i++)
		if(errs & radio_errs[i].mask)
			USART_Print_string("%s\r\n", radio_errs[i].errmsg);
}
/*
 * Print the FSM state
 */
char *fsm_states[FSM_N_FSM_STATES] = {
		"Idle",
		"Enable RF registers",
		"Wait for active 2",
		"Active 2",
		"Enable current",
		"Synth setup",
		"VCO calibration",
		"Lock Rx and Rx",
		"Llock on Rx",
		"Enable PA",
		"Transmit",
		"Analog power down",
		"End transmit",
		"lock on Rx",
		"Enable Rx",
		"Enable LNA",
		"Recieve",
		"End rx",
		"Synth power down"
};
void Print_FSM_state(uint8_t state)
{
	if(state > FSM_N_FSM_STATES)	{
		USART_Print_string("\r\n?Unknown FSM state\r\n");
		return;
	}
	USART_Print_string("\r\nRadio FSM State: %d: %s\r\n", state, fsm_states[state]);
}

#if __MEM_DEBUG
/*
 * dump memory statistics
 * uses the mallinfo structure
 */
void Print_Memory_Stats(void)
{

	struct mallinfo memStats;

	memStats = mallinfo();

	USART_Print_string("Memory Statistics\r\n\n");

	USART_Print_string("Total non-mapped bytes (arena)->%ld\r\n", memStats.arena);
	USART_Print_string("Chunks not in use->%ld\r\n", memStats.ordblks);
	USART_Print_string("Free fast bin blocks->%ld\r\n", memStats.smblks);
	USART_Print_string("Mapped Regions->%ld\r\n", memStats.hblks);
	USART_Print_string("Bytes in mapped regions->%ld\r\n\n", memStats.hblkhd);

	USART_Print_string("Free bytes in fast bins->%ld\r\n", memStats.fsmblks);
	USART_Print_string("Total Allocated Space->%ld\r\n", memStats.uordblks);
	USART_Print_string("Total Space not in use->%ld\r\n", memStats.fordblks);
	USART_Print_string("Topmost releasable block->%ld\r\n", memStats.keepcost);

}
#endif

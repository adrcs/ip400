/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	File Name:	      tasks.h

	Author:		      MartinA

	Creation Date:	  Jan 12, 2025

	Description:	  Declarations and entry points for system tasks

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
				is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/

#ifndef INC_TASKS_H_
#define INC_TASKS_H_

#include "types.h"

// real time task defines
#define	MAIN_TASK_SCHED			100				// 100 ms main task scheduling
#define	XCVR_TASK_SCHED			10				// SubG task
#define	SPI_TASK_SCHED			5				// SPI task

// usart task
void USART_API_init(void);

// menu (main) task
void Menu_Task_Init(void);
void Menu_Task_Exec(void);

// Frame Task
void Frame_task_init(void);
void Xcvr_Task_init(void);
void Xcvr_Task_Exec(void);

// Mesh Task
void Mesh_Task_Init(void);
void Mesh_ProcessBeacon(void *frameData, uint32_t rssi);
BOOL Mesh_Accept_Frame(void *rxFrame, uint32_t rssi);
void Mesh_ListStatus(void);
void UpdateMeshStatus(void);

// chat task
void Chat_Task_init(void);
void Chat_Task_welcome(void);
uint8_t Chat_Task_exec(void);

// beacon task
void Beacon_Task_init(void);
void Beacon_Task_exec(void);

// GPS Related
void GPS_Task_exec(void);
void GPSEcho(void);

// LED task
void Led_Task_Init(void);
void Led_Task_Exec(void);
BOOL LedTest(void);

// SPI task
void SPI_Task_init(void);
void SPI_Task_Exec(void);

#endif /* INC_TASKS_H_ */

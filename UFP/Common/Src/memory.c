/*---------- -----------------------------------------------------------------
	Project:	    IP400 mini node

	File Name:	    memory.c

	Author:		    MartinA

	Description:	This module handles memory allocation and deallocation

	Revision History:

---------------------------------------------------------------------------*/
//----Doxygen Header---------------------------------------------------------
//!
//!	\file  	memory.c
//!
//! \brief 	Memory allocation/deallocation with statistics
//!
//!	\author Martin C. Alcock, VE6VH
//!
//!	\par 	Revision History:
//!			<2026-05-27> updated to inclue doxy comments
//!
//! \copyright
//!
//! 	Copyright (c) Alberta Digital Radio Communications Society
//! 	All rights reserved.
//!
//! 	This program is free software: you can redistribute it and/or modify
//! 	it under the terms of the GNU General Public License as published by
//! 	the Free Software Foundation, either version 2 of the License, or
//! 	(at your option) any later version, provided this copyright notice
//! 	is included.
//!
//!
//---------------------------------------------------------------------------

#include "usart.h"
#include "memory.h"

// memory stats

ALLOCS MemAllocs[N_MEM_ALLOCS] = {
		{"Chat",    0, 0 },
		{"Data Q",  0, 0 },
		{"Frame",   0, 0 },
		{"Kiss",    0, 0 },
		{"SPI",     0, 0 },
		{"BUFFERS", 0, 0 },
		{"OFDM",  0, 0 }
};
/**
 * @fn void* nodeMemAlloc(MEM_ALLOCS, size_t)
 * @brief Allocate dynamic memory under a specific heading
 *
 * @param module memory allocation heading
 * @param size size of allocation in bytes
 * @return pointer to allocated memory or NULL
 */
void *nodeMemAlloc(MEM_ALLOCS module, size_t size)
{
	MemAllocs[module].nAllocs++;
	MemAllocs[module].size = size;
	return pvPortMalloc(size);
}
/**
 * @fn void nodeMemFree(MEM_ALLOCS, void*)
 * @brief Free dynamic memory
 *
 * @param module memeory allocation heading
 * @param mem pointer to allocated memory
 */
void nodeMemFree(MEM_ALLOCS module, void *mem)
{
	MemAllocs[module].nAllocs--;
	vPortFree(mem);
	return;
}

void PrintAllocStats(void)
{
	int nLeaks = 0;
	size_t allocSize = 0;

	USART_Print_string("\r\nModule allocations\r\n");

	for(int i=0;i<N_MEM_ALLOCS;i++)	{
		USART_Print_string("%s->%d (%d)\r\n", MemAllocs[i].name, MemAllocs[i].nAllocs, MemAllocs[i].size);
		nLeaks += MemAllocs[i].nAllocs;
		allocSize += MemAllocs[i].nAllocs * MemAllocs[i].size;
	}

	USART_Print_string("Total Memory Used: %d\r\n", allocSize);

}

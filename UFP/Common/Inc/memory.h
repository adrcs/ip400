/*---------------------------------------------------------------------------
	Project:	      IP400 Mini Node

	Module:		      memory management

	File Name:	      memory.h

	Date Created:	  Jan 4, 2026

	Author:			  MartinA

	Description:      Definitions for the memory manager

					  Copyright © 2024-25, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/


#ifndef IP400_INC_MEMORY_H_
#define IP400_INC_MEMORY_H_

/*	modules that allocate memory definitions	*/
/**
 * @enum mem_alloc_e
 * @brief Module names for memory allocations
 *
 */
typedef enum mem_alloc_e	{
	CHAT=0,					// chat module
	DATAQ,					// data queueing/dequeing
	FRAME,					// frame manager
	KISS,					// KISS manager
	SPI,					// SPI handler
	BUFFERS,				// Buffer manager
	OFDM,					// OFDM subsystem
	N_MEM_ALLOCS			// number of stats
} MEM_ALLOCS;

typedef struct mem_alloc_t	{
	char *  name;
	int 	nAllocs;
	size_t 	size;
} ALLOCS;

extern ALLOCS MemAllocs[N_MEM_ALLOCS];

// mem allocators/deallocators
void *nodeMemAlloc(MEM_ALLOCS module, size_t size);
void nodeMemFree(MEM_ALLOCS module, void *mem);

void PrintAllocStats(void);


#endif /* IP400_INC_MEMORY_H_ */

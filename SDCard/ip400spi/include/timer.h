/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        timer.h

        Author:           root

        Creation Date:    Mar. 6, 2025

        Description:      Definitions for the timer and associated tasks

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

---------------------------------------------------------------------------*/

#ifndef INCLUDE_TIMER_H_
#define INCLUDE_TIMER_H_

#include "types.h"

// definitions
#define	TIMER_VALUE	100			// 20 ms between polls

#define	MSTOUS(x)	((int)x*(int)1000)
#define	MSTONS(x)	((long long)x*(long long)1000000)

// functions
BOOL startTimer(int interval, void (*exec_function)(void));
void stopTimer(void);


#endif /* INCLUDE_TIMER_H_ */

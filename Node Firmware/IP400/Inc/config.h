/*---------------------------------------------------------------------------
	Project:	      WL33_E04_RPI

	File Name:	      config.h

	Author:		      MartinA

	Creation Date:	  Jan 27, 2025

	Description:	  Configuration parameters

					This program is free software: you can redistribute it and/or modify
					it under the terms of the GNU General Public License as published by
					the Free Software Foundation, either version 2 of the License, or
					(at your option) any later version, provided this copyright notice
					is included.

				  Copyright (c) 2024-25 Alberta Digital Radio Communications Society

	Revision History:

---------------------------------------------------------------------------*/
#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_

// the board type is selected here...
#define	PI_BOARD		0				// pi board
#define	NUCLEO_BOARD	1				// Nucleo board
#define	_BOARD_TYPE		NUCLEO_BOARD	// board type in use

// gps receiver enabled
#if _BOARD_TYPE == PI_BOARD
#define	__ENABLE_GPS	0				// PI board does not have GPS
#endif

#if _BOARD_TYPE == NUCLEO_BOARD
#define	__ENABLE_GPS	0				// set to 1 if you have GPS attached to Nucleo
#endif

#endif /* INC_CONFIG_H_ */


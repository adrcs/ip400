/*---------------------------------------------------------------------------
	Project:	      WL33_NUCLEO_UART

	Module:		      Type definitions

	File Name:	      types.h

	Date Created:	  Jan 9, 2025

	Author:			  MartinA

	Description:      Useful data types

					  Copyright © 2024-25, Alberta Digital Radio Communications Society,
					  All rights reserved


	Revision History:

---------------------------------------------------------------------------*/

#ifndef INC_TYPES_H_
#define INC_TYPES_H_

#include <stdint.h>
#include <stddef.h>

// boolean type
#ifndef BOOL
typedef	uint8_t	BOOL;			// boolean
typedef uint8_t BOOLEAN;		// alternate
#endif

#ifndef TRUE
#define TRUE  ((BOOL)1U)
#endif

#ifndef FALSE
#define FALSE ((BOOL)0U)
#endif

#ifndef __CCALL
#ifdef __cplusplus
#define __CCALL extern "C"
#else
#define __CCALL
#endif
#endif

// macros
#define		islower(c)		((c >= 'a') && (c <= 'z'))
#define		toupper(c)		(c-0x20)

#endif /* INC_TYPES_H_ */

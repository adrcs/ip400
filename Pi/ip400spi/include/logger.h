/*---------------------------------------------------------------------------
        Project:          Ip400Spi

        File Name:        logger.h

        Author:           root

        Creation Date:    Mar. 6, 2025

        Description:      Definitions for the logger

                          This program is free software: you can redistribute it and/or modify
                          it under the terms of the GNU General Public License as published by
                          the Free Software Foundation, either version 2 of the License, or
                          (at your option) any later version, provided this copyright notice
                          is included.

                          Copyright (c) 2024-25 Alberta Digital Radio Communications Society

------------------------------------------------------------------char *geterrno(int errnum)---------*/

#ifndef INCLUDE_LOGGER_H_
#define INCLUDE_LOGGER_H_

// severity
#define	LOG_DEBUG		0			// debug mode
#define	LOG_NOTICE		1			// information
#define	LOG_ERROR		2			// error message
#define	LOG_FATAL		3			// fatal error (causes exit)

// debug modes
#define	DEBUG_LOG		0x01	// debug log
#define	DEBUG_UDP		0x02	// debug UDP
#define	DEBUG_SPI		0x10	// debug SPI

void openLog(uint8_t debug);
void logger(int severity, char *format, ...);
char *geterrno(int errnum);

#endif /* INCLUDE_LOGGER_H_ */

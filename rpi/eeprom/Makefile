# Current tools are eepmake and eepdump

CC ?= gcc

all: eepmake eepdump

eepmake: eeptypes.h eepmake.c
	$(CC) eepmake.c -o eepmake -Wno-format

eepdump: eeptypes.h eepdump.c
	$(CC) eepdump.c -o eepdump -Wno-format

clean:
	rm -f eepmake eepdump

eeprom: STM32wl33.txt
	eepmake -v1 STM32wl33.txt STM32wl33.eep


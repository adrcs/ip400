# Current tools are eepmake and eepdump

CC ?= gcc

all: eepmake eepdump

eepmake: eeptypes.h eepmake.c
	$(CC) eepmake.c -o eepmake -Wno-format

eepdump: eeptypes.h eepdump.c
	$(CC) eepdump.c -o eepdump -Wno-format

clean:
	rm -f eepmake eepdump

eeprom: 
	./eepmake STMwl33.txt STMwl33.eep

flash:	eeprom
	./eepflash.sh -w -d=0 -a=50 -f=STMwl33.eep -t=24c32

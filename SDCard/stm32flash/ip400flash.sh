#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage ip400flash <filename>"
	exit 100
fi

echo "Flashing $1 to IP400 HAT"
set -x

#Notes: GPIO 20 is nreset
#	GPIO 21 is boot ena
nreset=20
bootena=21
set -x

# set pins to outputs
raspi-gpio set $nreset op pu
raspi-gpio set $bootena op pd

# reset pi
raspi-gpio set $bootena dh
raspi-gpio set $nreset dl
raspi-gpio set $nreset dh

# program flash
./stm32flash -v -b 115200 -w "$1" -g 0x0 -R /dev/ttyAMA0

# reset again
raspi-gpio set $bootena dl
raspi-gpio set $nreset dl
raspi-gpio set $nreset dh

# set pins to inputs
raspi-gpio set $nreset ip
raspi-gpio set $bootena ip

#
set +x

#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage ip400flash <filename>"
	exit 100
fi

echo "Flashing $1 to IP400 HAT"
#Notes: GPIO 20 is nreset
#	GPIO 21 is boot ena
nreset=532
bootena=533
set -x

# program flash
stm32flash -v -W -b 115200 -w "$1" -g 0x0 -R -i $bootena,$nreset,-$nreset,$nreset:-$bootena /dev/serial0
#
set +x

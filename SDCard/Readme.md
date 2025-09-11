# Mini Node SD Card directories
If you are unable to use the SD card image on the website, then download and flash one of your own using the Raspberru Pi disk imager. When completed, you will need
to add four directories:
1) Code. Contains a shell script to update the firmware, as well as the latest (1.4) code. Login as SU before attempting.
2) EEprom. Updates the HAT eeprom. Already done before shipping, but can be updated with 'make flash'. Login as SU first.
3) ip400spi. Contains code to connect to the data port on the node using UDP. Documentation is in the SPI specification.
4) stm32flash. The flash utility. Compile and install it with 'make install'.


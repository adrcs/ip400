# Using Wireshark with IP400 frames

This folder contains files for wireshark and Notepad++ to facilitate 
ethernet development and debugging.

## Wireshark Generic Dissector (WSGD) scripts
The two files, IP400.wsgd and IP400.fdesc, are scripts for the wireshark generic dissector that
recognizes IP400 packet formats. It will be added to as time goes on.

To install them, you will first need to install the generic dissector. See the instructions from
the author at http://wsgd.free.fr/installation.html. Place these two files in the same directory
as the dissector to start viewing packets.

If they are being interpreted as something different, then disable the protocol that is being 
used instead. You can always enable it again later.

## Notepad custom language interpreter
The xml file is a custom language interpreter for notepad++, which does syntax highlighting
for dissector scripts. Open the user defined language folder under the 'Language' menu, and place
this file in there.

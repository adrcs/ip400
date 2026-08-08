# IP400 Unified Firmware Platform

This repo contains the UFP code, which consists of two components:-
* The base (Common) code that implements the mesh node, diagnostics and I/O interfaces
* The modem code, which is specific to a particular modem type.

The current release, V194, is V2.0 of the code, which supports the WL33 mini-node and Nucleo platforms. A new release is planned
that will include support for the AMNC, for more information on the timeline see the presentation on the project site, ip400 (dot) adrcs (dot) org.

There is a pre-built image for the mini-node, that can be downloaded immediately, however the board MUST be at the latest revision, details of the 
hardware modifications are contained in the documentation, which is in the folder by the same name.

To build the code, you must have CubeIDE V2.1 or later. Unzip the project files, open CubeIDE with a blank workspace, then import the pre-built
projects into it. Open the .ioc file with CubeMX, and generate the code. Then return to CubeIDE and build it. Ensure that the Common and WL33
directories are included in the source, and the 'inc' directories are in the include path.

The Nucleo will generate an object file that can be downloaded, and the Pi Zero will generate a .bin file which can be flashed using STM32Flash.

Have fun.

73, de VE6VH.


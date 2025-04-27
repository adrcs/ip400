# Node Firmware
This directory contains the code sources. The current source code is at Rev level 1.1.

Setting up a CubeIDE development project has several steps:
1.  Download the code from this site.
2.  Unzip it after the download
3.  Navigate to the 'platforms' directory, and unzip the project for the appropriate platform.
4.  Import the project into CubEIDE and Add the source code to it (see .pfd doc for instructions).
5.  Build the code and download.

The reasoning behind it is because goign forward several platforms are being supported, the code iteself
is platform independent, so it can be shared between them. The platform code has the device specifics
and should not need to be updated each time a release is made, however it is probably good practice to
update it each time.

There are pre-built binaries in the Bin directory for platforms that support a download.

Currently supported platforms:
Nucleo CC Experimenter node

Release notes are in the node software document in the documentation directory.

Requirements
============
* Visual Studio 2015 or 2013 (lower may not work). You can get VS Comunity here: https://www.visualstudio.com/
* A precompiled libary of gd .
  This have to be compiled with the same version of zlib used in minetestmapper.

Setup IDE
=========
1. Open the minetestmapper.sln or MSVC\mintestmapper.vcxproj with Visual Studio.
2. Link to gd libary:
	Open projectsettings `ALT+F7`.
	Select `All Configurations` and `All Platforms`.
	Click C/C++ -> `additional include directories` and enter the path to the include directory of libGD.
	Click Apply
	
	Select a configuration (Debug|Release) and a platform (x86|x64)
	Click Linker --> `additional libary directories` Enter the path to libgd that fits to your configuration and platform.
	Do this step for all configurations and platform you want to use.
	
	Attention! You will get a linker error if you select one that does not fit to your configuration and platform. 


Debug Minetestmapper
====================
In projectsettings (`ALT+F7`) click `Debugging`.
Specify the Arguments in `Command arguments`.
Evrerytime you launch the debugger now minetstmapper will be executed with this arguments.

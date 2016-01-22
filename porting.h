#ifndef _PORTING_H
#define _PORTING_H


#ifdef _WIN32

#include <windows.h>
#include "MSVC\versioninfo.h"
#define sleepMs(x) Sleep(x)

#define VERSION_MAJOR GEN_VER_VERSION_STRING
#define VERSION_MINOR ""

#define INSTALL_PREFIX ""
#else

#include <unistd.h>
#define sleepMs(x) usleep(x*1000)

#endif

#ifdef _MSC_VER

#define strcasecmp(a, b) _stricmp(a, b)

#endif

#endif // _PORTING_H

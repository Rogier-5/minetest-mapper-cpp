#ifndef _PORTING_H
#define _PORTING_H

#ifdef _WIN32

#include <windows.h>
#define sleepMs(x) Sleep(x)

#else

#include <unistd.h>
#define sleepMs(x) usleep(x*1000)

#endif

#ifdef _MSC_VER

#define strcasecmp(a, b) _stricmp(a, b)

#endif

#endif // _PORTING_H

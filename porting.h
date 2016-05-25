#pragma once

#ifdef _WIN32
#include <io.h>//windows version of unistd.h
#include <windows.h>

#define sleep_ms(x) Sleep(x)
#else
#include <unistd.h>

#define sleep_ms(x) usleep(x*1000)
#endif

#ifdef _MSC_VER

#define strcasecmp(a, b) _stricmp(a, b)

#endif
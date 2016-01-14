
#include <iostream>
#include "cmake_config.h"

#include "CharEncodingConverter.h"
#ifdef _WIN32
#include "CharEncodingConverterWin32.h"
#else
#include "CharEncodingConverterIConv.h"
#endif

CharEncodingConverter *CharEncodingConverter::createStandardConverter(const std::string to, const std::string from)
{
#ifdef _WIN32
	return new CharEncodingConverterWin32(to, from);
#else
	#if USE_ICONV
	return new CharEncodingConverterIConv(to, from);
	#else
	return new CharEncodingConverterDummy(to, from);
	#endif
#endif
}

std::string CharEncodingConverter::getCurrentCharEncoding(void)
{
#ifdef _WIN32
	return CharEncodingConverterWin32::getCurrentCharEncoding();
#else
	#if USE_ICONV
	return CharEncodingConverterIConv::getCurrentCharEncoding();
	#else
	return CharEncodingConverterDummy::getCurrentCharEncoding();
	#endif
#endif
}



#include <iostream>
#include "build_config.h"

#include "CharEncodingConverter.h"
#ifdef _WIN32
#pragma message ("No standard charset converter defined for WIN32 - disabling conversion")
#else
#include "CharEncodingConverterIConv.h"
#endif

CharEncodingConverter *CharEncodingConverter::createStandardConverter(const std::string to, const std::string from)
{
#ifdef _WIN32
	return new CharEncodingConverterDummy(to, from);
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
	return CharEncodingConverterDummy::getCurrentCharEncoding();
#else
	#if USE_ICONV
	return CharEncodingConverterIConv::getCurrentCharEncoding();
	#else
	return CharEncodingConverterDummy::getCurrentCharEncoding();
	#endif
#endif
}


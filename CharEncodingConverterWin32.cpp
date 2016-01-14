
#include "CharEncodingConverterWin32.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <vector>
#include "util.h"

#ifdef _WIN32
#include <Windows.h>
#include <Stringapiset.h>
#else
// Test-compile this on linux. This shouldn't happen under regular circumstances.
#warning Compiling CharEncodingConverterWin32.cpp on non-Windows platform. This shouldn't happen
#include <LWindows.h>
#endif


std::string CharEncodingConverterWin32::getCurrentCharEncoding(void)
{
	// TBD: is this valid when minetestmapper is invoked from the gui ??
	uint32_t codePageIn = GetConsoleCP();
	uint32_t codePageOut = GetConsoleOutputCP();
	if (codePageIn != codePageOut) {
		std::ostringstream oss;
		oss << "Failure determining console code page: input page (" << codePageIn
			<< ") is different from output page (" << codePageOut << ")";
		throw std::runtime_error(oss.str());
	}
	#if 0
	// Try to get a meaningful string.
	// Disabled, because there seems to be no way to convert a name back to a number...
	struct _cpinfoex codePageInfo;
	int rv = GetCPInfoEx(codePage, 0, &codePageInfo);
	if (rv == 0) {
		std::ostringstream oss;
		oss << "CP-" << codePage;
		return oss.str();
	}
	else {
		return codePageInfo.CodePageName;
	}
	#else
	std::ostringstream oss;
	oss << "CP-" << codePageIn;
	return oss.str();
	#endif
}

uint32_t CharEncodingConverterWin32::getCodePageFromName(const std::string codePageName)
{
	int prefix = -1;
	if (strlower(codePageName.substr(0,3)) == "cp-")
		prefix = 3;
	else if (strlower(codePageName.substr(0,3)) == "cp")
		prefix = 2;
	else if (isdigit(codePageName[0]))
		prefix = 0;
	if (prefix >= 0) {
		uint32_t codePage;
		std::istringstream iss(codePageName.substr(prefix));
		iss >> codePage;
		if (iss.fail())
			throw std::runtime_error(std::string("Error finding code page number for ID '") + codePageName + "'");
		return codePage;
	}
	else if (strlower(codePageName) == "utf8" || strlower(codePageName) == "utf-8") {
		// Make sure UTF-8 can be converted to a code page number. See below for the general case.
		return 65001;
	}
	else if (strlower(codePageName) == "iso8859-2" || strlower(codePageName) == "iso-8859-2") {
		// Make sure ISO8859-2 can be converted to a code page number. See below for the general case.
		return 28592;
	}
	else {
		// No idea how to achieve this in Windows. Probably it can't even be done...
		// MS documentation is particularly opaque on this subject.
		#warning: Generic conversion of code page name (e.g. 'ISO8859-2', or 'UTF-8', ...) to number not implemented
		// int rv = WinConvertCodepageNameToNumber(codePageName);
		// if (rv <= 0)
			throw std::runtime_error(std::string("Error finding code page number for ID '") + codePageName + "'");
		throw std::runtime_error(std::string("Conversion of code page ID '") + codePageName + "' to number not implemented");
		//return rv;
	}
}

CharEncodingConverterWin32::CharEncodingConverterWin32(std::string to, std::string from)
	: CharEncodingConverter(to, from)
{
	m_fromCodePage = getCodePageFromName(m_fromFormat);
	m_toCodePage = getCodePageFromName(m_toFormat);
}

std::string CharEncodingConverterWin32::convert(const std::string &src)
{
	std::string dst;
	std::wstring utf16;
	int rv = convertToUTF16(src, utf16);
	if (rv > 0)
		rv = convertFromUTF16(utf16, dst);
	if (rv <= 0) {
		int rno = errno;
		std::ostringstream oss;
		oss << "Failure converting string from "
			<< fromFormat() << " to " << toFormat()
			<< " (text: '" << std::string(src.c_str()) << "'): "
			<< strerror(rno);
		throw std::runtime_error(oss.str());
	}
	return dst;
}

int CharEncodingConverterWin32::convertToUTF16(const std::string &src, std::wstring &dst)
{
	int len = MultiByteToWideChar(m_fromCodePage, 0, src.c_str(), -1, NULL, 0);
	if (len == 0)
		return 0;
	std::vector<wchar_t> dstdata;
	dst.reserve(len);
	len = MultiByteToWideChar(m_fromCodePage, 0, src.c_str(), -1, dstdata.data(), dstdata.capacity());
	if (len > 0)
		dst = std::wstring(dstdata.data(), len);
	return len;
}

int CharEncodingConverterWin32::convertFromUTF16(const std::wstring &src, std::string &dst)
{
	int len = WideCharToMultiByte(m_toCodePage, 0, src.c_str(), -1, NULL, 0, NULL, NULL);
	if (len == 0)
		return 0;
	std::vector<char> dstdata;
	dstdata.reserve(len);
	len = WideCharToMultiByte(m_toCodePage, 0, src.data(), -1, dstdata.data(), dstdata.capacity(), NULL, NULL);
	if (len > 0)
		dst = std::string(dstdata.data(), len);
	return len;
}


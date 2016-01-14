
#ifndef _CHARENCODINGCONVERTERWIN32_H_INCLUDED_
#define _CHARENCODINGCONVERTERWIN32_H_INCLUDED_

#include "CharEncodingConverter.h"

class CharEncodingConverterWin32 : public CharEncodingConverter
{
public:
	CharEncodingConverterWin32(std::string to, std::string from = "");
	virtual ~CharEncodingConverterWin32(void) {}

	static std::string getCurrentCharEncoding(void);
	std::string convert(const std::string &src) override;
private:
	// Assume wchar_t is 16 bits on Windows...
	int convertToUTF16(const std::string &src, std::wstring &dst);
	int convertFromUTF16(const std::wstring &src, std::string &dst);
	static uint32_t getCodePageFromName(const std::string codePageName);
	uint32_t m_fromCodePage;
	uint32_t m_toCodePage;
};

#endif // _CHARENCODINGCONVERTERWIN32_H_INCLUDED_


//
// Include file for compiling Windows character encoding conversion
// on linux.
//

#ifdef _WIN32
#error Compiling LWindows.h, but _WIN32 is already defined. This shouldn't happen!
#endif
#define _WIN32 1

#define GetConsoleOutputCP GetConsoleCP
static uint32_t GetConsoleCP(void)
{
	return 65001;	// Anything will do. 65001 is UTF-8
}

static int MultiByteToWideChar(uint32_t CodePage, int16_t dwFlags,
				const char *lpMultiByteStr, int cbMultiByte,
				wchar_t *lpWideCharStr, int cchWideChar)
{
	(void) CodePage;
	(void) dwFlags;
	(void) lpMultiByteStr;
	(void) cbMultiByte;
	(void) lpWideCharStr;
	(void) cchWideChar;
	return 0;	// Failure
}

static int WideCharToMultiByte(uint32_t CodePage, int16_t dwFlags,
				const wchar_t *lpWideCharStr, int cchWideChar,
				char *lpMultiByteStr, int cbMultiByte,
				char *lpDefaultChar, bool *lpUsedDefaultChar)
{
	(void) CodePage;
	(void) dwFlags;
	(void) lpWideCharStr;
	(void) cchWideChar;
	(void) lpMultiByteStr;
	(void) cbMultiByte;
	(void) lpDefaultChar;
	(void) lpUsedDefaultChar;
	return 0;	// Failure
}

struct _cpinfoex {
	uint32_t  MaxCharSize;
	int16_t  DefaultChar[1];
	char  LeadByte[1];
	wchar_t UnicodeDefaultChar;
	uint32_t  CodePage;
	char CodePageName[256];
};


int GetCPInfoEx(uint32_t CodePage, int16_t dwFlags, struct _cpinfoex *lpCPInfoEx)
{
	(void) dwFlags;
	memset(lpCPInfoEx, 0, sizeof(struct _cpinfoex));
	lpCPInfoEx->CodePage = CodePage;
	strcpy(lpCPInfoEx->CodePageName, "UTF-8");
	return 1;
}


#include "D3DApp/d3dapp.h"
#include "Logger.h"

#include <cstdarg>
#include <cstdio>

// FUNCTION: TOY2 0x0040C130 [PROVISIONAL]
void D3DAppISetErrorString(char* format, ...)
{
	char buffer[256];
	va_list arguments;

	va_start(arguments, format);
	buffer[0] = '\0';
	vsprintf(buffer, format, arguments);
	lstrcatA(buffer, "\r\n");
	lstrcpyA(LastErrorString, buffer);
	Logger::Log(buffer);
}

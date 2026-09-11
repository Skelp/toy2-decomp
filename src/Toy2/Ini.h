#pragma once

#include "Common.h"

#include <stddef.h>

namespace Toy2
{
	namespace Ini
	{
		struct ControlTextEntry
		{
			int32_t x;
			int32_t y;
			int32_t reserved0;
			const char* text;
			union
			{
				int32_t packedInput;
				struct
				{
					int8_t glyphIndex;
					uint8_t inputCode;
					uint16_t reservedInput;
				};
			};
		};

		struct MessageTextEntry
		{
			int32_t x;
			int32_t y;
			const char* text;
		};

		extern MessageTextEntry* g_messageTextTable[25];
		extern ControlTextEntry* g_keyTextTable[15];
		extern ControlTextEntry* g_joyTextTable[11];
		extern char g_iniInstallSearchPath[512];
		extern int32_t g_cheatsEnabled;
		extern int32_t g_highQualityMpeg;

		STATIC_ASSERT(offsetof(ControlTextEntry, glyphIndex) == 0x10);
		STATIC_ASSERT(offsetof(ControlTextEntry, inputCode) == 0x11);
		STATIC_ASSERT(sizeof(ControlTextEntry) == 0x14);
		STATIC_ASSERT(sizeof(MessageTextEntry) == 0xC);
	}
}

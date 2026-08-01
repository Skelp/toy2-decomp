#pragma once

#include "Common.h"

namespace Toy2
{
	namespace Ini
	{
		struct MessageTextEntry
		{
			int32_t x;
			int32_t y;
			const char* text;
		};

		extern MessageTextEntry* g_messageTextTable[25];

		STATIC_ASSERT(sizeof(MessageTextEntry) == 0xC);
	}
}

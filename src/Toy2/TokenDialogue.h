#pragma once

#include "Common.h"

namespace Toy2
{
	namespace Collectables
	{
		struct TokenDialogueRecord
		{
			int32_t tokenId;
			int32_t dialogueRecordIndex;
			const char* subtitle;
			int32_t facingAngle;
		};

		template <int32_t RecordCount>
		struct TokenDialogueTable
		{
			TokenDialogueRecord records[RecordCount];
			int32_t terminator;
		};

		STATIC_ASSERT(sizeof(TokenDialogueRecord) == 0x10);
	}
}

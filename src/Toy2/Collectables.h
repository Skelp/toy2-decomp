#pragma once

#include "Common.h"

namespace Toy2
{
	namespace Collectables
	{
		struct TokenState
		{
			int32_t linkId;
			int32_t active;
			int32_t* verticalPosition;
			int32_t timer;
		};

		struct TokenDialogueEntry
		{
			int32_t tokenId;
			int32_t dialogueRecordIndex;
			char* subtitle;
			int32_t facingAngle;
		};

		union TokenDialogueValue
		{
			int32_t number;
			char* text;
		};

		extern TokenState g_tokenStates[5];
		extern TokenDialogueEntry g_tokenDialogueEntries[10];

		void BuildPickupTable();
		void LoadTokenTable(const TokenDialogueValue* values);
		void Activate(int32_t tokenIndex, int32_t skipCutscene);
		void Deactivate(int32_t tokenIndex);
		int32_t ShowTokenSparkle(int32_t linkId);

		STATIC_ASSERT(sizeof(TokenState) == 0x10);
		STATIC_ASSERT(sizeof(TokenDialogueEntry) == 0x10);
		STATIC_ASSERT(sizeof(TokenDialogueValue) == 0x4);
	}
}

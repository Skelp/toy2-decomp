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

		extern TokenState g_tokenStates[5];

		void BuildPickupTable();
		void Activate(int32_t tokenIndex, int32_t skipCutscene);
		int32_t ShowTokenSparkle(int32_t linkId);

		STATIC_ASSERT(sizeof(TokenState) == 0x10);
	}
}

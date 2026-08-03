#pragma once

#include "Common.h"

namespace Toy2
{
	namespace Lighting
	{
		struct BuzzLightPreset
		{
			Vector3I positionOffset;
			int32_t reserved;
			int32_t colour;
		};

		extern BuzzLightPreset g_buzzLightPresets[15];

		STATIC_ASSERT(sizeof(BuzzLightPreset) == 0x14);
	}
}

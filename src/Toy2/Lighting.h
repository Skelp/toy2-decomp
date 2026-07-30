#pragma once

#include "Common.h"

#include <stddef.h>

namespace Toy2
{
	namespace Lighting
	{
		struct DynamicLight
		{
			Vector3I position;
			int32_t lifetime;
			int32_t sourceId;
			RGBColor3B colour;
		};

		struct LightingState
		{
			DynamicLight dynamicLights[6];
			Vector3I blendedPosition;
			int32_t reservedBlendState[2];
			RGBColor3B blendedColour;
			uint8_t reserved[0x18];
		};

		extern LightingState g_lightingState;

		void InitBuzzLight();
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId);
		void UpdateBuzzLight();

		STATIC_ASSERT(sizeof(DynamicLight) == 0x18);
		STATIC_ASSERT(offsetof(DynamicLight, lifetime) == 0xC);
		STATIC_ASSERT(offsetof(DynamicLight, sourceId) == 0x10);
		STATIC_ASSERT(offsetof(DynamicLight, colour) == 0x14);
		STATIC_ASSERT(sizeof(LightingState) == 0xC0);
		STATIC_ASSERT(offsetof(LightingState, blendedPosition) == 0x90);
		STATIC_ASSERT(offsetof(LightingState, blendedColour) == 0xA4);
	}
}

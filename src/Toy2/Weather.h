#pragma once

#include "Common.h"
#include "Numerics.h"
#include <stddef.h>

namespace Toy2
{
	namespace Sector
	{
		extern Vector3I16 g_viewRotation;
		extern int32_t g_activeSectorIndex;
	}

	namespace Weather
	{
		struct PrecipitationParticle
		{
			Vector3I position;
			int32_t terminalY;
		};

		STATIC_ASSERT(sizeof(PrecipitationParticle) == 0x10);
		STATIC_ASSERT(offsetof(PrecipitationParticle, terminalY) == 0xC);

		extern int32_t g_spawnAccumulator;
		extern int32_t g_precipitationSpriteSheetIndex;
		extern PrecipitationParticle* g_precipitationParticles;

		void Init();
		void StepPrecipitation(int32_t spawnRate, int32_t fallSpeed, int32_t spriteSheetIndex);
	}
}

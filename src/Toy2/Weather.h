#pragma once

#include "Common.h"
#include "Numerics.h"
#include <stddef.h>

namespace Toy2
{
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
		extern PrecipitationParticle* g_precipitationParticles;

		void Init();
	}
}

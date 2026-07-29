#include "Toy2/Weather.h"

#include "Toy2/Levels.h"

namespace Toy2
{
	namespace Weather
	{
		// GLOBAL: TOY2 0x00557708
		int32_t g_spawnAccumulator;

		// GLOBAL: TOY2 0x0054F08C
		PrecipitationParticle* g_precipitationParticles;

		// FUNCTION: TOY2 0x0044ED60 [MATCHED]
		void Init()
		{
			g_precipitationParticles = (PrecipitationParticle*)Levels::g_levelLoadArena;
			Levels::g_levelLoadArena += sizeof(PrecipitationParticle) * 64;

			for (int32_t i = 0; i < 64; i++)
			{
				g_precipitationParticles[i].terminalY = 0;
			}
		}
	}
}

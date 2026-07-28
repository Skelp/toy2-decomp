#include "Nu3D/Particles.h"
#include <string.h>

namespace Nu3D
{
	namespace Particles
	{
		// FUNCTION: TOY2 0x0040FAC0 [MATCHED]
		void Init()
		{
			memset(g_particleInstances, 0, sizeof(g_particleInstances));
			g_particleAllocationCursor = 0;
		}

		// STUB: TOY2 0x0040FDF0
		ParticleInstance* SpawnFromPreset(int32_t x, int32_t y, int32_t z, int32_t typeId, int32_t presetIndex) { return 0; }

		// GLOBAL: TOY2 0x00529E58;
		ParticleInstance g_particleInstances[64];

		// GLOBAL: TOY2 0x0052AD90
		int32_t g_particleAllocationCursor;
	}
}

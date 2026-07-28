#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Toy2/Toy2.h"
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

		// FUNCTION: TOY2 0x00446FA0 [MATCHED]
		void SetDefaultAlpha(ParticleInstance* particle)
		{
			particle->colourA = 0x2C;
			if ((particle->renderFlags & PARTICLE_RENDER_ALPHA_MODE_MASK) != PARTICLE_RENDER_ALPHA_MODE_MASK)
			{
				particle->colourA = 0x2E;
			}
		}

		// FUNCTION: TOY2 0x0042B250 [MATCHED]
		void UpdateDrainTrail(ParticleInstance* particle)
		{
			const int32_t drainCenterX = -0xBD7C;
			const int32_t drainCenterZ = 0x99;
			const int32_t triggerRadius = 0x8000;
			int32_t deltaX = (drainCenterX - particle->pos.x) >> 5;
			int32_t deltaZ = (drainCenterZ - particle->pos.z) >> 5;

			if (deltaZ * deltaZ + deltaX * deltaX < (triggerRadius >> 5) * (triggerRadius >> 5))
			{
				particle->lifetime = 0;
			}

			if (particle->lifetime > 4 && Toy2::g_fourTickPulse != 0)
			{
				SpawnFromPreset(particle->pos.x - particle->velX * Renderer::g_frameDelta,
					particle->pos.y,
					particle->pos.z - particle->velZ * Renderer::g_frameDelta,
					0x6F,
					2);
			}
		}
	}
}

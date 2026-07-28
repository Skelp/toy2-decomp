#include "Nu3D/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Toy2/Actor.h"
#include "Toy2/Toy2.h"
#include <limits.h>
#include <stdlib.h>
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

		// FUNCTION: TOY2 0x00425AD0 [MATCHED]
		void ReflectWallsSquareArena(ParticleInstance* particle)
		{
			const int32_t westWall = -0x169EB;
			const int32_t eastWall = 0x16915;
			const int32_t northWall = -0x16CEF;
			const int32_t southWall = 0x16991;
			const int32_t wallImpactSound = 0x4A;

			if (particle->pos.x < westWall)
			{
				particle->velX = abs(particle->velX);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
			if (particle->pos.x > eastWall)
			{
				particle->velX = -abs(particle->velX);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
			if (particle->pos.z < northWall)
			{
				particle->velZ = abs(particle->velZ);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
			if (particle->pos.z > southWall)
			{
				particle->velZ = -abs(particle->velZ);
				AudioManager::PlaySoundEffect(wallImpactSound, &particle->pos);
			}
		}

		// FUNCTION: TOY2 0x0042B090
		void UpdateArenaBounce(ParticleInstance* particle)
		{
			const int32_t drainCenterX = -0xBD7C;
			const int32_t drainCenterZ = 0x99;
			int32_t bounced = 0;

			if (particle->lifetime > 4 && Toy2::g_fourTickPulse != 0)
			{
				SpawnFromPreset(particle->pos.x - particle->velX * Renderer::g_frameDelta,
					particle->pos.y,
					particle->pos.z - particle->velZ * Renderer::g_frameDelta,
					0x6E,
					2);
			}

			if (Toy2::Actor::IsInsideBounds(&particle->pos, -0x256D6, 0xE0AA, -0x1B3E9, 0x1B297))
			{
				const int32_t arenaFloorY = -0x12BD3;
				if (particle->groundHeightY == INT_MIN)
				{
					particle->groundHeightY = arenaFloorY;
				}

				if (particle->pos.y > arenaFloorY)
				{
					if (particle->groundHeightY == arenaFloorY)
					{
						particle->pos.y = arenaFloorY;
						bounced = 1;
						particle->velY = -abs((particle->velY * 7) >> 3);
					}
					else
					{
						particle->velX = -particle->velX;
						particle->velZ = -particle->velZ;
						bounced = 1;
					}
				}
				else
				{
					particle->groundHeightY = arenaFloorY;
					if (Renderer::Shadows::g_shadowCount < 47)
					{
						int16_t shadowIndex = Renderer::Shadows::g_shadowCount;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.x = particle->pos.x;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.y = particle->groundHeightY;
						Renderer::Shadows::g_shadowInstances[shadowIndex].pos.z = particle->pos.z;
						Renderer::Shadows::g_shadowInstances[shadowIndex].size = particle->width;
						Renderer::Shadows::g_shadowCount = shadowIndex + 1;
					}
				}
			}
			else
			{
				particle->groundHeightY = 400000;
			}

			int32_t posX = particle->pos.x;
			if (posX < -0x29B56)
			{
				bounced = 1;
				particle->velX = abs(particle->velX);
			}
			if (posX > 0x2A62A)
			{
				bounced = 1;
				particle->velX = -abs(particle->velX);
			}

			int32_t posZ = particle->pos.z;
			if (posZ < -0x230E9)
			{
				bounced = 1;
				particle->velZ = abs(particle->velZ);
			}
			if (posZ > 0x22B97)
			{
				bounced = 1;
				particle->velZ = -abs(particle->velZ);
			}

			int32_t deltaX = (drainCenterX - posX) >> 5;
			int32_t deltaZ = (drainCenterZ - posZ) >> 5;
			if (deltaZ * deltaZ + deltaX * deltaX < 0x100000)
			{
				particle->lifetime = 0;
			}

			if (bounced)
			{
				AudioManager::PlaySoundEffect(0x43, &particle->pos);
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

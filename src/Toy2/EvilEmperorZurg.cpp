#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"

#include <limits.h>
#include <stdlib.h>

namespace Toy2
{
	namespace EvilEmperorZurg
	{
		// GLOBAL: TOY2 0x0052FE00
		int32_t g_previousPhase;

		// GLOBAL: TOY2 0x0052FE04
		int32_t g_damageFlashTimer;

		// GLOBAL: TOY2 0x0052FE08
		int32_t g_voiceTimer;

		// GLOBAL: TOY2 0x0052FE20
		int32_t g_introSoundTimer;

		// GLOBAL: TOY2 0x0052FE24
		int32_t g_encounterState;

		// GLOBAL: TOY2 0x0052FE28
		int32_t g_damageFlashToggle;

		// GLOBAL: TOY2 0x0052FE30
		int32_t g_attackTimer;

		// GLOBAL: TOY2 0x0052FE34
		int32_t g_attackVariant;

		// FUNCTION: TOY2 0x0042B2D0 [MATCHED]
		void BuzzRespawn()
		{
			g_buzzActor.respawnPos.x = -0x1DA7C;
			g_buzzActor.respawnPos.y = -0x12BD0;
			g_buzzActor.respawnPos.z = 0xF699;
			g_buzzActor.respawnYawAngle = 0x400;
		}

		// FUNCTION: TOY2 0x0042B300 [MATCHED]
		void Init()
		{
			Toy2::MoveableObject::InitTable(0);
			Collectables::Init(0, 0);

			Actor::Toy2Actor* zurg = &Actor::g_creatureActors[0];
			g_previousPhase = zurg->actorPhase;
			zurg->actorFlags |= Actor::ACTOR_FLAG_BOSS;
			zurg->pos.y -= 0x28000;
			zurg->motionTargetPos.y = zurg->pos.y;
			g_voiceTimer = 300;
			int32_t startZ = zurg->pos.z + 0x10000;
			g_introSoundTimer = 0x78;
			g_encounterState = 0;
			g_damageFlashToggle = 0;
			g_damageFlashTimer = 0;
			g_attackTimer = 0x104;
			g_attackVariant = 0;
			zurg->yawAngle = 0xC00;
			zurg->pos.z = startZ;
			zurg->creatureRam->rotSpeed = 0;
		}

		// STUB: TOY2 0x0042B3A0
		void Interactions() {}
	}
}

namespace Nu3D
{
	namespace Particles
	{
		// FUNCTION: TOY2 0x0042B090 [PROVISIONAL]
		void UpdateArenaBounce(ParticleInstance* particle)
		{
			const int32_t drainCenterX = -0xBD7C;
			const int32_t drainCenterZ = 0x99;
			int32_t bounced = 0;

			if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0)
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

			if (particle->lifetime > 4 && Toy2::g_framePulseOutputs.fourTick != 0)
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

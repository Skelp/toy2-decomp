#include "Toy2/Buzz.h"
#include "Toy2/BuzzInternal.h"
#include "Toy2/Animation.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/CollisionInternal.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/PoleRecord.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// The particle collisions of Buzz: retail holds 0x004100F0 as one object.
namespace Toy2
{
	namespace Buzz
	{
		// FUNCTION: TOY2 0x004100F0 [PROVISIONAL]
		void CheckParticleCollisions()
		{
			const int32_t buzzX = g_buzzActor.posAngles.pos.x;
			const int32_t buzzY = g_buzzActor.posAngles.pos.y - 0x1CC0;
			const int32_t buzzZ = g_buzzActor.posAngles.pos.z;
			int32_t collisionResponse = 0;
			int32_t replacementCollisionFlags = 0;
			Nu3D::Particles::ParticleInstance* collisionParticle;

			Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::g_particleInstances;
			for (int32_t remainingParticleCount = 64; remainingParticleCount != 0; remainingParticleCount--, particle++)
			{
				if (particle->lifetime <= 0 || (particle->renderFlags & Nu3D::Particles::PARTICLE_INTERACTS_WITH_BUZZ) == 0)
				{
					continue;
				}

				int32_t deltaX = (buzzX - particle->pos.x) * 0x100 >> 13;
				int32_t deltaY = (buzzY - particle->pos.y) * 0x80 >> 13;
				int32_t deltaZ = (buzzZ - particle->pos.z) * 0x100 >> 13;
				if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ >= (particle->width + 100) * (particle->width + 100))
				{
					continue;
				}

				int32_t soundEffect = 0;
				switch (particle->spriteSheet)
				{
					case 7:
						if (g_buzzActor.specialAirState == 0)
						{
							break;
						}
					case 0:
					case 1:
					case 0x13:
						collisionResponse = 1;
						replacementCollisionFlags = 0;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 4:
					case 0x33:
					case 0x34:
						collisionResponse = 2;
						replacementCollisionFlags = particle->collisionFlags;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 0x10:
						soundEffect = -1;
						replacementCollisionFlags = 5;
						HUD::g_slideTimers[HUD::SLIDE_COINS] = 180;
						if (g_buzzActor.coinsCollected < 99)
						{
							g_buzzActor.coinsCollected++;
						}
						if (g_buzzActor.coinsCollected == 50)
						{
							AudioManager::PlaySoundEffect(0x4F, 0);
						}
						break;

					case 0x15:
						replacementCollisionFlags = 4;
						soundEffect = 0;
						collisionResponse = 1;
						collisionParticle = particle;
						break;

					case 0x16:
						if (g_buzzActor.collisionFlags != 0)
						{
							g_pendingFootingType = (particle->updateParam >> 1) - 0x5A;
						}
						break;

					case 5:
						collisionResponse = 2;
						replacementCollisionFlags = particle->collisionFlags;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 0x35:
						if (g_buzzActor.specialAirState == 0)
						{
							break;
						}
					case 0x36:
						collisionResponse = 1;
						replacementCollisionFlags = particle->collisionFlags;
						soundEffect = 0;
						collisionParticle = particle;
						break;

					case 0x37:
						collisionResponse = 2;
						replacementCollisionFlags = 0;
						soundEffect = 0;
						collisionParticle = particle;
						break;
				}

				if ((g_spinCooldownTimer > 20 || g_spinHoverTimer <= -120) && collisionResponse == 2)
				{
					int32_t direction =
						Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - particle->pos.x, g_buzzActor.posAngles.pos.z - particle->pos.z);
					particle->renderFlags &= ~0xA;
					direction += 0x800;
					particle->velY = -0x400;
					particle->yawAngle = 0x30;
					particle->lifetime = 50;
					particle->velX = Numerics::g_sinCosLUT[direction & 0xFFF] / 16;
					collisionResponse = 0;
					particle->velZ = Numerics::g_sinCosLUT[(direction + 0x400) & 0xFFF] / 16;
					AudioManager::PlaySoundEffect(7, &particle->pos);
				}
				else
				{
					if (replacementCollisionFlags > 0)
					{
						particle->collisionFlags = replacementCollisionFlags;
						particle->lifetime = 1;
					}
					if (replacementCollisionFlags < 0)
					{
						particle->lifetime = 1;
					}
					if (soundEffect != 0)
					{
						AudioManager::PlaySoundEffect(soundEffect, &particle->pos);
					}
				}
			}

			if (collisionResponse != 0)
			{
				int32_t direction = Nu3D::Math::CartesianToFixedAngle(
					g_buzzActor.posAngles.pos.x - collisionParticle->pos.x, g_buzzActor.posAngles.pos.z - collisionParticle->pos.z);
				HandleDamage(direction, 3);
			}
			if (g_pendingFootingType != -1)
			{
				g_footingType = g_pendingFootingType;
			}
		}
	}
}

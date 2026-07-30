#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Lighting
	{
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId);
	}

	namespace AlsToyBarn
	{
		enum DinoEncounterState
		{
			DINO_ENCOUNTER_ACTIVE = 2,
			DINO_ENCOUNTER_DEFEATED = 3,
		};

		// GLOBAL: TOY2 0x0052FA14
		int32_t g_dinoEncounterState;
		// GLOBAL: TOY2 0x0052FA20
		int32_t g_dinoTintToggle;
		// GLOBAL: TOY2 0x0052FA2C
		int32_t g_previousDinoPhase;
		// GLOBAL: TOY2 0x0052FA50
		int32_t g_dinoTintTimer;

		// STUB: TOY2 0x00421090
		void Init() {}

		// STUB: TOY2 0x00421340
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00420AF0 [PROVISIONAL]
		void Dino(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AlsToyBarn::g_dinoTintToggle = (AlsToyBarn::g_dinoTintToggle - 1) & 1;
			if (actor->actorPhase != AlsToyBarn::g_previousDinoPhase)
			{
				AlsToyBarn::g_previousDinoPhase = actor->actorPhase;
				AlsToyBarn::g_dinoTintTimer = 0x3C;
				actor->creatureRam->defenseMode = 4;
				AudioManager::PlaySoundEffect(0x79, &actor->pos);
			}

			if (actor->previousActorPhase != 0)
			{
				if (Sector::g_activeSectorIndex == 4)
				{
					Vector4I particlePosition;
					particlePosition.x = 0x14;
					particlePosition.y = -600;
					particlePosition.z = 0;

					int32_t particleAngle = ((uint16_t)actor->yawAngle + *g_randDatBufferPtr++ - 0x80) & 0xFFF;
					int32_t velocityX = Numerics::g_sinCosLUT[particleAngle] >> 3;
					int32_t velocityZ = Numerics::g_sinCosLUT[(particleAngle + 0x400) & 0xFFF] >> 3;
					Actor::ResolveBoneAttachmentPos(&particlePosition, actor, 1);
					AudioManager::PlaySoundEffect(0x78, &actor->pos);

					int32_t randomValue = *g_randDatBufferPtr;
					g_randDatBufferPtr += 2;
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
						particlePosition.x, particlePosition.y, particlePosition.z, velocityX, randomValue + 0x200, velocityZ, 0, 0, randomValue - 0x80, 0x55);
					if (actor->previousActorPhase == 0x1E)
					{
						Camera::g_shakeTimer = 0x28;
						particle->renderFlags |= Nu3D::Particles::PARTICLE_INTERACTS_WITH_BUZZ;
					}
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
					actor->previousActorPhase = 0;
			}

			if (AlsToyBarn::g_dinoEncounterState > 1)
			{
				AlsToyBarn::g_dinoTintTimer -= Renderer::g_frameDelta;
				if (AlsToyBarn::g_dinoTintTimer < 0)
				{
					AlsToyBarn::g_dinoTintTimer = 0;
					if (AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
						actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlsToyBarn::g_dinoTintToggle != 0)
				{
					actor->useTint = 1;
					actor->actorTint.r = 0x2000;
					actor->actorTint.g = 0x2000;
					actor->actorTint.b = 0x2000;
				}
				else
				{
					actor->useTint = 0;
				}
			}
			else
			{
				actor->useTint = 0;
			}

			if (actor->actorPhase < 10 && AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
			{
				actor->movementData = g_dinoMovementData + 58;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~Actor::ACTOR_FLAG_DAMAGES_BUZZ;

				int32_t effectX = actor->pos.x + actor->collisionVolumes->offset.x;
				int32_t effectY = actor->pos.y + actor->collisionVolumes->offset.y;
				int32_t effectZ = actor->pos.z + actor->collisionVolumes->offset.z;
				for (int32_t effectCount = 5; effectCount != 0; effectCount--)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x23, 0xE);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}

				Lighting::SpawnLight(effectX, effectY, effectZ, 0xF08000, 0x20, (int32_t)actor);
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AlsToyBarn::g_dinoEncounterState = AlsToyBarn::DINO_ENCOUNTER_DEFEATED;
				g_hudActorAnimationFrame = 0;
			}

			if (AlsToyBarn::g_dinoEncounterState >= AlsToyBarn::DINO_ENCOUNTER_DEFEATED && Sector::g_activeSectorIndex == 4)
			{
				int32_t yawAngle = actor->yawAngle;
				int32_t sideOffset = Numerics::g_sinCosLUT[(yawAngle - 0x400) & 0xFFF] >> 2;
				int32_t forwardOffset = Numerics::g_sinCosLUT[yawAngle & 0xFFF] >> 2;
				int32_t particleX;
				int32_t particleY;
				int32_t particleZ;
				if (AlsToyBarn::g_dinoTintToggle != 0)
				{
					particleX = actor->pos.x + sideOffset;
					particleY = actor->pos.y - 0x1D00;
					particleZ = actor->pos.z + forwardOffset;
				}
				else
				{
					particleX = actor->pos.x - sideOffset * 3;
					particleY = actor->pos.y - 0x400;
					particleZ = actor->pos.z - forwardOffset * 3;
				}

				if (g_framePulseOutputs.sevenTick != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 0x11, 0xA);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					particle->width = 0x28;
					particle->height = 0x28;
				}

				if ((*g_randDatBufferPtr++ & 3) != 0 && (actor->animationFramePosition & (int32_t)0xFFFF0000) == 0xA0000)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 4, 4);
					particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
				}
			}

			if (AlsToyBarn::g_dinoEncounterState == AlsToyBarn::DINO_ENCOUNTER_ACTIVE)
			{
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 0x36 / 0x14;
				if (Sector::g_activeSectorIndex == 4)
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 0x5A;
			}
		}

		// FUNCTION: TOY2 0x00420ED0 [MATCHED]
		void Chick(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x3C;
				AudioManager::PlaySoundEffect(0x77, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x77, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}

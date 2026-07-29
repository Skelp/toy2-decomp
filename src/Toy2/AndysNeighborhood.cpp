#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <stdlib.h>

namespace Toy2
{
	namespace AndysNeighborhood
	{
		// STUB: TOY2 0x00418E50
		void Init() {}

		// STUB: TOY2 0x004190C0
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00418610 [PROVISIONAL]
		void Army(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (actor->actorPhase == 0x66)
			{
				if (g_framePulseOutputs.thirtyTwoTick != 0 && (*g_randDatBufferPtr++ & 3) == 0)
				{
					AudioManager::PlaySoundEffect(0x48, &actor->pos);
				}

				if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
				{
					g_levelObjectiveProgress++;
					Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
					Actor::Kill(actor, Actor::KILL_REMOVE_ACTOR);
					AudioManager::PlaySoundEffect(0x49, &actor->pos);
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
				{
					actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x80;
					if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
					{
						uint8_t* randomData = g_randDatBufferPtr;
						int32_t velocityZ = *randomData++ - 0x80;
						randomData += 2;
						g_randDatBufferPtr = randomData;
						int32_t velocityX = velocityZ * 4;
						Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
							actor->pos.x, actor->pos.y - 0x1000, actor->pos.z, velocityX, -0xC00, velocityZ, 0x60, 0, velocityX, 0x79);
						AudioManager::PlaySoundEffect(0x60, &particle->pos);
					}
				}
			}
		}
		// STUB: TOY2 0x004189C0
		void ZKite(Actor::Toy2Actor::ActorBehaviourContext* context) {}
		// FUNCTION: TOY2 0x00418CE0 [PROVISIONAL]
		void LawnMower(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AudioManager::PlaySoundEffect(0x4B, &actor->pos);
			if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
			{
				if (g_framePulseOutputs.sixteenTick != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(actor->pos.x, actor->pos.y, actor->pos.z, 0x31, 2);
					particle->groundAlignRot = *g_randDatBufferPtr++ << 4;
				}

				int32_t particleX;
				int32_t particleZ;
				particleX = actor->pos.x + Numerics::g_sinCosLUT[(actor->yawAngle - 0x800) & 0xFFF];
				particleZ = actor->pos.z + Numerics::g_sinCosLUT[(actor->yawAngle - 0x400) & 0xFFF];
				for (int32_t particleIndex = 0; particleIndex < g_framePulseOutputs.twoTickCount; particleIndex++)
				{
					int32_t particleAngle = (actor->yawAngle + 0x500 + *g_randDatBufferPtr++ * 6) & 0xFFF;
					int32_t randomValue = *g_randDatBufferPtr;
					g_randDatBufferPtr += 3;
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(particleX,
						actor->pos.y,
						particleZ,
						Numerics::g_sinCosLUT[particleAngle] >> 4,
						randomValue - 0xC00,
						Numerics::g_sinCosLUT[(particleAngle + 0x400) & 0xFFF] >> 4,
						0x100,
						randomValue << 4,
						randomValue * 2 - 0x100,
						0x32);
					particle->colourG = (*g_randDatBufferPtr++ >> 2) + 0x40;
				}
			}
		}

		// FUNCTION: TOY2 0x00418720 [PROVISIONAL]
		void RCCarLevel2(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			int32_t animationIndex = 0;
			Actor::Toy2Actor* actor = context->actor;
			int32_t frameSequenceIndex = 2;
			actor->actorFlags |= Actor::ACTOR_FLAG_RC_CAR;

			if (context->localForwardSpeed > 0)
			{
				AudioManager::g_dynamicSoundFrequencies[2] = context->localForwardSpeed * 4 + 0x800;
				AudioManager::PlaySoundEffect(0x37, &actor->pos);
			}

			if (HUD::g_challengeState >= 2)
			{
				if (context->localForwardSpeed < actor->creatureRam->speedTarget * 8)
				{
					animationIndex = 1;
					frameSequenceIndex = 0xB;
				}

				if (abs(context->localStrafeSpeed) > context->localForwardSpeed)
				{
					animationIndex = context->localStrafeSpeed < 0 ? 2 : 3;
					frameSequenceIndex = 0xC;
				}

				if (animationIndex != actor->primaryAnimIdx
					&& (animationIndex != 0 || actor->primaryAnimIdx != 1 || (actor->animationFramePosition & (int32_t)0xFFFF0000) >= 0xE0000))
				{
					Actor::SetAnimation(actor, (int16_t)animationIndex, frameSequenceIndex);
				}
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == 0)
			{
				return;
			}

			Actor::UpdatePrimaryAnimation(actor);
			if (context->localForwardSpeed > 0)
			{
				g_rcCarFrontWheelRotation = (g_rcCarFrontWheelRotation + (context->localForwardSpeed * Renderer::g_frameDelta >> 3)) & 0xFFF;
				SetRCCarNodeAngle(actor, 0, g_rcCarFrontWheelRotation, 0, 0);
				SetRCCarNodeAngle(actor, 1, g_rcCarFrontWheelRotation, 0, 0);

				if (animationIndex == 1 || abs(context->localStrafeSpeed) > 0x200)
				{
					g_rcCarRearWheelRotation = (g_rcCarRearWheelRotation + Renderer::g_frameDelta * 0xA0) & 0xFFF;
				}
				else
				{
					g_rcCarRearWheelRotation = (g_rcCarRearWheelRotation + (context->localForwardSpeed * Renderer::g_frameDelta >> 3)) & 0xFFF;
				}
				SetRCCarNodeAngle(actor, 2, g_rcCarRearWheelRotation, 0, 0);
				SetRCCarNodeAngle(actor, 3, g_rcCarRearWheelRotation, 0, 0);
			}

			if (g_framePulseOutputs.fourTick != 0 && (animationIndex == 1 || abs(context->localStrafeSpeed) > 0x200))
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
					actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle + 0x680) & 0xFFF] >> 1),
					actor->pos.y - 0x800,
					actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x580) & 0xFFF] >> 1),
					0x2A,
					0xA);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;

				particle = Nu3D::Particles::SpawnFromPreset(actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x680) & 0xFFF] >> 1),
					actor->pos.y - 0x800,
					actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x280) & 0xFFF] >> 1),
					0x2A,
					0xA);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				AudioManager::PlaySoundEffect(0x36, &actor->pos);
			}
		}
	}
}

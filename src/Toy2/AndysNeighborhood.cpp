#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <stdlib.h>

namespace Toy2
{
	namespace AndysNeighborhood
	{
		enum KiteEncounterState
		{
			KITE_ENCOUNTER_ACTIVE = 2,
		};

		// GLOBAL: TOY2 0x0052F5D8
		int32_t g_kiteTintTimer;
		// GLOBAL: TOY2 0x0052F63C
		int32_t g_kiteTintToggle;
		// GLOBAL: TOY2 0x0052F65C
		int32_t g_kiteSpinAngle;
		// GLOBAL: TOY2 0x0052F664
		int32_t g_kiteEncounterState;
		// GLOBAL: TOY2 0x0052F6AC
		int32_t g_previousKitePhase;
		// GLOBAL: TOY2 0x0052F6B8
		int32_t g_kiteBobAngle;
		// GLOBAL: TOY2 0x0052F6EC
		int32_t g_kiteRollAngle;

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
		// FUNCTION: TOY2 0x004189C0 [PROVISIONAL]
		void ZKite(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			AndysNeighborhood::g_kiteTintToggle = (AndysNeighborhood::g_kiteTintToggle - 1) & 1;
			Actor::Toy2Actor* actor = context->actor;
			AndysNeighborhood::g_kiteTintTimer -= Renderer::g_frameDelta;
			if (AndysNeighborhood::g_kiteTintTimer < 0)
			{
				AndysNeighborhood::g_kiteTintTimer = 0;
				actor->useTint = 0;
			}
			else if (AndysNeighborhood::g_kiteTintToggle != 0)
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

			AndysNeighborhood::g_kiteRollAngle = (AndysNeighborhood::g_kiteRollAngle + Renderer::g_frameDelta * 0x24) & 0xFFF;
			actor->rollAngle = Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteRollAngle] >> 6;
			AndysNeighborhood::g_kiteBobAngle = (AndysNeighborhood::g_kiteBobAngle + Renderer::g_frameDelta * 0x40) & 0xFFF;
			int32_t buzzX = g_buzzActor.posAngles.pos.x;
			int32_t buzzZ = g_buzzActor.posAngles.pos.z;
			actor->targetYaw = (Nu3D::Math::CartesianToFixedAngle(actor->pos.x - buzzX, actor->pos.z - buzzZ) - 0x800) & 0xFFF;

			if (AndysNeighborhood::g_kiteEncounterState == AndysNeighborhood::KITE_ENCOUNTER_ACTIVE)
			{
				int32_t buzzY = g_buzzActor.posAngles.pos.y;
				int32_t buzzIsAboveKite = buzzY > -0x72000;
				if (buzzIsAboveKite)
				{
					actor->actorFlags = (actor->actorFlags & ~Actor::ACTOR_FLAG_TARGETS_BUZZ) | Actor::ACTOR_FLAG_TRACKS_TARGET;
					actor->motionTargetPos.x = actor->boundary.x;
					actor->motionTargetPos.z = actor->boundary.z;
					actor->creatureRam->defenseMode = 4;
					actor->movementCommandTimer = 0x32;
				}
				else
				{
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 0x5A;
					Camera::g_actorCameraTarget.x = actor->pos.x;
					Camera::g_actorCameraTarget.y = actor->pos.y;
					Camera::g_actorCameraTarget.z = actor->pos.z;
				}

				if (buzzY > -0x721AD)
					buzzY = -0x721AD;

				if ((context->targetFlags & 1) == 0 && ! buzzIsAboveKite)
				{
					actor->previousActorPhase = 2;
					actor->motionTargetPos.x = g_buzzActor.posAngles.pos.x;
					actor->motionTargetPos.y = Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteBobAngle] - 0x3000 + buzzY;
					actor->motionTargetPos.z = g_buzzActor.posAngles.pos.z;
					if (actor->motionTargetPos.y > -0x741AD)
						actor->motionTargetPos.y = -0x741AD;

					int32_t spinSpeed = 0x108 - actor->actorPhase * 0x14;
					AudioManager::g_dynamicSoundFrequencies[0] = (int16_t)spinSpeed * 0x10;
					AudioManager::PlaySoundEffect(0x3C, &actor->pos);
					AndysNeighborhood::g_kiteSpinAngle = (AndysNeighborhood::g_kiteSpinAngle + Renderer::g_frameDelta * spinSpeed) & 0xFFF;
					actor->yawAngle = (int16_t)AndysNeighborhood::g_kiteSpinAngle;
					actor->velX = Numerics::g_sinCosLUT[actor->targetYaw] >> 5;
					actor->velForward = Numerics::g_sinCosLUT[(actor->targetYaw + 0x400) & 0xFFF] >> 5;
				}
				else
				{
					if (AndysNeighborhood::g_previousKitePhase != actor->actorPhase)
					{
						AndysNeighborhood::g_kiteTintTimer = 0x3C;
						AndysNeighborhood::g_previousKitePhase = actor->actorPhase;
						actor->movementCommandTimer = 0;
						if (AndysNeighborhood::g_previousKitePhase > 0)
							AudioManager::PlaySoundEffect(0x98, &actor->pos);
					}

					actor->motionTargetPos.y = Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteBobAngle] - 0xA000 + buzzY;
					AndysNeighborhood::g_kiteSpinAngle = actor->yawAngle;
					actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
					if (actor->previousActorPhase < 0)
					{
						actor->previousActorPhase = 0x78;
						int32_t movementAngle = actor->yawAngle + 0x200;
						if (*g_randDatBufferPtr++ < 0x80)
							movementAngle -= 0x400;
						actor->velX = Numerics::g_sinCosLUT[movementAngle] >> 3;
						actor->velForward = Numerics::g_sinCosLUT[(movementAngle + 0x400) & 0xFFF] >> 3;
					}
				}
			}
			else
			{
				actor->pos.y = (Numerics::g_sinCosLUT[AndysNeighborhood::g_kiteBobAngle] >> 3) - 0x7C000;
			}

			if (actor->pos.y > -0x741AD)
				actor->pos.y = -0x741AD;
		}
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

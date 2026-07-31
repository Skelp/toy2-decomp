#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/KiteTail.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Math.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>
#include <stdlib.h>

namespace Toy2
{
	extern uint8_t g_environmentTintRed;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintBlue;

	namespace AndysNeighborhood
	{
		struct MoveableObjectInitTable
		{
			MoveableObject::InitEntry entries[2];
			int16_t terminator;
		};

		enum KiteEncounterState
		{
			KITE_ENCOUNTER_ACTIVE = 2,
		};

		// GLOBAL: TOY2 0x004F1460
		char g_molehillChallengeInstructions[] = "the soldier will surrender if you quickly ^stomp^ on the ^molehills^ that he is near.";

		// GLOBAL: TOY2 0x004F1518
		MoveableObjectInitTable g_moveableObjectInitTable = {
			{
				{ -2, 2, 4 },
				{ -2, 11, 20 },
			},
			-1,
		};
		// GLOBAL: TOY2 0x004F153C
		Collectables::TokenDialogueValue g_tokenDialogueValues[] = {
			{ 0x46 },
			{ 0xC },
			{ reinterpret_cast<int32_t>(g_molehillChallengeInstructions) },
			{ 0x800 },
		};
		// GLOBAL: TOY2 0x004F154C
		int16_t g_tokenLinkIds[] = { 0x33, 0x31, 0x32, 0x30, 0x34, 0 };

		// GLOBAL: TOY2 0x0052F5D8
		int32_t g_kiteTintTimer;
		// GLOBAL: TOY2 0x0052F5DC
		int32_t g_moleCurrentHoleIndex;
		// GLOBAL: TOY2 0x0052F5E0
		int32_t g_rcCarRaceStarted;
		// GLOBAL: TOY2 0x0052F5E4
		int32_t g_gateRotationVelocity;
		// GLOBAL: TOY2 0x0052F5E8
		int32_t g_moleTargetHoleIndex;
		// GLOBAL: TOY2 0x0052F5EC
		int32_t g_garageDoorRotationAngle;
		// GLOBAL: TOY2 0x0052F5F0
		int32_t g_gateRotationAngle;
		// GLOBAL: TOY2 0x0052F608
		int32_t g_poleScaleY;
		// GLOBAL: TOY2 0x0052F60C
		int32_t g_launchPadIsRising;
		// GLOBAL: TOY2 0x0052F610
		int32_t g_fallingTreesState;
		// GLOBAL: TOY2 0x0052F614
		int32_t g_fallingTreePitch;
		// GLOBAL: TOY2 0x0052F618
		int32_t g_fallingTreeRoll;
		// GLOBAL: TOY2 0x0052F61C
		int32_t g_moleHitTimer;
		// GLOBAL: TOY2 0x0052F620
		int32_t g_rcCarPathLap;
		// GLOBAL: TOY2 0x0052F638
		int32_t g_unusedState0;
		// GLOBAL: TOY2 0x0052F63C
		int32_t g_kiteTintToggle;
		// GLOBAL: TOY2 0x0052F640
		int32_t g_garageDoorRotationVelocity;
		// GLOBAL: TOY2 0x0052F648
		Vector3I g_launchPadPosition;
		// GLOBAL: TOY2 0x0052F658
		int32_t g_launchPadSoundTimer;
		// GLOBAL: TOY2 0x0052F65C
		int32_t g_kiteSpinAngle;
		// GLOBAL: TOY2 0x0052F660
		int32_t g_unusedRaceState;
		// GLOBAL: TOY2 0x0052F664
		int32_t g_kiteEncounterState;
		// GLOBAL: TOY2 0x0052F668
		int32_t g_rcCarPathPoint;
		// GLOBAL: TOY2 0x0052F66C
		int32_t g_fallingTreeShakeTimer;
		// GLOBAL: TOY2 0x0052F674
		int32_t g_targetLaunchPadDepression;
		// GLOBAL: TOY2 0x0052F678
		int32_t g_ambientParticlePending;
		// GLOBAL: TOY2 0x0052F680
		Vector3I g_polePosition;
		// GLOBAL: TOY2 0x0052F690
		int32_t g_launchPadDepression;
		// GLOBAL: TOY2 0x0052F6A8
		int32_t g_poleRiseSpeed;
		// GLOBAL: TOY2 0x0052F6AC
		int32_t g_previousKitePhase;
		// GLOBAL: TOY2 0x0052F6B0
		int32_t g_raceCheckpointIndex;
		// GLOBAL: TOY2 0x0052F6B4
		int32_t g_launchPadScaleY;
		// GLOBAL: TOY2 0x0052F6B8
		int32_t g_kiteBobAngle;
		// GLOBAL: TOY2 0x0052F6C4
		int32_t g_kiteHudPulseAngle;
		// GLOBAL: TOY2 0x0052F6C8
		int32_t g_firstTreeScaleY;
		// GLOBAL: TOY2 0x0052F6CC
		int32_t g_secondTreeScaleY;
		// GLOBAL: TOY2 0x0052F6D0
		int32_t g_destroyedMolehillCount;
		// GLOBAL: TOY2 0x0052F6D4
		int32_t g_molehillParticlePathPoint;
		// GLOBAL: TOY2 0x0052F6D8
		int32_t g_treeSwayAngle;
		// GLOBAL: TOY2 0x0052F6DC
		int32_t g_platform0TiltVelocity;
		// GLOBAL: TOY2 0x0052F6E0
		int32_t g_platform1TiltVelocity;
		// GLOBAL: TOY2 0x0052F6E4
		int32_t g_ambientParticlePathPoint;
		// GLOBAL: TOY2 0x0052F6E8
		int32_t g_unusedState1;
		// GLOBAL: TOY2 0x0052F6EC
		int32_t g_kiteRollAngle;

		// FUNCTION: TOY2 0x00418E50 [EFFECTIVE]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x41);
			Collectables::Activate(3, 1);
			MoveableObject::InitTable(g_moveableObjectInitTable.entries);
			Collectables::LoadTokenTable(g_tokenDialogueValues);

			g_environmentSurfaceY = 0x4400;
			g_previousBuzzEnvironmentY = 0x4400;
			g_environmentTintBlue = 0x50;
			g_environmentTintGreen = 0x60;
			g_environmentTintRed = 0x80;
			Platform::SetRotationAngles(0, 0, 0x961, 0);
			Platform::SetRotationAngles(1, 0, 0xB4A, 0);

			g_previousKitePhase = Actor::g_creatureActors[26].actorPhase;
			Actor::g_creatureActors[26].actorFlags |= Actor::ACTOR_FLAG_BOSS;
			Actor::g_creatureActors[4].previousActorPhase = 100;
			g_kiteBobAngle = 0;
			g_kiteRollAngle = 0;
			g_kiteSpinAngle = 0;
			g_kiteEncounterState = 0;
			g_kiteTintTimer = 0;
			g_kiteTintToggle = 0;
			g_platform0TiltVelocity = 0;
			g_platform1TiltVelocity = 0;
			HUD::g_challengeState = 0;
			g_rcCarPathPoint = 0;
			g_rcCarPathLap = 0;
			AndysHouse::g_raceCheckpointPassCount = 0;
			g_unusedRaceState = 0;
			g_rcCarRaceStarted = 0;
			g_raceCheckpointIndex = 0;
			g_ambientParticlePathPoint = 0;
			g_ambientParticlePending = 1;
			g_launchPadScaleY = 0xFFF;
			g_targetLaunchPadDepression = 0;
			g_launchPadDepression = 0;
			g_launchPadIsRising = 0;
			g_firstTreeScaleY = 0x1000;
			g_secondTreeScaleY = 0x1000;
			g_fallingTreeRoll = 0;
			g_fallingTreePitch = 0;
			g_treeSwayAngle = 0xC00;
			g_fallingTreeShakeTimer = 0;
			g_fallingTreesState = 0;
			g_gateRotationAngle = 0x2EE;
			g_gateRotationVelocity = 0;
			g_garageDoorRotationAngle = 0;
			g_garageDoorRotationVelocity = 0;
			g_moleCurrentHoleIndex = 0;
			g_molehillParticlePathPoint = 0;
			g_moleHitTimer = 0;
			g_moleTargetHoleIndex = 0;
			g_destroyedMolehillCount = 0;
			g_unusedState0 = 0;
			g_launchPadSoundTimer = 0;
			g_unusedState1 = 0;

			Nu3D::Link::SetRotationRelative8bit(5, 0, 0, 0x2EE);
			Nu3D::Link::GetCurrentPosFixed(6, &g_launchPadPosition);
			Platform::DisableCollision(5);
			Platform::DisableCollision(4);
			Platform::DisableCollision(12);
			Platform::AddFlags(7, 0x100);
			Platform::SetOrigin(7, g_launchPadPosition.x, g_launchPadPosition.y, g_launchPadPosition.z);
			Nu3D::Link::SetScaleFromFixedOffsets(21, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(32, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(33, 0, 0, 0);
			Nu3D::Link::GetCurrentPosFixed(25, &g_polePosition);
			g_poleScaleY = 0x118;
			Nu3D::Link::SetScaleFromFixedOffsets(27, 0x1000, g_poleScaleY, 0x1000);
			Levels::g_recordData[61]->data[0].y = g_polePosition.y;
			g_poleRiseSpeed = 0;
			g_kiteHudPulseAngle = 0;
			KiteTail::Init(&Actor::g_creatureActors[26].pos, 0x10, 0x1000, 0, 0x60);
		}

		// STUB: TOY2 0x004190C0
		void Interactions() {}

		// FUNCTION: TOY2 0x00448080 [MATCHED]
		void UpdatePickupGroundHeights()
		{
			PosAndAngles groundProbe;
			int32_t pickupIndex = 0;
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(Levels::g_recordData[63] + 1);
			if (Levels::g_recordData[63]->recordCount > 0)
			{
				do
				{
					if (pickup->position.y != INT_MIN && pickup->objectIndex < 0x30)
					{
						groundProbe.pos.x = pickup->position.x << 5;
						groundProbe.pos.z = pickup->position.z << 5;
						groundProbe.pos.y = (pickup->position.y - 10) << 5;
						pickup->groundHeight = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
						if (pickup->groundHeight == 0x7FFF)
							pickup->groundHeight = 0x7FFE;
						if (pickup->groundHeight - pickup->position.y > 0x1800)
							pickup->groundHeight = 0x7FFF;
					}
					pickup++;
					pickupIndex++;
				} while (pickupIndex < Levels::g_recordData[63]->recordCount);
			}
		}
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

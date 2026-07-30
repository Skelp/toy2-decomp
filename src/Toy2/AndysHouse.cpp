#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Collision.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <stdlib.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Lighting
	{
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId);
	}

	namespace AndysHouse
	{
		enum TinManState
		{
			TIN_MAN_STATE_ACTIVE = 2,
			TIN_MAN_STATE_DEFEATED = 3,
		};

		struct LinkOrigin
		{
			Vector3I position;
			int32_t padding;
		};

		// GLOBAL: TOY2 0x004F04D0
		extern const char g_laserAttackInstructions[] = {
#include "LaserAttackInstructions.inc"
		};
		// GLOBAL: TOY2 0x004F05BC
		extern const char g_swingBarInstructions[] =
			"if you jump at a horizontal bar you will swing around it. press jump when you have left the bar to jump farther and higher.";
		// GLOBAL: TOY2 0x004F0638
		extern const char g_cameraInstructions[] = {
#include "CameraInstructions.inc"
		};
		// GLOBAL: TOY2 0x004F0730
		extern const char g_pushInstructions[] = "if you see the ^push^ icon you can run into the object and push it out of the way. ";
		// GLOBAL: TOY2 0x004F0784
		extern const char g_extendedJumpInstructions[] =
			"if you press the jump button once and then press it again you can do an extended jump. try it to reach the open drawer.";
		// GLOBAL: TOY2 0x004F07FC
		extern const char g_poleInstructions[] = {
#include "PoleInstructions.inc"
		};
		// GLOBAL: TOY2 0x004F08B8
		extern const char g_zipLineInstructions[] = "you can jump onto zip lines to slide down them. press jump to let go of the line.";
		// GLOBAL: TOY2 0x004F090C
		extern const char g_visorInstructions[] = {
#include "VisorInstructions.inc"
		};
		// GLOBAL: TOY2 0x004F0A18
		extern const char g_stompInstructions[] = {
#include "StompInstructions.inc"
		};

		// GLOBAL: TOY2 0x004F0EDC
		int16_t g_tokenLinkIds[] = { 0x39, 0x3A, 0x3B, 0x3C, 0x30, 0 };
		// GLOBAL: TOY2 0x004F0330
		char g_tinManChallengeDialogue[] = "ha ha ha ha ... defeat the ^tin robot^ to get a ^token^!";

		// GLOBAL: TOY2 0x004F0EE8
		extern const Collectables::TokenDialogueValue g_tokenDialogueValues[] = {
			{ 0x43 },
			{ 0x1F },
			{ reinterpret_cast<int32_t>(g_cameraInstructions) },
			{ 0x800 },
			{ 0x3E },
			{ 0x20 },
			{ reinterpret_cast<int32_t>(g_swingBarInstructions) },
			{ 0x800 },
			{ 0x3F },
			{ 0x21 },
			{ reinterpret_cast<int32_t>(g_stompInstructions) },
			{ 0x983 },
			{ 0x40 },
			{ 0x22 },
			{ reinterpret_cast<int32_t>(g_pushInstructions) },
			{ 0x800 },
			{ 0x44 },
			{ 0x23 },
			{ reinterpret_cast<int32_t>(g_extendedJumpInstructions) },
			{ 0x2D7 },
			{ 0x41 },
			{ 0x24 },
			{ reinterpret_cast<int32_t>(g_poleInstructions) },
			{ 0xE2 },
			{ 0x45 },
			{ 0x25 },
			{ reinterpret_cast<int32_t>(g_zipLineInstructions) },
			{ 0x30 },
			{ 0x42 },
			{ 0x26 },
			{ reinterpret_cast<int32_t>(g_visorInstructions) },
			{ 0xC00 },
			{ 0x47 },
			{ 0x28 },
			{ reinterpret_cast<int32_t>(g_laserAttackInstructions) },
			{ 0 },
			{ -1 },
		};

		// GLOBAL: TOY2 0x004F0F90
		MoveableObject::InitEntry g_moveableObjectInitTable[] = {
			{ 1, 5, 1 },
			{ 2, 6, 2 },
			{ 3, 7, 3 },
			{ 4, 1, 4 },
			{ 7, 3, 7 },
			{ 8, 4, 8 },
			{ 0, 0, 0 },
			{ -1, 0, 0 },
		};

		// GLOBAL: TOY2 0x0052AD64
		int32_t g_raceCheckpointPassCount;
		// GLOBAL: TOY2 0x0052F4F4
		int32_t g_verticalLinkRotationAngle;
		// GLOBAL: TOY2 0x0052F4F8
		int32_t g_raceLap;
		// GLOBAL: TOY2 0x0052F4FC
		int32_t g_launchPadBounceState;
		// GLOBAL: TOY2 0x0052F500
		int32_t g_moveablePlatformRotation;
		// GLOBAL: TOY2 0x0052F504
		int32_t g_poleLiftVelocity;
		// GLOBAL: TOY2 0x0052F508
		LinkOrigin g_horizontalLinkOrigins[2];
		// GLOBAL: TOY2 0x0052F528
		int32_t g_racePathPoint;
		// GLOBAL: TOY2 0x0052F52C
		int32_t g_ambientParticlePathPoint;
		// GLOBAL: TOY2 0x0052F530
		int32_t g_poleRecordHeightOffset;
		// GLOBAL: TOY2 0x0052F538
		LinkOrigin g_verticalLinkOrigins[3];
		// GLOBAL: TOY2 0x0052F568
		int32_t g_verticalLinkCycleAngle;
		// GLOBAL: TOY2 0x0052F56C
		int32_t g_toyBoxLiftTimer;
		// GLOBAL: TOY2 0x0052F570
		int32_t g_secondToyRollVelocity;
		// GLOBAL: TOY2 0x0052F574
		int32_t g_firstToyRollVelocity;
		// GLOBAL: TOY2 0x0052F578
		int32_t g_tinManState;
		// GLOBAL: TOY2 0x0052F57C
		int32_t g_toyBoxLiftVelocity;
		// GLOBAL: TOY2 0x0052F580
		int32_t g_raceCheckpointArmed;
		// GLOBAL: TOY2 0x0052F584
		int32_t g_previousRaceCheckpointMask;
		// GLOBAL: TOY2 0x0052F58C
		int32_t g_tiltPlatformState;
		// GLOBAL: TOY2 0x0052F590
		int32_t g_tinManEffectTimer;
		// GLOBAL: TOY2 0x0052F594
		int32_t g_toyBoxState;
		// GLOBAL: TOY2 0x0052F598
		int32_t g_ambientParticlePositionIndex;
		// GLOBAL: TOY2 0x0052F59C
		int32_t g_savedAmbientEmitterHeight;
		// GLOBAL: TOY2 0x0052F5A8
		int32_t g_previousTinManPhase;
		// GLOBAL: TOY2 0x0052F5AC
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x0052F5B0
		int32_t g_ambientParticleTimer;
		// GLOBAL: TOY2 0x0052F5B4
		int32_t g_poleTargetHeight;
		// GLOBAL: TOY2 0x0052F5B8
		Vector3I g_polePosition;
		// GLOBAL: TOY2 0x0052F5C8
		int32_t g_tinManBlinkToggle;
		// GLOBAL: TOY2 0x0052F5CC
		int32_t g_rotatingPlatformAngle;
		// GLOBAL: TOY2 0x0052F5D0
		int32_t g_shieldIconAngle;

		// FUNCTION: TOY2 0x004171D0 [MATCHED]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x48);
			Collectables::LoadTokenTable(g_tokenDialogueValues);
			Collectables::Activate(3, 1);
			MoveableObject::InitTable(g_moveableObjectInitTable);

			g_ambientParticlePathPoint = 1;
			g_ambientParticlePositionIndex = 0;
			g_ambientParticleTimer = 0;
			g_verticalLinkRotationAngle = 0;
			g_verticalLinkCycleAngle = 0;
			g_rotatingPlatformAngle = 0;
			g_poleLiftVelocity = 0;
			g_tiltPlatformState = 0;
			Platform::SetRotationAngles(15, 0, 0, 0);

			g_previousTinManPhase = Actor::g_creatureActors[8].actorPhase;
			g_launchPadBounceState = 0;
			g_unusedState = 0x400;
			g_tinManState = 0;
			g_racePathPoint = 0;
			HUD::g_challengeState = 0;
			g_raceLap = 0;
			g_raceCheckpointPassCount = 0;
			g_previousRaceCheckpointMask = 0;
			g_raceCheckpointArmed = 0;
			g_firstToyRollVelocity = 0;
			g_secondToyRollVelocity = 0;
			g_toyBoxLiftTimer = 0;
			g_toyBoxLiftVelocity = 0;
			g_toyBoxState = 0;
			g_shieldIconAngle = 0;
			g_moveablePlatformRotation = 0;

			g_savedAmbientEmitterHeight = Levels::g_recordData[58]->data[Levels::g_alternateAmbientEmitterStart + 1].y;
			Levels::DeactivateAmbientEmitter(1, 1);

			Nu3D::Link::GetCurrentPosFixed(6, &g_polePosition);
			g_poleTargetHeight = g_polePosition.y + 0xB800;
			g_poleRecordHeightOffset = Levels::g_recordData[61]->data[16].y - g_polePosition.y;
			Nu3D::Link::GetCurrentPosFixed(9, &g_verticalLinkOrigins[0].position);
			Nu3D::Link::GetCurrentPosFixed(17, &g_verticalLinkOrigins[1].position);
			Nu3D::Link::GetCurrentPosFixed(18, &g_verticalLinkOrigins[2].position);
			Nu3D::Link::GetCurrentPosFixed(10, &g_horizontalLinkOrigins[0].position);
			Nu3D::Link::GetCurrentPosFixed(16, &g_horizontalLinkOrigins[1].position);

			Nu3D::Link::SetScaleFromFixedOffsets(21, 0, 0x1000, 0x1000);
			Nu3D::Link::SnapToOtherLinkUsingScale(22, 21);
			Platform::DisableCollision(8);
			Platform::DisableCollision(14);
			g_tinManEffectTimer = 0;
			g_tinManBlinkToggle = 0;
		}

		// STUB: TOY2 0x00417680
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00416AB0 [PROVISIONAL]
		void TinMan(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AndysHouse::g_tinManBlinkToggle = (AndysHouse::g_tinManBlinkToggle - 1) & 1;
			AndysHouse::g_tinManEffectTimer -= Renderer::g_frameDelta;
			if (AndysHouse::g_tinManEffectTimer < 0)
			{
				AndysHouse::g_tinManEffectTimer = 0;
				actor->useTint = 0;
			}
			else if (AndysHouse::g_tinManBlinkToggle != 0)
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

			actor->creatureRam->speedNoTarget = (actor->actorPhase + 14) * 12;
			if (actor->primaryAnimIdx == 3 || actor->primaryAnimIdx == 5)
			{
				actor->creatureRam->defenseMode = 7;
				if (actor->actorPhase != AndysHouse::g_previousTinManPhase)
				{
					AndysHouse::g_tinManEffectTimer = 60;
					if (actor->actorPhase < 10)
					{
						actor->movementData = g_tinManMovementData + 101;
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
						AndysHouse::g_tinManState = AndysHouse::TIN_MAN_STATE_DEFEATED;
					}
					else
					{
						actor->movementData = g_tinManMovementData + 90;
					}
					AndysHouse::g_previousTinManPhase = actor->actorPhase;
					actor->movementCommandTimer = 0;
				}
			}
			else
			{
				actor->creatureRam->defenseMode = 4;
			}

			int32_t canEngageBuzz;
			if ((context->targetFlags & 1) != 0)
			{
				canEngageBuzz = 1;
			}
			else
			{
				RawLoader::CreatureListRam* creatureRam = actor->creatureRam;
				int32_t deltaX = (g_buzzActor.posAngles.pos.x - actor->boundary.x) >> 5;
				int32_t deltaZ = (g_buzzActor.posAngles.pos.z - actor->boundary.z) >> 5;
				int32_t boundAngle = creatureRam->boundAngle * 8;
				int32_t boundSin = Numerics::g_sinCosLUT[boundAngle] >> 2;
				int32_t boundCos = Numerics::g_sinCosLUT[(boundAngle + 0x400) & 0xFFF] >> 2;
				int32_t boundHalfX = creatureRam->boundHalfX * 0x100;
				int32_t boundHalfZ = creatureRam->boundHalfZ * 0x100;
				if ((uint32_t)(((boundCos * deltaX - boundSin * deltaZ) >> 7) + boundHalfX) >= (uint32_t)(boundHalfX * 2)
					|| (uint32_t)(((boundCos * deltaZ + boundSin * deltaX) >> 7) + boundHalfZ) >= (uint32_t)(boundHalfZ * 2))
					canEngageBuzz = 0;
				else
					canEngageBuzz = 1;
			}

			if (actor->primaryAnimIdx == 7)
			{
				if ((actor->animationFramePosition & (int32_t)0xFFFF0000) >= 0xD0000 && Collectables::g_tokenStates[4].active == 0)
					Collectables::Activate(4, 0);

				int32_t particleX = actor->pos.x + Numerics::g_sinCosLUT[actor->yawAngle] * 2 / 3;
				int32_t particleY = actor->pos.y - 0x5000;
				int32_t particleZ = actor->pos.z + Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] * 2 / 3;
				if (g_framePulseOutputs.sevenTick != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 0x11, 0xA);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}

				if ((*g_randDatBufferPtr++ & 3) != 0 && (actor->animationFramePosition & (int32_t)0xFFFF0000) == 0xE0000)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 4, 4);
					particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
				}
			}

			if (canEngageBuzz != 0 && actor->primaryAnimIdx != 7)
			{
				if ((uint32_t)g_buzzActor.posAngles.pos.y > (uint32_t)-205114)
				{
					if (AndysHouse::g_tinManState == 0)
					{
						if (g_buzzActor.specialAirState != 0 && g_buzzActor.posAngles.pos.y < -179566)
						{
							Dialogue::Begin(8, 0x1A, AndysHouse::g_tinManChallengeDialogue, 0xE23, 0x700, -1);
							AndysHouse::g_tinManState = 1;
						}
					}
					else if (AndysHouse::g_tinManState == AndysHouse::TIN_MAN_STATE_ACTIVE)
					{
						Camera::g_actorCameraTarget.x = actor->pos.x;
						Camera::g_actorCameraTarget.y = actor->pos.y;
						Camera::g_actorCameraTarget.z = actor->pos.z;
					}
				}
			}
			else if (actor->primaryAnimIdx == 2)
			{
				actor->movementCommandTimer = 0;
				actor->movementData = g_tinManMovementData + 10;
			}

			g_hudActorAnimationFrame = (actor->actorPhase - 9) * 54 / 11;
			if (context->localForwardSpeed > 0)
			{
				AudioManager::g_dynamicSoundFrequencies[1] = context->localForwardSpeed * 2 + 0x800;
			}
			else
			{
				if (AudioManager::g_dynamicSoundFrequencies[1] > 0x200)
					AudioManager::g_dynamicSoundFrequencies[1] -= (int16_t)Renderer::g_frameDelta * 0x20;
			}

			if (actor->actorPhase >= 10 && AudioManager::g_dynamicSoundFrequencies[1] > 0x200)
				AudioManager::PlaySoundEffect(0x35, &actor->pos);
		}

		// FUNCTION: TOY2 0x00416A60 [MATCHED]
		void Sheep(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x20, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}

		// FUNCTION: TOY2 0x00416F30 [PROVISIONAL]
		void RCCarLevel1(Actor::Toy2Actor::ActorBehaviourContext* context)
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

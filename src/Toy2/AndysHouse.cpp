#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Collision.h"
#include "Toy2/Actor.h"
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
	namespace AndysHouse
	{
		struct LinkOrigin
		{
			Vector3I position;
			int32_t padding;
		};

		// GLOBAL: TOY2 0x004F04D0
		extern const char g_laserAttackInstructions[] =
			"if you press the fire button you can attack with your laser. if you press the spin button you can attack with your spin attack. hold down the "
			"button "
			"to charge up the attack. your spin attack can deflect objects that are fired at you.";
		// GLOBAL: TOY2 0x004F05BC
		extern const char g_swingBarInstructions[] =
			"if you jump at a horizontal bar you will swing around it. press jump when you have left the bar to jump farther and higher.";
		// GLOBAL: TOY2 0x004F0638
		extern const char g_cameraInstructions[] =
			"you can change camera mode using the pause menu. active camera will always try and turn to view buzz from behind. passive camera will not turn. "
			"the "
			"camera left and right buttons turn the ^camera^ in ^passive^ mode and turn ^buzz^ in ^active^ mode.";
		// GLOBAL: TOY2 0x004F0730
		extern const char g_pushInstructions[] = "if you see the ^push^ icon you can run into the object and push it out of the way. ";
		// GLOBAL: TOY2 0x004F0784
		extern const char g_extendedJumpInstructions[] =
			"if you press the jump button once and then press it again you can do an extended jump. try it to reach the open drawer.";
		// GLOBAL: TOY2 0x004F07FC
		extern const char g_poleInstructions[] =
			"you can grab onto poles. when you are holding on you can push up and down to move up and down the pole. you can press left and right to turn on "
			"the "
			"pole. press jump to let go of the pole.";
		// GLOBAL: TOY2 0x004F08B8
		extern const char g_zipLineInstructions[] = "you can jump onto zip lines to slide down them. press jump to let go of the line.";
		// GLOBAL: TOY2 0x004F090C
		extern const char g_visorInstructions[] =
			"you can target your laser by using your visor view. press the visor toggle button to activate the view. then press the target lock button to swap "
			"between visible targets, then press fire to use your laser. use it to shoot the catches holding up the side of the crib.";
		// GLOBAL: TOY2 0x004F0A18
		extern const char g_stompInstructions[] =
			"you can foot ^stomp^ where you see the ^stomp^ icon by pressing jump and then pressing ^spin^ while you are in the air. this will activate "
			"switches "
			"and catapults and will also act as an attack.";

		// GLOBAL: TOY2 0x004F0EDC
		int16_t g_tokenLinkIds[] = { 0x39, 0x3A, 0x3B, 0x3C, 0x30, 0 };

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

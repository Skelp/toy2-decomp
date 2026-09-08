#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Collision.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/TokenDialogue.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <math.h>
#include <stdlib.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Camera
	{
		extern int32_t g_cameraSmoothingDivisor;
	}

	namespace Game
	{
		void InitActor(Actor::Toy2Actor* actor, int32_t fullInit);
	}

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
		// GLOBAL: TOY2 0x004F036C
		char g_sheepQuestDialogue[] = "hi buzz! please find my ^five^ missing ^sheep^. when you find them come back and see me for a pizza planet ^token^.";
		// GLOBAL: TOY2 0x004F03E0
		char g_sheepRewardDialogue[] = "you found my ^sheep^ buzz! thank you, here is your pizza planet ^token^.";
		// GLOBAL: TOY2 0x004F042C
		char g_raceChallengeDialogue[] =
			"hi buzz! i challenge you to a race! we will race ^3^ laps around the car. if you beat me, i will give you a pizza "
			"planet ^token.^ get ready to race 3... 2... 1...";
		// GLOBAL: TOY2 0x004F0ADC
		char g_missingEarDialogue[] =
			"hey buzz! you need a ^cosmic shield^ to cross the ^green sludge^. find my missing ^ear^ and i will let you use the ^cosmic shield^!";
		// GLOBAL: TOY2 0x004F0B60
		char g_earRewardDialogue[] =
			"wow! thanks buzz! in return for finding my ^ear^ i will let you use the ^cosmic shield^. it will only protect you from "
			"harm for a little while so make sure you use it wisely.";
		// GLOBAL: TOY2 0x004F0C10
		char g_shieldHintDialogue[] = "the ^cosmic shield^ will only protect you from harm for a little while so make sure you use it wisely.";
		// GLOBAL: TOY2 0x004F0C78
		char g_hammHintDialogue[] = "^hamm^ is sitting on the sofa in the living room.";
		// GLOBAL: TOY2 0x004F0CAC
		char g_boPeepHintDialogue[] = "^bo peep^ has lost her sheep. she is on the table in the ^kitchen^.";
		// GLOBAL: TOY2 0x004F0CF0
		char g_rcCarHintDialogue[] = "^r.c. car^ is waiting to race you around the garage. he is through the hole in the door downstairs.";
		// GLOBAL: TOY2 0x004F0D54
		char g_basementHintDialogue[] =
			"you can move the boxes on the shelf in the ^basement^ to get to the token on the shelf. it is easier with the ^cosmic "
			"shield^. mr. potato head will let you use it, if you find his ^ear^ for him.";
		// GLOBAL: TOY2 0x004F0E18
		char g_tinManHintDialogue[] = "the ^tin robot^ boss is in the attic above us. you can reach the attic by climbing the plant and then the rope.";
		// GLOBAL: TOY2 0x004F0EB8
		Vector3I g_ambientParticlePositions[] = {
			{ 0xB81A8, 0x29283, -0x86785 },
			{ 0xB31E8, 0x29283, -0x86785 },
			{ 0xB81A8, 0x29283, -0x81685 },
		};

		// GLOBAL: TOY2 0x004F0EE8
		extern const Collectables::TokenDialogueTable<9> g_tokenDialogueValues = {
			{
				{ 0x43, 0x1F, g_cameraInstructions, 0x800 },
				{ 0x3E, 0x20, g_swingBarInstructions, 0x800 },
				{ 0x3F, 0x21, g_stompInstructions, 0x983 },
				{ 0x40, 0x22, g_pushInstructions, 0x800 },
				{ 0x44, 0x23, g_extendedJumpInstructions, 0x2D7 },
				{ 0x41, 0x24, g_poleInstructions, 0xE2 },
				{ 0x45, 0x25, g_zipLineInstructions, 0x30 },
				{ 0x42, 0x26, g_visorInstructions, 0xC00 },
				{ 0x47, 0x28, g_laserAttackInstructions, 0 },
			},
			-1,
		};

		// GLOBAL: TOY2 0x004F0F7C
		char* g_rotatingHintDialogues[] = {
			g_hammHintDialogue,
			g_boPeepHintDialogue,
			g_rcCarHintDialogue,
			g_basementHintDialogue,
			g_tinManHintDialogue,
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
		// GLOBAL: TOY2 0x0052F588
		int32_t g_horizontalLinkCycleAngle;
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
		// GLOBAL: TOY2 0x0052F5A0
		int32_t g_retractablePlatformScale;
		// GLOBAL: TOY2 0x0052F5A4
		int32_t g_horizontalLinkRotationAngle;
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
			Collectables::LoadTokenTable(reinterpret_cast<const Collectables::TokenDialogueValue*>(g_tokenDialogueValues.records));
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

		// FUNCTION: TOY2 0x00417380 [MATCHED]
		void UpdateHorizontalLinkEffect(int32_t cycleAngle, int32_t linkId, const LinkOrigin* origin, int32_t rotationAngle, int32_t platformId)
		{
			if (Nu3D::Math::IsWithinDistance(&origin->position, &g_buzzActor.posAngles.pos, 0x280) != 0)
			{
				int32_t positionAngle;
				cycleAngle &= 0x1FFF;
				if (cycleAngle < 0x800)
				{
					if (cycleAngle - Renderer::g_frameDelta * 0x40 < 0x400 && cycleAngle >= 0x400)
						Platform::DisableCollision(platformId);
					positionAngle = cycleAngle;
				}
				else if (cycleAngle < 0x1000)
				{
					positionAngle = 0x800;
				}
				else if (cycleAngle < 0x1800)
				{
					if (cycleAngle - Renderer::g_frameDelta * 0x40 < 0x1400 && cycleAngle >= 0x1400)
					{
						Collision::MarkPlatformAsMoving(platformId);
						AudioManager::PlaySoundEffect(0x25, &origin->position);
					}
					positionAngle = 0x1800 - cycleAngle;
				}
				else
				{
					for (int32_t particleIndex = 0; particleIndex < g_framePulseOutputs.twoTickCount; particleIndex++)
					{
						Nu3D::Particles::ParticleInstance* particle =
							Nu3D::Particles::SpawnFromPreset(origin->position.x - 0x3800, origin->position.y + 0x1000, origin->position.z, 0x18, 0xD);
						particle->groundAlignRot = *g_randDatBufferPtr++ << 4;
						particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					}
					positionAngle = 0;
				}

				Nu3D::Link::SetRotationRelative8bit(linkId, 0, 0, rotationAngle & 0xFFF);
				Nu3D::Link::SetPositionRawAndCommit(linkId,
					origin->position.x >> 5,
					(origin->position.y >> 5) - (Numerics::g_sinCosLUT[(positionAngle + 0x400) & 0xFFF] >> 6) + 0x100,
					origin->position.z >> 5);
			}
		}

		// FUNCTION: TOY2 0x00417510 [MATCHED]
		void UpdateVerticalLinkEffect(int32_t cycleAngle, int32_t linkId, const LinkOrigin* origin, int32_t rotationAngle)
		{
			if (Nu3D::Math::IsWithinDistance(&origin->position, &g_buzzActor.posAngles.pos, 0x280) != 0)
			{
				int32_t scaleAngle;
				cycleAngle &= 0x1FFF;
				if (cycleAngle < 0x800)
				{
					scaleAngle = cycleAngle;
				}
				else if (cycleAngle < 0x1000)
				{
					scaleAngle = 0x800;
				}
				else if (cycleAngle < 0x1800)
				{
					scaleAngle = 0x1800 - cycleAngle;
				}
				else
				{
					if (cycleAngle - Renderer::g_frameDelta * 0x40 < 0x1800)
					{
						Nu3D::Particles::SpawnFromPreset(origin->position.x, origin->position.y + 0x4800, origin->position.z, 0x19, 2);
						Nu3D::Particles::SpawnFromPreset(origin->position.x, origin->position.y + 0x6000, origin->position.z, 0x1A, 2);
						AudioManager::PlaySoundEffect(0x24, &origin->position);
					}

					scaleAngle = 0;
					for (int32_t particleIndex = 0; particleIndex < g_framePulseOutputs.twoTickCount; particleIndex++)
					{
						Nu3D::Particles::ParticleInstance* particle =
							Nu3D::Particles::SpawnFromPreset(origin->position.x, origin->position.y + 0x6000, origin->position.z, 0x18, 9);
						particle->groundAlignRot = *g_randDatBufferPtr++ << 4;
						particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					}
				}

				Nu3D::Link::SetRotationRelative8bit(linkId, 0, rotationAngle & 0xFFF, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(linkId, 0x1000, (Numerics::g_sinCosLUT[(scaleAngle + 0x400) & 0xFFF] >> 3) + 0xC00, 0x1000);
			}
		}

		// FUNCTION: TOY2 0x00417680 [PROVISIONAL]
		void Interactions()
		{
			if (Sector::g_activeSectorIndex == 1)
			{
				if (Actor::g_creatureActors[0].actorPhase != 0x50)
				{
					Actor::g_creatureActors[0].creatureRam->defenseMode = 0;
					g_firstToyRollVelocity += Renderer::g_frameDelta * 4;
					Actor::g_creatureActors[0].rollAngle += g_firstToyRollVelocity;
					if (Actor::g_creatureActors[0].rollAngle > 0x700)
					{
						Actor::g_creatureActors[0].rollAngle = 0x700;
						g_firstToyRollVelocity = -(g_firstToyRollVelocity / 2);
						AudioManager::PlaySoundEffect(0x32, &Actor::g_creatureActors[0].pos);
						if (g_firstToyRollVelocity > -8)
							Actor::g_creatureActors[0].actorPhase = 0x50;
					}
				}
				if (Actor::g_creatureActors[2].actorPhase != 0x50)
				{
					Actor::g_creatureActors[2].creatureRam->defenseMode = 0;
					g_secondToyRollVelocity += Renderer::g_frameDelta * 4;
					Actor::g_creatureActors[2].rollAngle = (Actor::g_creatureActors[2].rollAngle - g_secondToyRollVelocity) & 0xFFF;
					if (Actor::g_creatureActors[2].rollAngle < 0x900)
					{
						Actor::g_creatureActors[2].rollAngle = 0x900;
						g_secondToyRollVelocity = -(g_secondToyRollVelocity / 2);
						AudioManager::PlaySoundEffect(0x32, &Actor::g_creatureActors[2].pos);
						if (g_secondToyRollVelocity > -8)
							Actor::g_creatureActors[2].actorPhase = 0x50;
					}
				}
				if (Actor::g_creatureActors[0].creatureRam->defenseMode == 0
					&& Actor::g_creatureActors[2].creatureRam->defenseMode == 0)
				{
					if (g_toyBoxLiftTimer == 0)
					{
						g_toyBoxLiftTimer = 0x90;
						Platform::DisableCollision(9);
						g_toyBoxState = 2;
					}
				}
				else
				{
					if (g_buzzActor.posAngles.pos.x < 0x4C274)
					{
						if (Actor::g_creatureActors[0].creatureRam->defenseMode != 0)
							Actor::g_creatureActors[0].creatureRam->defenseMode = 4;
						if (Actor::g_creatureActors[2].creatureRam->defenseMode != 0)
							Actor::g_creatureActors[2].creatureRam->defenseMode = 4;
					}
					else
					{
						if (Actor::g_creatureActors[0].creatureRam->defenseMode != 0)
							Actor::g_creatureActors[0].creatureRam->defenseMode = 5;
						if (Actor::g_creatureActors[2].creatureRam->defenseMode != 0)
							Actor::g_creatureActors[2].creatureRam->defenseMode = 5;
					}
					if (g_buzzActor.collisionFlags != 0 && g_footingType == 14)
						Platform::DisableCollision(9);
					else
						Collision::MarkPlatformAsMoving(9);
				}
				if (g_toyBoxLiftTimer > 0)
				{
					g_toyBoxLiftTimer -= Renderer::g_frameDelta;
					g_toyBoxLiftVelocity += Renderer::g_frameDelta * 0x10;
					if (g_toyBoxLiftTimer < 1)
						g_toyBoxLiftTimer = -1;
					Vector3I position;
					Nu3D::Link::GetCurrentPosFixed(0x13, &position);
					if (position.y > 0x11C00 && g_toyBoxLiftVelocity > 0)
						g_toyBoxLiftVelocity = -(g_toyBoxLiftVelocity / 2);
					position.y += g_toyBoxLiftVelocity * Renderer::g_frameDelta;
					Nu3D::Link::SetPositionRawAndCommit(0x13, position.x >> 5, position.y >> 5, position.z >> 5);
					Nu3D::Link::GetCurrentPosFixed(0x14, &position);
					position.y += g_toyBoxLiftVelocity * Renderer::g_frameDelta;
					Nu3D::Link::SetPositionRawAndCommit(0x14, position.x >> 5, position.y >> 5, position.z >> 5);
					Nu3D::Link::GetCurrentPosFixed(0x1A, &position);
					position.y = position.y * 4 + g_toyBoxLiftVelocity * Renderer::g_frameDelta;
					Nu3D::Link::SetPositionRawAndCommit(0x1A, position.x >> 5, position.y >> 7, position.z >> 5);
				}
			}

			if (MoveableObject::g_objects[3].swingState == -1 || MoveableObject::g_objects[3].verticalVelocity != 0)
			{
				if (g_moveablePlatformRotation == 0)
				{
					Collision::MarkPlatformAsMoving(14);
					Platform::DisableCollision(1);
				}
				g_moveablePlatformRotation += Renderer::g_frameDelta * 0x10;
				if (g_moveablePlatformRotation > 0x200)
					g_moveablePlatformRotation = 0x200;
				Nu3D::Link::SetRotationRelative8bit(4, g_moveablePlatformRotation, 0, 0);
			}
			if (g_retractablePlatformScale == 0)
			{
				if (g_buzzActor.posAngles.pos.z < -0x4E942 && g_buzzActor.posAngles.pos.z > -0x58482 && g_buzzActor.posAngles.pos.x < 0x4CBB6
					&& g_buzzActor.posAngles.pos.x > 0x46376 && g_buzzActor.posAngles.pos.y < 0x4800 && g_buzzActor.posAngles.pos.y > 0
					&& g_buzzActor.specialAirState != 0)
				{
					Collision::MarkPlatformAsMoving(8);
					g_retractablePlatformScale = 8;
				}
			}
			else if (g_retractablePlatformScale < 0x1000)
			{
				g_retractablePlatformScale += Renderer::g_frameDelta * 0x40;
				if (g_retractablePlatformScale > 0xFFF)
					g_retractablePlatformScale = 0x1000;
				Nu3D::Link::SetScaleFromFixedOffsets(0x15, g_retractablePlatformScale, 0x1000, 0x1000);
				Nu3D::Link::SnapToOtherLinkUsingScale(0x16, 0x15);
			}

			if (Sector::g_activeSectorIndex == 4)
			{
				Vector3I effectPosition = { 0xB3E1A, 0x26720, -0x814C1 };
				if (Nu3D::Math::IsWithinDistance(&effectPosition, &g_buzzActor.posAngles.pos, 0x280) != 0)
				{
					if (g_framePulseOutputs.sixteenTick != 0)
					{
						Levels::RecordData* path = Levels::g_recordData[0x16];
						Vector3I* pathOrigin = path->data;
						Vector3I* pathTarget = pathOrigin + g_ambientParticlePathPoint;
						int32_t deltaX = (pathOrigin->x - pathTarget->x) >> 3;
						int32_t deltaZ = (pathOrigin->z - pathTarget->z) >> 3;
						int32_t angle = (Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) - 0x800) & 0xFFF;
						int32_t distance = (int32_t)sqrt((double)(deltaX * deltaX + deltaZ * deltaZ));
						int32_t speed = distance * 7 / 2;
						int32_t verticalSpeed = -((distance << 12) / speed) * 0x80;
						Nu3D::Particles::SpawnInstance(pathOrigin->x << 5,
							effectPosition.y,
							effectPosition.x,
							Numerics::g_sinCosLUT[angle] * speed >> 13,
							verticalSpeed >> 6,
							Numerics::g_sinCosLUT[(angle + 0x400) & 0xFFF] * speed >> 13,
							0x80,
							0,
							((int32_t)*g_randDatBufferPtr++ - 0x80) >> 2,
							0xC);
						g_ambientParticlePathPoint += 1 + (*g_randDatBufferPtr++ & 0xF);
						if (g_ambientParticlePathPoint >= path->recordCount)
							g_ambientParticlePathPoint += 1 - path->recordCount;
					}
					g_ambientParticleTimer += Renderer::g_frameDelta;
					if (g_ambientParticleTimer > 0x78)
					{
						g_ambientParticleTimer -= 0x78;
						g_ambientParticlePositionIndex += 3;
						if (g_ambientParticlePositionIndex == 9)
							g_ambientParticlePositionIndex = 0;
						Vector3I* position =
							reinterpret_cast<Vector3I*>(reinterpret_cast<int32_t*>(g_ambientParticlePositions) + g_ambientParticlePositionIndex);
						Nu3D::Particles::SpawnFromPreset(position->x, position->y - 0x800, position->z, 0x22, 2);
					}
					Vector3I* particlePosition =
						reinterpret_cast<Vector3I*>(reinterpret_cast<int32_t*>(g_ambientParticlePositions) + g_ambientParticlePositionIndex);
					for (int32_t i = 0; i < g_framePulseOutputs.twoTickCount; i++)
					{
						Nu3D::Particles::ParticleInstance* particle =
							Nu3D::Particles::SpawnFromPreset(particlePosition->x, particlePosition->y, particlePosition->z, 0x10, 10);
						particle->lifetime = (*g_randDatBufferPtr++ & 0xF) + 0x20;
					}
				}
				Vector3I hazardPosition = { 0xB74DC, 0x23F3E, -0x6634A };
				int32_t hazardDistance = Nu3D::Math::IsWithinDistance(&hazardPosition, &g_buzzActor.posAngles.pos, 0x280);
				if (hazardDistance != 0)
				{
					if (hazardDistance < 900)
						Buzz::HandleDamage(0, 2);
					if (g_framePulseOutputs.eightTick != 0)
					{
						Nu3D::Particles::ParticleInstance* particle =
							Nu3D::Particles::SpawnFromPreset(hazardPosition.x, hazardPosition.y, hazardPosition.z, 0x11, 10);
						particle->rotSpeed = ((int32_t)*g_randDatBufferPtr++ - 0x80) >> 3;
					}
				}
				if (MoveableObject::g_objects[4].swingState == -1)
					Levels::g_recordData[58]->data[Levels::g_alternateAmbientEmitterStart + 1].y = g_savedAmbientEmitterHeight;
				if (g_buzzActor.collisionFlags != 0 && (Platform::GetFlags(3) & 3) == 2 && Platform::GetContactFaceNormal(3)->y < -0x3000)
				{
					Levels::DeactivateAmbientEmitter(1, 1);
					int32_t launchVelocity = -0x980;
					if (g_groundSlamTimer != 0)
					{
						g_groundSlamTimer = 0;
						launchVelocity = -0xC00;
					}
					Buzz::Launch(launchVelocity, 2);
					AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
				}
				Actor::PlayPeriodicHintSound(1, 0xB2);
				Actor::Toy2Actor* boPeep = &Actor::g_creatureActors[1];
				if ((boPeep->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
				{
					boPeep->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
					if (g_levelObjectiveProgress >= 0)
					{
						if (g_levelObjectiveProgress == 5)
						{
							AudioManager::Preset::PlayOneShotSound2(0xB4, boPeep);
							Dialogue::Begin(1, 0x1C, g_sheepRewardDialogue, -1, 0, 1);
							g_levelObjectiveProgress = -1;
						}
						else
						{
							AudioManager::Preset::PlayOneShotSound2(0xB3, boPeep);
							Dialogue::Begin(1, 0x1C, g_sheepQuestDialogue, -1, 0, -1);
						}
					}
				}
			}

			if (Sector::g_activeSectorIndex == 2)
			{
				if (g_poleRecordOffset == 0x30 && g_poleLiftVelocity == 0)
				{
					g_poleLiftVelocity = 2;
					AudioManager::PlaySoundEffect(0x21, &g_buzzActor.posAngles.pos);
				}
				if (g_poleLiftVelocity > 0)
				{
					g_polePosition.y += g_poleLiftVelocity * Renderer::g_frameDelta;
					if (g_polePosition.y > g_poleTargetHeight)
					{
						g_poleLiftVelocity = -1;
						g_polePosition.y = g_poleTargetHeight;
					}
					Levels::g_recordData[0x3D]->data[16].y = g_poleRecordHeightOffset + g_polePosition.y;
					Nu3D::Link::SetPositionRawAndCommit(6, g_polePosition.x >> 5, g_polePosition.y >> 5, g_polePosition.z >> 5);
					g_poleLiftVelocity += Renderer::g_frameDelta * 0x80 / 4;
				}
				g_verticalLinkRotationAngle = (g_verticalLinkRotationAngle + Renderer::g_frameDelta * 0x100) & 0xFFF;
				g_verticalLinkCycleAngle = (g_verticalLinkCycleAngle + Renderer::g_frameDelta * 0x40) & 0x1FFF;
				UpdateVerticalLinkEffect(g_verticalLinkCycleAngle, 9, &g_verticalLinkOrigins[0], g_verticalLinkRotationAngle);
				UpdateVerticalLinkEffect(g_verticalLinkCycleAngle + 0xAAA, 0x11, &g_verticalLinkOrigins[1], g_verticalLinkRotationAngle + 0x200);
				UpdateVerticalLinkEffect(g_verticalLinkCycleAngle + 0x1555, 0x12, &g_verticalLinkOrigins[2], g_verticalLinkRotationAngle + 0x400);
				g_horizontalLinkRotationAngle = (g_horizontalLinkRotationAngle + Renderer::g_frameDelta * 0x80) & 0xFFF;
				g_horizontalLinkCycleAngle = (g_horizontalLinkCycleAngle + Renderer::g_frameDelta * 0x40) & 0x1FFF;
				UpdateHorizontalLinkEffect(g_horizontalLinkCycleAngle, 10, &g_horizontalLinkOrigins[0], g_horizontalLinkRotationAngle, 0xC);
				UpdateHorizontalLinkEffect(g_horizontalLinkCycleAngle + 0x1000, 0x10, &g_horizontalLinkOrigins[1], g_horizontalLinkRotationAngle + 0x200, 0xD);
				Actor::Toy2Actor* rcCar = &Actor::g_creatureActors[0x1D];
				if ((rcCar->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && Collectables::g_tokenStates[2].active != 2
					&& HUD::g_challengeState == HUD::CHALLENGE_STATE_INACTIVE)
				{
					rcCar->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
					Dialogue::Begin(0x1D, 0x1B, g_raceChallengeDialogue, 0, 0, -1);
					HUD::g_challengeState = HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA;
					g_racePathPoint = 0;
					g_raceLap = 0;
					g_raceCheckpointPassCount = 0;
					g_previousRaceCheckpointMask = 0;
					g_raceCheckpointArmed = 1;
					rcCar->actorPhase = 0xCA;
				}
				bool updateRace = true;
				if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA)
				{
					if (Nu3D::Camera::g_viewHistoryInitialized != 0)
						updateRace = false;
					else
					{
						rcCar->actorFlags |= Actor::ACTOR_FLAG_TRACKS_TARGET;
						HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
					}
				}
				else if (HUD::g_challengeState < HUD::CHALLENGE_STATE_ACTIVE)
					updateRace = false;
				if (updateRace && HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
				{
					uint32_t checkpointMask = (uint32_t)g_buzzActor.posAngles.pos.z < 0xFFFE6000;
					if (g_buzzActor.posAngles.pos.x > 0x2900)
						checkpointMask |= 2;
					if (g_buzzActor.posAngles.pos.x < 0x21000)
						checkpointMask |= 4;
					if ((uint32_t)g_buzzActor.posAngles.pos.z > 0xFFFE2000)
						checkpointMask |= 8;
					if (checkpointMask == 0xE && g_previousRaceCheckpointMask == 0xF)
					{
						if (g_raceCheckpointArmed == 0)
							g_raceCheckpointPassCount++;
						g_raceCheckpointArmed = 0;
					}
					else if (checkpointMask == 0xF && g_previousRaceCheckpointMask == 0xE)
						g_raceCheckpointArmed = 1;
					g_previousRaceCheckpointMask = checkpointMask;
				}
				if (updateRace)
				{
					Levels::RecordData* racePath = Levels::g_recordData[0x1E];
					int32_t deltaX = racePath->data[g_racePathPoint].x - (rcCar->pos.x >> 5);
					int32_t deltaZ = racePath->data[g_racePathPoint].z - (rcCar->pos.z >> 5);
					if (deltaX * deltaX + deltaZ * deltaZ < 360000)
					{
						rcCar->boundary.x = racePath->data[g_racePathPoint].x << 5;
						rcCar->boundary.z = racePath->data[g_racePathPoint].z << 5;
						if (g_raceLap < 3)
						{
							g_racePathPoint++;
							if (g_racePathPoint > 0x2C)
							{
								g_raceLap++;
								g_racePathPoint = 0;
							}
						}
						else if ((uint32_t)rcCar->pos.z > 0xFFFE6000)
						{
							if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE && g_raceCheckpointPassCount < 3)
								HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
							rcCar->actorPhase = 1;
						}
					}
					rcCar->motionTargetPos.x = racePath->data[g_racePathPoint].x << 5;
					rcCar->motionTargetPos.z = racePath->data[g_racePathPoint].z << 5;
					if (g_raceCheckpointPassCount == 3)
					{
						Collectables::Activate(2, 0);
						HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
					}
				}
			}

			if (Sector::g_activeSectorIndex == 3)
			{
				int32_t bounceMagnitude = abs(g_launchPadBounceState);
				Nu3D::Link::SetScaleFromFixedOffsets(0x17, 0x1000, bounceMagnitude * 0x80, 0x1000);
				Nu3D::Link::SetRotationRelative8bit(0x17, 0, 0, bounceMagnitude * 0x20);
				bool updateBounce = true;
				if (g_buzzActor.collisionFlags != 0 && g_footingType == 8)
				{
					if (g_launchPadBounceState == 0)
					{
						if (g_groundSlamTimer == 0)
							updateBounce = false;
						else
							g_launchPadBounceState = 2;
					}
				}
				else if (g_launchPadBounceState == 0)
					updateBounce = false;
				if (updateBounce && g_launchPadBounceState < 1)
				{
					g_launchPadBounceState += Renderer::g_frameDelta;
					if (g_launchPadBounceState > 0)
						g_launchPadBounceState = 0;
				}
				else if (updateBounce)
				{
					g_launchPadBounceState += Renderer::g_frameDelta;
					if (g_launchPadBounceState > 0x20)
						g_launchPadBounceState = -0x20;
					else if (g_launchPadBounceState > 6 && g_launchPadBounceState - Renderer::g_frameDelta < 7)
					{
						Levels::DeactivateAmbientEmitter(0, 1);
						g_groundSlamTimer = 0;
						Buzz::Launch(-0x1280, 2);
						AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
						g_buzzActor.velocity.forward = Numerics::g_sinCosLUT[0x51E];
						g_buzzActor.velocity.lateral = Numerics::g_sinCosLUT[0x11E];
						g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING | Buzz::ACTOR_FLAG_UNCONTROLLED_MOMENTUM;
						g_buzzActor.posAngles.angles.yaw = 0x11E;
						g_buzzActor.facingAngle = 0x11E;
						Camera::g_cameraSmoothingDivisor = 0x40;
					}
				}
				Actor::CollectQuestReward(0x1E, 0x1D, 0xE10, 0x6E0, 0);
			}
			Actor::RotatingHint(0x23, 0x27, g_rotatingHintDialogues);
			if (Sector::g_activeSectorIndex == 5)
			{
				g_tiltPlatformState = Platform::StepTiltPhysics(0xF, 0xF, g_tiltPlatformState, 1, -0x1C0, 0x1C0, 8);
				if (g_tinManState == 1 && Nu3D::Camera::g_viewHistoryInitialized == 0)
				{
					g_tinManState = TIN_MAN_STATE_ACTIVE;
					Actor::g_creatureActors[8].movementData = CreatureBehaviour::g_tinManMovementData + 10;
					Actor::g_creatureActors[8].actorFlags |= Actor::ACTOR_FLAG_TARGETS_BUZZ;
					Actor::g_creatureActors[8].movementCommandTimer = 0;
					g_levelInteractionTimer = 0xB4;
				}
				if (Actor::g_creatureActors[8].pos.y > -0x2DE11)
					Actor::g_creatureActors[8].pos.y = -0x2DE11;
			}
			if (Sector::g_activeSectorIndex == 6)
			{
				Actor::ItemReturnReward(0x1F, 0x19, g_missingEarDialogue, g_earRewardDialogue, g_shieldHintDialogue, 0x200, 0xA00);
				Renderer::BlitTextureByIndexOffset(8, 0x80, 0x80, 0x40, 0x40, 0, (Numerics::g_sinCosLUT[g_shieldIconAngle] >> 11) & 0x3F, 0x40, 0);
				g_shieldIconAngle = (g_shieldIconAngle + Renderer::g_frameDelta * 0x20) & 0xFFF;
			}
			if (g_poleRecordOffset == 0x3C)
			{
				int32_t buzzY = g_buzzActor.posAngles.pos.y;
				if (buzzY < -100000 && buzzY >= -150000 && Camera::g_gameplayCamera.position.view.pos.y > -150000)
				{
					Camera::g_gameplayCamera.position.view.pos.y = -150000;
					Camera::g_gameplayCamera.position.view.lookAt.y = -150000;
					int32_t delta = ((int16_t)Camera::g_gameplayCamera.state.fields.orbitPitch - 0x300) * Renderer::g_frameDelta;
					Camera::g_gameplayCamera.state.fields.orbitPitch -= delta / 8;
				}
				if (buzzY < -0x10AC0 && buzzY >= -100001 && Camera::g_gameplayCamera.position.view.pos.y < -50000)
				{
					Camera::g_gameplayCamera.position.view.pos.y = -50000;
					Camera::g_gameplayCamera.position.view.lookAt.y = -50000;
					int32_t delta = ((int16_t)Camera::g_gameplayCamera.state.fields.orbitPitch + 0x100) * Renderer::g_frameDelta;
					Camera::g_gameplayCamera.state.fields.orbitPitch -= delta / 8;
				}
				if (buzzY >= -0x10AC0 && Camera::g_gameplayCamera.position.view.pos.y < -50000)
				{
					Camera::g_gameplayCamera.position.view.pos.y = -50000;
					Camera::g_gameplayCamera.position.view.lookAt.y = -50000;
				}
			}
			g_rotatingPlatformAngle = (g_rotatingPlatformAngle + Renderer::g_frameDelta * 0x20) & 0xFFF;
			Nu3D::Link::SetRotationRelative8bit(0xE, 0, g_rotatingPlatformAngle, 0);
			if (Sector::g_activeSectorIndex != 2 && HUD::g_challengeState != 0 && (Actor::g_creatureActors[0x1D].actorFlags & Actor::ACTOR_FLAG_TARGETABLE) == 0
				&& Collectables::g_tokenStates[2].active == 0)
			{
				HUD::g_challengeState = 0;
				Game::InitActor(&Actor::g_creatureActors[0x1D], 0);
			}
			if (g_buzzActor.posAngles.pos.y > -0x436DC && g_buzzActor.posAngles.pos.z > -0x4725B && g_buzzActor.posAngles.pos.y < -0x21A16
				&& g_tinManState == TIN_MAN_STATE_ACTIVE)
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 0x5A;
			PlayLevelMusic();
		}
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

#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Levels.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Numerics.h"
#include "Random.h"

#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Lighting
	{
		void SpawnLight(int32_t x, int32_t y, int32_t z, int32_t colour, int32_t lifetime, int32_t sourceId);
	}

	namespace AlsSpaceLand
	{
			enum BuggyEncounterState
			{
				BUGGY_ENCOUNTER_ACTIVE = 2,
				BUGGY_ENCOUNTER_DEFEATED = 3,
				BUGGY_ENCOUNTER_REWARD_GIVEN = 200,
			};

			enum ZiplineChallengeState
			{
				ZIPLINE_CHALLENGE_FAILED = 4,
			};

			enum SpaceshipTokenMotionState
			{
				SPACESHIP_TOKEN_ATTACHED = 1,
				SPACESHIP_TOKEN_FALLING = 2,
				SPACESHIP_TOKEN_SETTLED = 3,
			};

		struct RotatingLinkState
		{
			int32_t rotationAngle;
			int32_t speedAngle;
		};

		union LaserPosition
		{
			Vector3I point;
			Vector4I beam;
		};

		// GLOBAL: TOY2 0x004F2A68
		char g_buggyIntroSubtitle[] =
			"hey space ranger! return to your ship immediately!... defeat the ^buzz lightyear buggy^ boss to get a pizza planet ^token^!";
		// GLOBAL: TOY2 0x004F2AE4
		char g_missingAliensSubtitle[] =
			"hi buzz! if you find my ^five^ missing ^aliens^ and come back and find me, i will give you a pizza planet ^token^.";
		// GLOBAL: TOY2 0x004F2B58
		char g_foundAliensSubtitle[] = "thanks for finding my ^aliens^ buzz! here is a pizza planet ^token^!";
		// GLOBAL: TOY2 0x004F2BA0
		char g_ziplineChallengeSubtitle[] =
			"hi buzz! if you can beat me to the end of the ^zipline^ course i will give you a pizza planet ^token^!";
		// GLOBAL: TOY2 0x004F2C08
		char g_hammHintSubtitle[] = "i saw ^hamm^ at the end of the ^laser battle zone^.";
		// GLOBAL: TOY2 0x004F2C3C
		char g_mothershipHintSubtitle[] = "the ^mothership^ has returned for the ^aliens^. she is waiting near a pile of boxes.";
		// GLOBAL: TOY2 0x004F2C94
		char g_ziplineHintSubtitle[] =
			"there is a ^flying saucer^ that wants to challenge you to a ^race^. it is waiting at the start of the ^zipline^ course.";
		// GLOBAL: TOY2 0x004F2D0C
		char g_clawHintSubtitle[] = "there is a ^token^ in the claw machine next to you. stomp the button to grab the ^token^.";
		// GLOBAL: TOY2 0x004F2D68
		char g_buggyHintSubtitle[] =
			"the ^buzz lightyear buggy^ boss is in a room that can only be reached by climbing the space ^mobile^ in the central area.";

		// GLOBAL: TOY2 0x004F2F0C
		int16_t g_tokenLinkIds[] = { 0x31, 0x33, 0x32, 0x35, 0x34, 0 };
		// GLOBAL: TOY2 0x004F2F18
		RGB16 g_surfaceParticleColours[] = {
			{ 0x80, 0, 0 },
			{ 0, 0x80, 0 },
			{ 0x80, 0, 0 },
			{ 0, 0x40, 0x80 },
		};
		// GLOBAL: TOY2 0x004F2F30
		char* g_rotatingHintSubtitles[] = {
			g_hammHintSubtitle,
			g_mothershipHintSubtitle,
			g_ziplineHintSubtitle,
			g_clawHintSubtitle,
			g_buggyHintSubtitle,
			0,
		};

		// GLOBAL: TOY2 0x004F2F44
		MoveableObject::InitEntry g_moveableObjectInitTable[] = {
			{ 0, 0, 1 },
			{ 15, 3, 2 },
			{ 19, 4, 3 },
			{ -1, -1, -1 },
		};

		// GLOBAL: TOY2 0x0052FA58
		int32_t g_buggyTintToggle;
		// GLOBAL: TOY2 0x0052FA60
		LaserPosition g_laserPositions[2];
		// GLOBAL: TOY2 0x0052FA80
		Collectables::PickupRecord* g_spaceshipTokenPickup;
		// GLOBAL: TOY2 0x0052FA84
		int32_t g_challengeBobAngle;
		// GLOBAL: TOY2 0x0052FA88
		int32_t g_previousBuggyPhase;
		// GLOBAL: TOY2 0x0052FA8C
		int32_t g_buggyPhaseTimer;
		// GLOBAL: TOY2 0x0052FA90
		int32_t g_textureAnimationFrame;
		// GLOBAL: TOY2 0x0052FA98
		Vector3I g_link21Origin;
		// GLOBAL: TOY2 0x0052FAA8
		Vector3I g_link20Origin;
		// GLOBAL: TOY2 0x0052FAB8
		Vector3I g_link23Origin;
		// GLOBAL: TOY2 0x0052FAC8
		Vector3I g_link22Origin;
		// GLOBAL: TOY2 0x0052FAD8
		int32_t g_challengePathProgress;
		// GLOBAL: TOY2 0x0052FADC
		int32_t g_challengePathSpeed;
		// GLOBAL: TOY2 0x0052FAE0
		int32_t g_hammDialogueState;
		// GLOBAL: TOY2 0x0052FAE4
		int32_t g_stompSwitchTimer;
		// GLOBAL: TOY2 0x0052FAE8
		Vector3I g_spaceshipBasePosition;
		// GLOBAL: TOY2 0x0052FAF8
		int32_t g_projectilePathPoint;
		// GLOBAL: TOY2 0x0052FAFC
		int32_t g_laserTravelDurations[2];
		// GLOBAL: TOY2 0x0052FB08
		Vector3I g_spaceshipTokenPosition;
		// GLOBAL: TOY2 0x0052FB18
		int32_t g_spaceshipTokenDetached;
		// GLOBAL: TOY2 0x0052FB1C
		int32_t g_spaceshipTokenVerticalVelocity;
		// GLOBAL: TOY2 0x0052FB20
		RotatingLinkState g_rotatingLinkStates[9];
		// GLOBAL: TOY2 0x0052FB68
		int32_t g_link22BobAngle;
		// GLOBAL: TOY2 0x0052FB6C
		int32_t g_link21BobAngle;
		// GLOBAL: TOY2 0x0052FB70
		int32_t g_link23BobAngle;
		// GLOBAL: TOY2 0x0052FB74
		int32_t g_spaceshipTokenMotionState;
		// GLOBAL: TOY2 0x0052FB78
		int32_t g_link20BobAngle;
		// GLOBAL: TOY2 0x0052FB7C
		int32_t g_buggySoundTimer;
		// GLOBAL: TOY2 0x0052FB80
		int32_t g_ambientSoundTimer;
		// GLOBAL: TOY2 0x0052FB84
		int32_t g_spaceshipSequenceTimer;
		// GLOBAL: TOY2 0x0052FB88
		int32_t g_spaceshipSequenceState;
		// GLOBAL: TOY2 0x0052FB8C
		int32_t g_swingPlatformTriggered;
		// GLOBAL: TOY2 0x0052FB90
		int32_t g_laserCooldowns[2];
		// GLOBAL: TOY2 0x0052FB98
		int32_t g_projectileScanTimer;
		// GLOBAL: TOY2 0x0052FB9C
		int32_t g_buggyLaserTimer;
		// GLOBAL: TOY2 0x0052FBA0
		int32_t g_spaceshipOrbitX;
		// GLOBAL: TOY2 0x0052FBA4
		int32_t g_spaceshipOrbitZ;
		// GLOBAL: TOY2 0x0052FBA8
		int32_t g_spaceshipLiftOffset;
		// GLOBAL: TOY2 0x0052FBAC
		int32_t g_laserPathPoints[2];

		STATIC_ASSERT(sizeof(RotatingLinkState) == 0x8);
		STATIC_ASSERT(sizeof(LaserPosition) == 0x10);

		// FUNCTION: TOY2 0x00422D20 [PROVISIONAL]
		void FireLaser(int32_t laserIndex)
		{
			g_laserCooldowns[laserIndex] -= Renderer::g_frameDelta;
			if (g_laserCooldowns[laserIndex] <= 0)
			{
				int32_t travelDuration = (*g_randDatBufferPtr++ & 7) + 8;
				g_laserCooldowns[laserIndex] = travelDuration;
				g_laserTravelDurations[laserIndex] = travelDuration;
				g_laserPathPoints[laserIndex] += *g_randDatBufferPtr++ & 7;

				Levels::RecordData* targetPath = Levels::g_recordData[laserIndex + 14];
				if (g_laserPathPoints[laserIndex] >= targetPath->recordCount)
					g_laserPathPoints[laserIndex] -= targetPath->recordCount;

				g_laserPositions[laserIndex].point.x = targetPath->data[g_laserPathPoints[laserIndex]].x << 5;
				g_laserPositions[laserIndex].point.y = targetPath->data[g_laserPathPoints[laserIndex]].y << 5;
				g_laserPositions[laserIndex].point.z = targetPath->data[g_laserPathPoints[laserIndex]].z << 5;

				if (g_buzzActor.posAngles.pos.y > -0x150F)
				{
					int32_t distanceX = (g_laserPositions[laserIndex].point.x - g_buzzActor.posAngles.pos.x) >> 8;
					int32_t distanceY = (g_laserPositions[laserIndex].point.y - g_buzzActor.posAngles.pos.y) >> 8;
					int32_t distanceZ = (g_laserPositions[laserIndex].point.z - g_buzzActor.posAngles.pos.z) >> 8;
					if (distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ < 250000)
					{
						g_laserPositions[laserIndex].point.x = g_buzzActor.posAngles.pos.x;
						g_laserPositions[laserIndex].point.y = g_buzzActor.posAngles.pos.y - 0x800;
						g_laserPositions[laserIndex].point.z = g_buzzActor.posAngles.pos.z;
					}
				}
			}

			Levels::RecordData* sourcePath = Levels::g_recordData[15 - laserIndex];
			Vector4I beamDirection;
			beamDirection.x = (sourcePath->data[g_laserPathPoints[laserIndex]].x << 5) - g_laserPositions[laserIndex].point.x;
			beamDirection.y = (sourcePath->data[g_laserPathPoints[laserIndex]].y << 5) - g_laserPositions[laserIndex].point.y;
			beamDirection.z = (sourcePath->data[g_laserPathPoints[laserIndex]].z << 5) - g_laserPositions[laserIndex].point.z;
			beamDirection.x = g_laserCooldowns[laserIndex] * beamDirection.x / g_laserTravelDurations[laserIndex];
			beamDirection.y = g_laserCooldowns[laserIndex] * beamDirection.y / g_laserTravelDurations[laserIndex];
			beamDirection.z = g_laserCooldowns[laserIndex] * beamDirection.z / g_laserTravelDurations[laserIndex];

			if (laserIndex == 0)
				Renderer::Beam::QueueBeam(9, 0x40, 400, &g_laserPositions[0].beam, &beamDirection, 0x80, 0, 0);
			else
				Renderer::Beam::QueueBeam(9, 0x40, 400, &g_laserPositions[laserIndex].beam, &beamDirection, 0, 0x40, 0x80);

			if (g_laserCooldowns[laserIndex] == g_laserTravelDurations[laserIndex])
			{
				for (int32_t particleCount = 5; particleCount != 0; particleCount--)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
						g_laserPositions[laserIndex].point.x, g_laserPositions[laserIndex].point.y, g_laserPositions[laserIndex].point.z, 4, 4);
					particle->lifetime = (*g_randDatBufferPtr++ & 0xF) * 2 + 0x18;
				}

				if (laserIndex != 0)
				{
					Lighting::SpawnLight(g_laserPositions[laserIndex].point.x,
						g_laserPositions[laserIndex].point.y,
						g_laserPositions[laserIndex].point.z,
						0x78F0,
						0x10,
						g_laserPositions[laserIndex].point.x);
				}
				else
				{
					Lighting::SpawnLight(
						g_laserPositions[0].point.x, g_laserPositions[0].point.y, g_laserPositions[0].point.z, 0xF00000, 0x10, g_laserPositions[0].point.z);
				}

				Vector3I* laserPosition = &g_laserPositions[laserIndex].point;
				AudioManager::PlaySoundEffect(7, laserPosition);
				if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, laserPosition, 0x19) != 0)
				{
					int32_t damageAngle =
						Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - laserPosition->x, g_buzzActor.posAngles.pos.z - laserPosition->z);
					Buzz::HandleDamage(damageAngle, 2);
				}
			}
		}

		// FUNCTION: TOY2 0x00423020 [PROVISIONAL]
		void Init()
		{
			MoveableObject::InitTable(g_moveableObjectInitTable);
			Collectables::Init(g_tokenLinkIds, 0x41);
			Collectables::Activate(3, 1);

			Collectables::PickupRecord* tokenPickup = reinterpret_cast<Collectables::PickupRecord*>(Collectables::g_tokenStates[3].verticalPosition - 1);
			g_environmentSurfaceY = 0;
			g_previousBuzzEnvironmentY = 0;
			g_environmentEffectType = 0;
			g_spaceshipTokenPickup = tokenPickup;
			tokenPickup->facingAngle &= Collectables::PICKUP_FLAG_PERSISTENT;
			Actor::g_creatureActors[1].actorFlags |= Actor::ACTOR_FLAG_COLLIDABLE;
			memset(g_rotatingLinkStates, 0, sizeof(g_rotatingLinkStates));

			g_challengePathProgress = 0;
			g_challengePathSpeed = 0;
			g_challengeBobAngle = 0;
			g_laserPathPoints[0] = 0;
			g_laserTravelDurations[0] = 0;
			g_laserCooldowns[0] = 0;
			g_laserPathPoints[1] = 0;
			g_laserTravelDurations[1] = 0;
			g_laserCooldowns[1] = 0;

			Nu3D::Link::GetCurrentPosFixed(10, &g_spaceshipBasePosition);
			Nu3D::Link::GetCurrentPosFixed(10, &g_spaceshipTokenPosition);
			Nu3D::Link::GetCurrentPosFixed(20, &g_link20Origin);
			Nu3D::Link::GetCurrentPosFixed(21, &g_link21Origin);
			Nu3D::Link::GetCurrentPosFixed(22, &g_link22Origin);
			Nu3D::Link::GetCurrentPosFixed(23, &g_link23Origin);

			g_spaceshipTokenPosition.y += 0x6000;
			g_spaceshipBasePosition.x -= 0x4000;
			g_spaceshipBasePosition.z -= 0x4000;
			Nu3D::Link::SetScaleFromFixedOffsets(11, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(14, 0, 0, 0);

			g_spaceshipSequenceState = 0;
			g_spaceshipLiftOffset = 0;
			g_spaceshipSequenceTimer = 0;
			g_spaceshipTokenMotionState = 0;
			g_spaceshipTokenVerticalVelocity = 0;
			g_spaceshipTokenDetached = 0;
			g_projectileScanTimer = 0;
			g_stompSwitchTimer = 0;
			int32_t previousBuggyPhase = Actor::g_creatureActors[40].actorPhase;
			g_projectilePathPoint = 0;
			g_textureAnimationFrame = 0;
			g_link20BobAngle = 0;
			g_link21BobAngle = 0;
			g_link22BobAngle = 0;
			g_link23BobAngle = 0;
			g_swingPlatformTriggered = 0;
			g_ambientSoundTimer = 0;
			g_buggySoundTimer = 60;
			g_hammDialogueState = 0;
			g_buggyTintToggle = 0;
			g_buggyPhaseTimer = 0;
			g_previousBuggyPhase = previousBuggyPhase;

			Nu3D::Link::SetPositionRawAndCommit(25, -0x2A6D, -0x4962, 0x25E1);
			HUD::g_challengeState = 0;
			AndysHouse::g_raceCheckpointPassCount = 3;
		}

		// FUNCTION: TOY2 0x00423200
		void Interactions()
		{
			RotatingLinkState* rotatingLink = g_rotatingLinkStates;
			int32_t linkId = 2;
			int32_t linkIndex;
			do
			{
				int32_t frameDelta = Renderer::g_frameDelta;
				rotatingLink->speedAngle = (rotatingLink->speedAngle + linkId * frameDelta) & 0xFFF;
				rotatingLink->rotationAngle =
					(rotatingLink->rotationAngle + (Numerics::g_sinCosLUT[rotatingLink->speedAngle] >> 10) * frameDelta) & 0xFFF;
				Nu3D::Link::SetRotationRelative8bit(linkId, 0, rotatingLink->rotationAngle, 0);
				rotatingLink++;
				linkIndex = linkId - 1;
				linkId++;
			} while (linkIndex < 8);

			Actor::CollectQuestReward(0, 10, -1, 0, 0);
			Actor::RotatingHint(41, 5, g_rotatingHintSubtitles);

			Actor::Toy2Actor* alien = &Actor::g_creatureActors[2];
			if ((alien->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				alien->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					if (g_levelObjectiveProgress == 5)
					{
						Dialogue::Begin(2, 11, g_foundAliensSubtitle, -1, 0, 1);
						g_levelObjectiveProgress = -1;
					}
					else
					{
						Dialogue::Begin(2, 11, g_missingAliensSubtitle, -1, 0, -1);
					}
				}
			}

			Actor::Toy2Actor* challenger = &Actor::g_creatureActors[1];
			if ((challenger->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				challenger->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (HUD::g_challengeState == HUD::CHALLENGE_STATE_INACTIVE)
				{
					HUD::g_challengeState = HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA;
					AndysHouse::g_raceCheckpointPassCount = 2;
					Dialogue::Begin(1, 12, g_ziplineChallengeSubtitle, -1, 0, -1);
					challenger->actorFlags &= ~Actor::ACTOR_FLAG_COLLIDABLE;
					g_challengePathSpeed = 0;
					g_challengePathProgress = 0;
				}
			}

			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
				g_challengePathProgress = 1;
			}

			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE && g_buzzActor.collisionFlags != 0)
			{
				if (g_buzzActor.posAngles.pos.y < -0x1419A)
				{
					if (Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, -0x7265C, -0x600DC, 0x129C8, 0x19148) != 0)
					{
						HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
						AndysHouse::g_raceCheckpointPassCount = 3;
						Collectables::Activate(2, 0);
					}
					else if (Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, -0x924C, -0x204C, -0x5FD94, -0x3DB94) == 0)
					{
						HUD::g_challengeState = ZIPLINE_CHALLENGE_FAILED;
					}
				}
				else
				{
					HUD::g_challengeState = ZIPLINE_CHALLENGE_FAILED;
				}
			}

			if (g_challengePathProgress != 0)
			{
				g_challengePathSpeed += Renderer::g_frameDelta * 0x10;
				if (g_challengePathSpeed > 3000)
					g_challengePathSpeed = 3000;

				if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
				{
					AudioManager::g_dynamicSoundFrequencies[0] = static_cast<int16_t>(g_challengePathSpeed) + 0x800;
					AudioManager::PlaySoundEffect(0x7E, &challenger->pos);
				}

				g_challengePathProgress += g_challengePathSpeed * Renderer::g_frameDelta;
				if ((g_challengePathProgress >> 16) > static_cast<int32_t>(Levels::g_recordData[13]->recordCount) - 2)
				{
					challenger->actorFlags |= Actor::ACTOR_FLAG_COLLIDABLE;
					g_challengePathProgress = Levels::g_recordData[13]->recordCount * 0x10000 - 0x10001;
					if (HUD::g_challengeState != HUD::CHALLENGE_STATE_COMPLETE)
						HUD::g_challengeState = ZIPLINE_CHALLENGE_FAILED;
				}
			}

			Levels::RecordData* challengePath = Levels::g_recordData[13];
			int32_t challengePathPoint = g_challengePathProgress >> 16;
			uint32_t challengePathFraction = g_challengePathProgress & 0xFFFF;
			challenger->pos.x = (challengePath->data[challengePathPoint].x << 5)
				+ ((challengePath->data[challengePathPoint + 1].x - challengePath->data[challengePathPoint].x) * challengePathFraction >> 11);
			challenger->pos.y = (challengePath->data[challengePathPoint].y << 5)
				+ ((challengePath->data[challengePathPoint + 1].y - challengePath->data[challengePathPoint].y) * challengePathFraction >> 11)
				+ (Numerics::g_sinCosLUT[g_challengeBobAngle] >> 2);
			challenger->pos.z = (challengePath->data[challengePathPoint].z << 5)
				+ ((challengePath->data[challengePathPoint + 1].z - challengePath->data[challengePathPoint].z) * challengePathFraction >> 11);
			g_challengeBobAngle = (g_challengeBobAngle + Renderer::g_frameDelta * 0x20) & 0xFFF;

			if (HUD::g_challengeState == ZIPLINE_CHALLENGE_FAILED && (challenger->actorFlags & Actor::ACTOR_FLAG_ACTIVE) == 0)
			{
				challenger->actorPhase = 0;
				g_challengePathProgress = 0;
				g_challengePathSpeed = 0;
				HUD::g_challengeState = HUD::CHALLENGE_STATE_INACTIVE;
			}
			challenger->boundary = challenger->pos;
			challenger->motionTargetPos = challenger->pos;

			if (Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, 0x29EA3, 0x9AB23, -0x6F3F3, -0x6273) != 0)
			{
				FireLaser(0);
				FireLaser(1);
			}

			if (Sector::g_currentSectorIndex == 4)
			{
				if (g_spaceshipTokenDetached == 0 && Nu3D::Math::IsWithinDistanceXZ(&g_spaceshipTokenPosition, &g_buzzActor.posAngles.pos, 1000) != 0)
				{
					g_spaceshipTokenPosition.x += (0x80 - *g_randDatBufferPtr) * 0x60;
					g_spaceshipTokenPosition.z += (0x80 - g_randDatBufferPtr[1]) * 0x60;
					g_spaceshipOrbitZ = g_randDatBufferPtr[2] << 8;
					g_spaceshipTokenDetached = 1;
					g_spaceshipOrbitX = g_randDatBufferPtr[3] << 8;
					g_randDatBufferPtr += 4;
				}

				if (g_buzzActor.collisionFlags != 0 && g_groundSlamTimer != 0 && g_footingType == 8 && g_stompSwitchTimer == 0)
				{
					g_stompSwitchTimer = 60;
					if (g_spaceshipSequenceState < 3)
						g_spaceshipSequenceState++;
					Nu3D::Link::SetScaleFromFixedOffsets(13, 0, 0, 0);
					Nu3D::Link::SetScaleFromFixedOffsets(14, 0x1000, 0x1000, 0x1000);
					Levels::DeactivateAmbientEmitter(0, 1);
				}
				if (g_stompSwitchTimer != 0)
				{
					g_stompSwitchTimer -= Renderer::g_frameDelta;
					if (g_stompSwitchTimer < 1)
					{
						g_stompSwitchTimer = 0;
						Nu3D::Link::SetScaleFromFixedOffsets(13, 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(14, 0, 0, 0);
					}
				}

				int32_t motorFrequency = 0;
				switch (g_spaceshipSequenceState)
				{
				case 1:
					g_spaceshipOrbitX += Renderer::g_frameDelta * 0x80;
					if (g_spaceshipOrbitX > 0xFFFF)
						g_spaceshipOrbitX -= 0x10000;
					motorFrequency = 0x3000;
					break;
				case 2:
					g_spaceshipOrbitZ += Renderer::g_frameDelta * 0x80;
					if (g_spaceshipOrbitZ > 0xFFFF)
						g_spaceshipOrbitZ -= 0x10000;
					motorFrequency = 0x3400;
					break;
				case 3:
					g_spaceshipLiftOffset += Renderer::g_frameDelta * 0x80;
					if (g_spaceshipLiftOffset > 0x4FFF)
					{
						g_spaceshipSequenceState = 4;
						g_spaceshipSequenceTimer = 0;
						g_spaceshipLiftOffset = 0x5000;
					}
					motorFrequency = 0x2C00;
					break;
				case 4:
				{
					g_spaceshipSequenceTimer += Renderer::g_frameDelta;
					if (g_spaceshipSequenceTimer > 59 && g_spaceshipSequenceTimer - Renderer::g_frameDelta < 60)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(11, 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(10, 0, 0, 0);
						AudioManager::PlaySoundEffect(0x81, &g_spaceshipBasePosition);
					}
					if (g_spaceshipSequenceTimer > 119)
					{
						g_spaceshipSequenceState = 5;
						Vector3I linkPosition;
						Nu3D::Link::GetCurrentPosFixed(10, &linkPosition);
						g_spaceshipTokenMotionState = Nu3D::Math::IsWithinDistanceXZ(&linkPosition, &g_spaceshipTokenPosition, 15) != 0;
					}
					break;
				}
				case 5:
					g_spaceshipLiftOffset -= Renderer::g_frameDelta * 0x80;
					if (g_spaceshipLiftOffset < 0)
					{
						g_spaceshipLiftOffset = 0;
						if (g_spaceshipTokenMotionState == SPACESHIP_TOKEN_ATTACHED)
						{
							g_spaceshipSequenceState = 6;
							if (g_spaceshipOrbitX > 0x7FFF)
								g_spaceshipOrbitX = 0x10000 - g_spaceshipOrbitX;
							if (g_spaceshipOrbitZ > 0x7FFF)
								g_spaceshipOrbitZ = 0x10000 - g_spaceshipOrbitZ;
						}
						else
						{
							g_spaceshipSequenceState = 0;
							Nu3D::Link::SetScaleFromFixedOffsets(10, 0x1000, 0x1000, 0x1000);
							Nu3D::Link::SetScaleFromFixedOffsets(11, 0, 0, 0);
							AudioManager::PlaySoundEffect(0x81, &g_spaceshipBasePosition);
						}
					}
					motorFrequency = 0x2C00;
					break;
				case 6:
					g_spaceshipOrbitX -= Renderer::g_frameDelta * 0x80;
					if (g_spaceshipOrbitX < 0)
						g_spaceshipOrbitX = 0;
					g_spaceshipOrbitZ += Renderer::g_frameDelta * 0x80;
					if (g_spaceshipOrbitZ > 0x8000)
						g_spaceshipOrbitZ = 0x8000;
					if (g_spaceshipOrbitX == 0 && g_spaceshipOrbitZ == 0x8000)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(10, 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(11, 0, 0, 0);
						g_spaceshipSequenceState = 7;
						g_spaceshipTokenMotionState = SPACESHIP_TOKEN_FALLING;
						g_spaceshipTokenVerticalVelocity = 0;
						AudioManager::PlaySoundEffect(0x81, &g_spaceshipBasePosition);
					}
					else
					{
						motorFrequency = 0x2C00;
					}
					break;
				}

				if (motorFrequency != 0)
				{
					AudioManager::g_dynamicSoundFrequencies[0] = motorFrequency;
					AudioManager::PlaySoundEffect(0x82, &g_spaceshipBasePosition);
				}

				int32_t orbitX = g_spaceshipOrbitX;
				if (orbitX > 0x7FFF)
					orbitX = 0x10000 - orbitX;
				int32_t orbitZ = g_spaceshipOrbitZ;
				if (orbitZ > 0x7FFF)
					orbitZ = 0x10000 - orbitZ;
				int32_t spaceshipX = g_spaceshipBasePosition.x + orbitX;
				int32_t spaceshipY = g_spaceshipBasePosition.y + g_spaceshipLiftOffset;
				int32_t spaceshipZ = g_spaceshipBasePosition.z + orbitZ;
				Nu3D::Link::SetPositionRawAndCommit(10, spaceshipX >> 5, spaceshipY >> 5, spaceshipZ >> 5);
				Nu3D::Link::SetPositionRawAndCommit(11, spaceshipX >> 5, spaceshipY >> 5, spaceshipZ >> 5);
				Nu3D::Link::SetPositionRawAndCommit(12, spaceshipX >> 5, (spaceshipY >> 5) - 0x420, spaceshipZ >> 5);
				Nu3D::Link::SetPositionRawAndCommit(16, spaceshipX >> 5, (g_spaceshipBasePosition.y >> 5) - 0x420, spaceshipZ >> 5);

				if (Renderer::Shadows::g_shadowCount < 47)
				{
					Renderer::Shadows::ShadowInstance* shadow = &Renderer::Shadows::g_shadowInstances[Renderer::Shadows::g_shadowCount++];
					shadow->pos.x = spaceshipX;
					shadow->pos.y = -64000;
					shadow->pos.z = spaceshipZ;
					shadow->size = 0x100;
				}

				if (g_spaceshipTokenMotionState == SPACESHIP_TOKEN_ATTACHED)
				{
					g_spaceshipTokenPosition.x = spaceshipX;
					g_spaceshipTokenPosition.y = spaceshipY + 0x1000;
					g_spaceshipTokenPosition.z = spaceshipZ;
				}
				else if (g_spaceshipTokenMotionState == SPACESHIP_TOKEN_FALLING)
				{
					g_spaceshipTokenVerticalVelocity += Renderer::g_frameDelta * 0x20;
					g_spaceshipTokenPosition.y += g_spaceshipTokenVerticalVelocity * Renderer::g_frameDelta;
					if (g_spaceshipTokenPosition.y > -0x6D41)
					{
						g_spaceshipTokenPickup->facingAngle |= 0x28;
						g_spaceshipTokenPosition.x -= Renderer::g_frameDelta * 0x100;
						g_spaceshipTokenPosition.z += Renderer::g_frameDelta * 0x100;
					}
					if (g_spaceshipTokenPosition.y > -0x1800)
					{
						g_spaceshipTokenPosition.y = -0x1800;
						g_spaceshipTokenVerticalVelocity = -(g_spaceshipTokenVerticalVelocity / 2);
						if (abs(g_spaceshipTokenVerticalVelocity) < 0x100)
							g_spaceshipTokenMotionState = SPACESHIP_TOKEN_SETTLED;
					}
				}

				RotatingLinkState* spaceshipRotation = &g_rotatingLinkStates[8];
				spaceshipRotation->speedAngle = (spaceshipRotation->speedAngle + Renderer::g_frameDelta * 8) & 0xFFF;
				spaceshipRotation->rotationAngle =
					(spaceshipRotation->rotationAngle + (Numerics::g_sinCosLUT[spaceshipRotation->speedAngle] >> 11) * Renderer::g_frameDelta) & 0xFFF;
				Nu3D::Link::SetRotationRelative8bit(10, 0, spaceshipRotation->rotationAngle, 0);
				Nu3D::Link::SetRotationRelative8bit(11, 0, spaceshipRotation->rotationAngle, 0);

				if (g_spaceshipTokenPickup->position.y != INT_MIN)
				{
					Nu3D::Link::SetPositionRawAndCommit(53,
						g_spaceshipTokenPosition.x >> 5,
						g_spaceshipTokenPosition.y >> 5,
						g_spaceshipTokenPosition.z >> 5);
					g_spaceshipTokenPickup->position.x = g_spaceshipTokenPosition.x >> 5;
					g_spaceshipTokenPickup->position.y = g_spaceshipTokenPosition.y >> 5;
					g_spaceshipTokenPickup->position.z = g_spaceshipTokenPosition.z >> 5;
					g_spaceshipTokenPickup->groundHeight = g_spaceshipTokenMotionState < 2 ? -0x7D0 : 0;
				}

				Renderer::BlitTextureByIndexOffset(5, 0, 0, 0x40, 0x40, 0, g_textureAnimationFrame / 2, 0, 0x40);
				g_textureAnimationFrame = (g_textureAnimationFrame + Renderer::g_frameDelta) & 0x7F;

				g_link21BobAngle = (g_link21BobAngle + Renderer::g_frameDelta * 7) & 0xFFF;
				g_link20BobAngle = (g_link20BobAngle + Renderer::g_frameDelta * 8) & 0xFFF;
				g_link22BobAngle = (g_link22BobAngle + Renderer::g_frameDelta * 6) & 0xFFF;
				g_link23BobAngle = (g_link23BobAngle + Renderer::g_frameDelta * 11) & 0xFFF;
				Nu3D::Link::SetPositionRawAndCommit(
					20, g_link20Origin.x >> 5, (g_link20Origin.y >> 5) + (Numerics::g_sinCosLUT[g_link20BobAngle] >> 6), g_link20Origin.z >> 5);
				Nu3D::Link::SetPositionRawAndCommit(
					21, (g_link21Origin.x >> 5) + (Numerics::g_sinCosLUT[g_link21BobAngle] >> 6), g_link21Origin.y >> 5, g_link21Origin.z >> 5);
				Nu3D::Link::SetPositionRawAndCommit(
					22, (g_link22Origin.x >> 5) + (Numerics::g_sinCosLUT[g_link22BobAngle] >> 6), g_link22Origin.y >> 5, g_link22Origin.z >> 5);
				Nu3D::Link::SetPositionRawAndCommit(
					23, g_link23Origin.x >> 5, (g_link23Origin.y >> 5) + (Numerics::g_sinCosLUT[g_link23BobAngle] >> 6), g_link23Origin.z >> 5);

				g_ambientSoundTimer -= Renderer::g_frameDelta;
				if (g_ambientSoundTimer < 1)
				{
					g_ambientSoundTimer = (*g_randDatBufferPtr & 0x7F) + 10;
					int32_t soundId = g_randDatBufferPtr[1] & 3;
					if (soundId == 3)
						soundId = 0;
					int32_t sourceIndex = g_randDatBufferPtr[2] & 3;
					g_randDatBufferPtr += 3;
					Vector3I* soundPosition;
					switch (sourceIndex)
					{
					case 0:
						soundPosition = &g_link20Origin;
						break;
					case 1:
						soundPosition = &g_link21Origin;
						break;
					case 2:
						soundPosition = &g_link22Origin;
						break;
					default:
						soundPosition = &g_link23Origin;
						break;
					}
					AudioManager::PlaySoundEffect(soundId + 0x7B, soundPosition);
				}

				if (g_swingPlatformTriggered == 0)
				{
					Nu3D::Link::SetRotationRelative8bit(19, Numerics::g_sinCosLUT[g_framePulsePhases.sixtyFourTick * 0x40] >> 7, 0, 0);
					if (g_buzzActor.posAngles.pos.y < -0x1AB05 && g_buzzActor.collisionFlags != 0
						&& Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, 0x693FC, 0x753FC, 0x4B2EC, 0x536EC) != 0)
					{
						MoveableObject::g_objects[2].swingState = -1;
						g_swingPlatformTriggered = 1;
						Nu3D::Link::SetRotationRelative8bit(19, 0, 0, 0);
					}
				}
			}

			int32_t surfaceY;
			if (Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, 0x3320E, 0x5960E, 0x269CA, 0x49D8A) != 0)
			{
				surfaceY = -0x8200;
				g_environmentEffectType = 3;
				g_environmentSurfaceY = surfaceY;
			}
			else
			{
				surfaceY = 0;
				g_environmentEffectType = 0;
				g_environmentSurfaceY = 0;
			}

			if ((g_buzzActor.actorFlags & Buzz::ACTOR_FLAG_BLOCK_EDGE_IDLE) != 0 && g_framePulseOutputs.fourTick != 0
				&& (g_buzzActor.forwardSpeed > 0x20 || abs(g_buzzActor.velocity.vertical) > 0x20))
			{
				Nu3D::Particles::ParticleInstance* particle =
					Nu3D::Particles::SpawnFromPreset(g_buzzActor.posAngles.pos.x, surfaceY, g_buzzActor.posAngles.pos.z, 0x5F, 4);
				RGB16* colour = &g_surfaceParticleColours[*g_randDatBufferPtr++ & 3];
				particle->colourR = static_cast<uint8_t>(colour->r);
				particle->colourG = static_cast<uint8_t>(colour->g);
				particle->colourB = static_cast<uint8_t>(colour->b);
			}

			if (Sector::g_currentSectorIndex == 2 && g_buzzActor.posAngles.pos.x < 0x1B467)
			{
				g_projectileScanTimer += Renderer::g_frameDelta;
				if (g_projectileScanTimer > 200 && g_framePulseOutputs.sixteenTick != 0)
				{
					Levels::RecordData* projectilePath = Levels::g_recordData[16];
					Vector3I projectilePosition;
					projectilePosition.x = projectilePath->data[g_projectilePathPoint].x << 5;
					projectilePosition.y = projectilePath->data[g_projectilePathPoint].y << 5;
					g_projectilePathPoint++;
					projectilePosition.z = projectilePath->data[g_projectilePathPoint].z << 5;
					if (g_projectilePathPoint >= projectilePath->recordCount)
					{
						g_projectilePathPoint = 0;
						g_projectileScanTimer = 0;
					}

					if (Nu3D::Math::IsWithinDistance(&projectilePosition, &g_buzzActor.posAngles.pos, 0x300) != 0)
					{
						int32_t deltaX = (projectilePosition.x - g_buzzActor.posAngles.pos.x) >> 5;
						int32_t deltaY = projectilePosition.y - g_buzzActor.posAngles.pos.y;
						int32_t deltaZ = (projectilePosition.z - g_buzzActor.posAngles.pos.z) >> 5;
						int32_t yaw = Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF;
						int32_t distance = static_cast<int32_t>(sqrt(static_cast<float>(deltaX * deltaX + deltaZ * deltaZ)));
						int32_t horizontalSpeed = distance * 2 / 3;
						int32_t travelTime = (distance << 12) / horizontalSpeed;
						int32_t velocityX = Numerics::g_sinCosLUT[(yaw - 0x800) & 0xFFF] * horizontalSpeed / 0x4000;
						int32_t velocityY = (-travelTime * 0x80) / 0x100 - (deltaY * 0x80) / travelTime;
						int32_t velocityZ = Numerics::g_sinCosLUT[(yaw - 0x400) & 0xFFF] * horizontalSpeed / 0x4000;
						Nu3D::Particles::SpawnInstance(projectilePosition.x,
							projectilePosition.y,
							projectilePosition.z,
							velocityX,
							velocityY,
							velocityZ,
							0x80,
							0,
							0,
							0x60);
						AudioManager::PlaySoundEffect(13, &projectilePosition);
					}
				}
			}

			Vector3I linkPosition;
			Nu3D::Link::GetCurrentPosFixed(0, &linkPosition);
			Nu3D::Link::SetPositionRawAndCommit(17, linkPosition.x >> 7, linkPosition.y >> 7, linkPosition.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(15, &linkPosition);
			Nu3D::Link::SetPositionRawAndCommit(18, linkPosition.x >> 7, linkPosition.y >> 7, linkPosition.z >> 7);
			if ((Actor::g_creatureActors[2].actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
			{
				Renderer::BlitTextureByIndexOffset(18, 0x40, 0, 0x14, 0x40, 0, g_framePulsePhases.sixtyFourTick, 0x14, 0);
			}

			bool projectileActive = false;
			for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
			{
				Nu3D::Particles::ParticleInstance* particle = &Nu3D::Particles::g_particleInstances[particleIndex];
				if (particle->typeId == 0x72 && particle->lifetime != 0)
				{
					projectileActive = true;
					break;
				}
			}
			if (!projectileActive)
				Nu3D::Link::SetPositionRawAndCommit(25, -0x2A6D, -0x4962, 0x25E1);

			if (g_hammDialogueState == 0 && g_buzzActor.collisionFlags != 0 && Sector::g_currentSectorIndex == 5
				&& g_buzzActor.posAngles.pos.y >= -0x27A7F)
			{
				g_hammDialogueState = 1;
				Dialogue::Begin(40, 17, g_buggyIntroSubtitle, -1, 0, -1);
			}
			if (g_hammDialogueState == 1 && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				Actor::Toy2Actor* buggy = &Actor::g_creatureActors[40];
				buggy->movementData = CreatureBehaviour::g_buggyMovementData + 14;
				g_levelInteractionTimer = 180;
				g_hammDialogueState = BUGGY_ENCOUNTER_ACTIVE;
				buggy->movementCommandTimer = 0;
				buggy->creatureRam->initialFacingAngle = 0;
				buggy->previousActorPhase = 400;
				g_buggyLaserTimer = 600;
			}
			if (g_hammDialogueState > BUGGY_ENCOUNTER_ACTIVE)
			{
				if (g_hammDialogueState < 120)
				{
					g_hammDialogueState += Renderer::g_frameDelta;
					PlayLevelMusic();
					return;
				}
				if (g_hammDialogueState != BUGGY_ENCOUNTER_REWARD_GIVEN)
				{
					Collectables::Activate(4, 0);
					g_hammDialogueState = BUGGY_ENCOUNTER_REWARD_GIVEN;
				}
			}

			PlayLevelMusic();
		}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x00422660 [PROVISIONAL]
		void BBuggy(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			if (context->localForwardSpeed > 0)
			{
				AudioManager::g_dynamicSoundFrequencies[0] = context->localForwardSpeed * 4 + 0x800;
				AudioManager::PlaySoundEffect(0x7F, &actor->pos);
			}

			if (Sector::g_currentSectorIndex != 5)
				actor->motionTargetPos = actor->boundary;

			AlsSpaceLand::g_buggyTintToggle = (AlsSpaceLand::g_buggyTintToggle - 1) & 1;
			if (actor->actorPhase != AlsSpaceLand::g_previousBuggyPhase)
			{
				AlsSpaceLand::g_previousBuggyPhase = actor->actorPhase;
				AlsSpaceLand::g_buggyPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
			}

			if (AlsSpaceLand::g_hammDialogueState > 1)
			{
				AlsSpaceLand::g_buggyPhaseTimer -= Renderer::g_frameDelta;
				if (AlsSpaceLand::g_buggyPhaseTimer < 0)
				{
					AlsSpaceLand::g_buggyPhaseTimer = 0;
					if (AlsSpaceLand::g_hammDialogueState == AlsSpaceLand::BUGGY_ENCOUNTER_ACTIVE)
						actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlsSpaceLand::g_buggyTintToggle != 0)
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

			if (AlsSpaceLand::g_hammDialogueState == AlsSpaceLand::BUGGY_ENCOUNTER_ACTIVE)
			{
				AlsSpaceLand::g_buggyLaserTimer -= Renderer::g_frameDelta;
				if (AlsSpaceLand::g_buggyLaserTimer < 0)
				{
					AlsSpaceLand::g_buggyLaserTimer = 240;
				}
				else if (AlsSpaceLand::g_buggyLaserTimer < 40 && g_framePulseOutputs.eightTick != 0)
				{
					AudioManager::PlaySoundEffect(2, &actor->pos);
					Vector3I beamOffset = { 0, 0, 0 };
					Vector3I beamPosition = {
						actor->pos.x + (Numerics::g_sinCosLUT[actor->yawAngle & 0xFFF] >> 2),
						actor->pos.y - 0x1000,
						actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] >> 2),
					};
					SpawnBeamShot(0, actor->yawAngle, 0, &beamPosition, &beamOffset, 4, 0);

					if (beamPosition.y < g_buzzActor.posAngles.pos.y && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 0x200) != 0)
					{
						int32_t attackAngle =
							Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - actor->pos.x, g_buzzActor.posAngles.pos.z - actor->pos.z);
						if (((attackAngle - (uint16_t)actor->yawAngle + 0x20) & 0xFFF) < 0x40)
						{
							Buzz::HandleDamage(attackAngle, 2);
							Lighting::SpawnLight(g_buzzActor.posAngles.pos.x,
								g_buzzActor.posAngles.pos.y,
								g_buzzActor.posAngles.pos.z,
								0xF00000,
								0x10,
								g_buzzActor.posAngles.pos.z);
						}
					}
				}

				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 54 / 20;
				if (Sector::g_currentSectorIndex == 5)
				{
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
					Camera::g_actorCameraTarget.x = actor->pos.x;
					Camera::g_actorCameraTarget.y = actor->pos.y;
					Camera::g_actorCameraTarget.z = actor->pos.z;
					AlsSpaceLand::g_buggySoundTimer -= Renderer::g_frameDelta;
					if (AlsSpaceLand::g_buggySoundTimer <= 0)
					{
						AlsSpaceLand::g_buggySoundTimer = *g_randDatBufferPtr++ + 0x708;
						AudioManager::Preset::PlayOneShotSound(0xD4, &g_buzzActor);
					}
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase < 0)
				{
					AudioManager::PlaySoundEffect(0x4C, &actor->pos);
					actor->previousActorPhase = 400;
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnInstance(
						actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x800) & 0xFFF] >> 2),
						actor->pos.y - 0x2000,
						actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x400) & 0xFFF] >> 2),
						0,
						-2,
						0,
						actor->yawAngle * 4,
						0,
						0x80,
						0x72);
					particle->discPitchAngle = 0x400;
				}
			}

			if (actor->actorPhase < 10 && AlsSpaceLand::g_hammDialogueState == AlsSpaceLand::BUGGY_ENCOUNTER_ACTIVE)
			{
				actor->movementData = g_buggyMovementData + 34;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~Actor::ACTOR_FLAG_DAMAGES_BUZZ;

				int32_t effectX = actor->pos.x + actor->collisionVolumes->offset.x;
				int32_t effectY = actor->pos.y + actor->collisionVolumes->offset.y;
				int32_t effectZ = actor->pos.z + actor->collisionVolumes->offset.z;
				for (int32_t particleCount = 5; particleCount != 0; particleCount--)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(effectX, effectY, effectZ, 0x23, 0xE);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}

				Lighting::SpawnLight(effectX, effectY, effectZ, 0xF08000, 0x20, (int32_t)actor);
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AlsSpaceLand::g_hammDialogueState = AlsSpaceLand::BUGGY_ENCOUNTER_DEFEATED;
				g_hudActorAnimationFrame = 0;
			}

			if (g_framePulseOutputs.fourTick != 0)
			{
				int32_t effectMode = 0;
				if (actor->primaryAnimIdx == 1 && actor->animationFramePosition < 0x80000)
					effectMode = 2;
				if (actor->primaryAnimIdx == 0 && abs(context->localStrafeSpeed) > 0x200)
					effectMode = 1;

				if (effectMode != 0)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
						actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle + 0x680) & 0xFFF] >> 1),
						actor->pos.y - 0x800,
						actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x580) & 0xFFF] >> 1),
						0x2A,
						0xA);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					if (effectMode == 2)
					{
						particle->velX = -0x80;
						particle->velZ = 0x100;
					}

					particle = Nu3D::Particles::SpawnFromPreset(actor->pos.x + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x680) & 0xFFF] >> 1),
						actor->pos.y - 0x800,
						actor->pos.z + (Numerics::g_sinCosLUT[(actor->yawAngle - 0x280) & 0xFFF] >> 1),
						0x2A,
						0xA);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					if (effectMode == 2)
					{
						particle->velX = -0x80;
						particle->velZ = 0x100;
					}
					AudioManager::PlaySoundEffect(0x36, &actor->pos);
				}
			}

			if (actor->primaryAnimIdx == 2 && g_framePulseOutputs.sevenTick != 0)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(
					actor->pos.x - Numerics::g_sinCosLUT[actor->yawAngle & 0xFFF] / 3,
					actor->pos.y - 0x2000,
					actor->pos.z - Numerics::g_sinCosLUT[(actor->yawAngle + 0x400) & 0xFFF] / 3,
					0x11,
					0xA);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
			}
		}

		// FUNCTION: TOY2 0x00422C70 [MATCHED]
		void Martian(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x12C;
				AudioManager::PlaySoundEffect(0x80, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0xB8, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}

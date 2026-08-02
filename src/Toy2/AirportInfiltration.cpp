#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Collision.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Lighting.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "RawLoader.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>
#include <stdlib.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace Path
	{
		// FUNCTION: TOY2 0x0042C200 [PROVISIONAL]
		void SamplePoint(int32_t pathRecordType, int32_t pathPosition, Vector4I* position)
		{
			int32_t pointIndex = pathPosition / 0x1000;
			int32_t fraction = pathPosition & 0xFFF;

			position->x =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].x - Levels::g_recordData[pathRecordType]->data[pointIndex].x) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].x;
			position->y =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].y - Levels::g_recordData[pathRecordType]->data[pointIndex].y) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].y;
			position->z =
				(Levels::g_recordData[pathRecordType]->data[pointIndex + 1].z - Levels::g_recordData[pathRecordType]->data[pointIndex].z) * fraction / 0x1000
				+ Levels::g_recordData[pathRecordType]->data[pointIndex].z;
		}
	}

	namespace Platform
	{
		struct PathPlatformState
		{
			int32_t pathPosition;
			int32_t speed;
			int32_t previousX;
			int32_t previousZ;
			int32_t platformIndex;
			int32_t pathRecordType;
			int16_t facingAngle;
			int16_t emitParticles;
			int16_t primaryLinkIndex;
			int16_t secondaryLinkIndex;
		};

		// GLOBAL: TOY2 0x0052FE48
		PathPlatformState g_pathPlatforms[5];

		STATIC_ASSERT(sizeof(PathPlatformState) == 0x20);
		STATIC_ASSERT(offsetof(PathPlatformState, platformIndex) == 0x10);
		STATIC_ASSERT(offsetof(PathPlatformState, facingAngle) == 0x18);
		STATIC_ASSERT(offsetof(PathPlatformState, primaryLinkIndex) == 0x1C);

		// FUNCTION: TOY2 0x0042C2B0 [MATCHED]
		void InitPathPlatform(int32_t pathIndex,
			int32_t platformIndex,
			int32_t pathRecordType,
			int32_t primaryLinkIndex,
			int32_t secondaryLinkIndex,
			int32_t speedLimit,
			int32_t pathPosition,
			int32_t facingAngle)
		{
			g_pathPlatforms[pathIndex].pathPosition = pathPosition;
			g_pathPlatforms[pathIndex].speed = 0;
			g_pathPlatforms[pathIndex].platformIndex = platformIndex;
			g_pathPlatforms[pathIndex].pathRecordType = pathRecordType;
			g_pathPlatforms[pathIndex].primaryLinkIndex = (int16_t)primaryLinkIndex;
			g_pathPlatforms[pathIndex].secondaryLinkIndex = (int16_t)secondaryLinkIndex;
			g_pathPlatforms[pathIndex].facingAngle = (int16_t)facingAngle;
			g_pathPlatforms[pathIndex].emitParticles = 1;
			g_pathPlatforms[pathIndex].previousX = INT_MAX;
			g_pathPlatforms[pathIndex].previousZ = INT_MAX;

			Vector4I position;
			Path::SamplePoint(g_pathPlatforms[pathIndex].pathRecordType, g_pathPlatforms[pathIndex].pathPosition, &position);
			SetOrigin(g_pathPlatforms[pathIndex].platformIndex, position.x << 5, position.y << 5, position.z << 5);
			if (g_pathPlatforms[pathIndex].secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetPositionRawAndCommit(g_pathPlatforms[pathIndex].secondaryLinkIndex, position.x, position.y, position.z);
			}
			Nu3D::Link::SetPositionRawAndCommit(g_pathPlatforms[pathIndex].primaryLinkIndex, position.x, position.y, position.z);
			SetRotationAngles(platformIndex, 0, (int16_t)facingAngle, 0);
			Nu3D::Link::SetRotationRelative8bit(primaryLinkIndex, 0, facingAngle, 0);
			if (secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetRotationRelative8bit(secondaryLinkIndex, 0, facingAngle, 0);
			}
		}
	}

	namespace AirportInfiltration
	{
		enum FramePulseIndex
		{
			FRAME_PULSE_SEVEN_TICK = 7,
			FRAME_PULSE_SIXTEEN_TICK = 9,
		};

		enum ProspectorState
		{
			PROSPECTOR_STATE_IDLE = 0,
			PROSPECTOR_STATE_DIALOGUE = 1,
			PROSPECTOR_STATE_ACTIVE = 2,
			PROSPECTOR_STATE_DEFEATED = 3,
			PROSPECTOR_STATE_TOKEN_DELAY_END = 120,
			PROSPECTOR_STATE_TOKEN_AWARDED = 200,
		};

		// GLOBAL: TOY2 0x004F4668
		Vector3I g_fanParticleVelocities[5] = {
			{ 0, 0, 0x500 },
			{ 0x500, 0, 0 },
			{ 0, 0, -0x500 },
			{ -0x500, 0, 0 },
			{ 0, -0x500, 0 },
		};

		struct MoveableObjectInitTable
		{
			MoveableObject::InitEntry entries[1];
			int16_t terminator;
		};

		// GLOBAL: TOY2 0x004F46A4
		MoveableObjectInitTable g_moveableObjectInitTable = {
			{ { 8, 11, 0 } },
			-1,
		};

		// GLOBAL: TOY2 0x004F46C0
		int16_t g_tokenLinkIds[5] = { 49, 50, 52, 48, 51 };
		// GLOBAL: TOY2 0x004F46AC
		char* g_rotatingHintSubtitles[5] = {
			"^hamm^ is on the shelf at the end of the x-ray room.",
			"the ^little tike pilot^ has lost his passengers. he is in the repair hangar.",
			"^rocky gilbraltor^ has a ^challenge^ for you. he is on the floor of the conveyor belt room.",
			"you can get to the ^token^ on top of the broken plane using the ^hover boots^. mr. potato head has them near the stack of luggage.",
			"the ^prospector^ boss is on the conveyor belt at the top of the luggage stack. you need the ^hover boots^ to reach him.",
		};

		// GLOBAL: TOY2 0x0052FE38
		int32_t g_slammedPlatformRotation;
		// GLOBAL: TOY2 0x0052FE3C
		int32_t g_slammedPlatformLinkId;
		// GLOBAL: TOY2 0x0052FE40
		int32_t g_hiddenCollectiblesVisible;
		// GLOBAL: TOY2 0x0052FEE8
		int32_t g_prospectorEffectTimer;

		struct HiddenCollectibleState
		{
			int32_t* verticalPosition;
			int32_t savedVerticalPosition;
		};

		struct State
		{
			HiddenCollectibleState hiddenCollectibles[5];
			int32_t prospectorTurnAngle;
			int32_t fanBlend;
			int32_t prospectorState;
			int32_t platform3Rotation;
			int32_t platform4Rotation;
			int32_t fanPhase;
			int32_t prospectorTintToggle;
			int32_t prospectorAttackTimer;
			int32_t oddFanRotation;
			int32_t evenFanRotation;
			int32_t previousProspectorPhase;
			int32_t prospectorTargetAngle;
			int32_t prospectorPhaseTimer;
		};

		// GLOBAL: TOY2 0x0052FEEC
		State g_state;

		STATIC_ASSERT(sizeof(HiddenCollectibleState) == 0x8);
		STATIC_ASSERT(sizeof(State) == 0x5C);
		STATIC_ASSERT(offsetof(State, prospectorTurnAngle) == 0x28);
		STATIC_ASSERT(offsetof(State, prospectorState) == 0x30);
		STATIC_ASSERT(offsetof(State, previousProspectorPhase) == 0x50);
		STATIC_ASSERT(offsetof(State, prospectorPhaseTimer) == 0x58);

		// FUNCTION: TOY2 0x0042C810 [PROVISIONAL]
		void SpawnFanParticle(const Vector3I* position, int32_t fanIndex)
		{
			int32_t pulseIndex = fanIndex == 4 ? FRAME_PULSE_SEVEN_TICK : FRAME_PULSE_SIXTEEN_TICK;
			if (g_framePulseOutputs.bytes[pulseIndex] != 0)
			{
				Nu3D::Particles::ParticleInstance* particle;
				if (fanIndex != 4)
					particle = Nu3D::Particles::SpawnFromPreset(position->x, position->y, position->z, 0x4E, 2);
				else
					particle = Nu3D::Particles::SpawnFromPreset(position->x, position->y, position->z, 0x4F, 2);
				particle->velX = g_fanParticleVelocities[fanIndex].x;
				particle->velY = g_fanParticleVelocities[fanIndex].y;
				particle->velZ = g_fanParticleVelocities[fanIndex].z;
				particle->groundAlignRot = 0xFFF - g_framePulsePhases.thirtyTwoTick * 0x40;
				particle->rotSpeed = -0x100;
			}
		}

		// FUNCTION: TOY2 0x0042C8A0 [MATCHED]
		void InitHiddenCollectibles()
		{
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 56)
					g_state.hiddenCollectibles[0].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 57)
					g_state.hiddenCollectibles[1].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 58)
					g_state.hiddenCollectibles[2].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 59)
					g_state.hiddenCollectibles[3].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 60)
					g_state.hiddenCollectibles[4].verticalPosition = &pickup->position.y;
			}

			for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
			{
				g_state.hiddenCollectibles[hiddenIndex].savedVerticalPosition = *g_state.hiddenCollectibles[hiddenIndex].verticalPosition;
				*g_state.hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
				Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 56, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0042C930 [MATCHED]
		void Init()
		{
			MoveableObject::InitTable(g_moveableObjectInitTable.entries);
			Collectables::Init(g_tokenLinkIds, 0x40);
			Collectables::Activate(3, 1);
			InitHiddenCollectibles();

			g_state.platform4Rotation = 0;
			g_state.platform3Rotation = 0;
			Platform::SetRotationAngles(4, 0, -0x400, 0);
			g_state.oddFanRotation = 0;
			g_state.evenFanRotation = 0;
			g_state.prospectorTargetAngle = -0x200;
			g_state.prospectorTurnAngle = 0x200;
			g_slammedPlatformRotation = 0;
			g_state.prospectorAttackTimer = 200;

			Platform::InitPathPlatform(0, 8, 2, 21, 20, 125, 0, 0x400);
			Platform::InitPathPlatform(1, 10, 4, 25, 24, 125, 0, 0x400);
			Platform::InitPathPlatform(2, 2, 6, 13, 12, 125, 0x8000, -0x400);
			Platform::InitPathPlatform(3, 5, 7, 15, 14, 125, 0, -0xC00);
			Platform::InitPathPlatform(4, 1, 5, 11, 10, 125, 0, -0x638);

			int32_t previousProspectorPhase = Actor::g_creatureActors[32].actorPhase;
			RawLoader::CreatureListRam* prospectorRam = Actor::g_creatureActors[32].creatureRam;
			g_hiddenCollectiblesVisible = 1;
			g_state.prospectorState = 0;
			g_state.prospectorPhaseTimer = 0;
			g_state.prospectorTintToggle = 0;
			g_prospectorEffectTimer = 0;
			g_state.previousProspectorPhase = previousProspectorPhase;
			prospectorRam->boundHalfX = 90;
		}

		// FUNCTION: TOY2 0x0042C3E0 [PROVISIONAL]
		void UpdatePathPlatform(int32_t pathIndex, int32_t speedLimit)
		{
			if ((g_ledgeClimbPlatformIndex == Platform::g_pathPlatforms[pathIndex].platformIndex || pathIndex == 2 || pathIndex == 3)
				&& (g_ledgeClimbPlatformIndex == Platform::g_pathPlatforms[2].platformIndex
					|| g_ledgeClimbPlatformIndex == Platform::g_pathPlatforms[3].platformIndex))
				return;

			Vector4I targetPosition;
			Vector3I platformOrigin;
			Vector3I direction;
			Vector3I pairedPlatformOrigin;
			if (Platform::g_pathPlatforms[pathIndex].pathPosition
				> (Levels::g_recordData[Platform::g_pathPlatforms[pathIndex].pathRecordType]->recordCount - 2) * 0x1000)
			{
				Platform::g_pathPlatforms[pathIndex].pathPosition = 0;
				Path::SamplePoint(Platform::g_pathPlatforms[pathIndex].pathRecordType, 0, &targetPosition);
				Platform::SetOrigin(Platform::g_pathPlatforms[pathIndex].platformIndex, targetPosition.x << 5, targetPosition.y << 5, targetPosition.z << 5);
				Platform::GetOrigin(Platform::g_pathPlatforms[pathIndex].platformIndex, &platformOrigin);
			}
			else
			{
				Path::SamplePoint(Platform::g_pathPlatforms[pathIndex].pathRecordType, Platform::g_pathPlatforms[pathIndex].pathPosition, &targetPosition);
				Platform::GetOrigin(Platform::g_pathPlatforms[pathIndex].platformIndex, &platformOrigin);
				if (Platform::g_pathPlatforms[pathIndex].speed < speedLimit)
					Platform::g_pathPlatforms[pathIndex].speed += Renderer::g_frameDelta * 4;

				if (Platform::g_pathPlatforms[pathIndex].speed > (speedLimit >> 1) && Platform::g_pathPlatforms[pathIndex].previousX == platformOrigin.x
					&& Platform::g_pathPlatforms[pathIndex].previousZ == platformOrigin.z)
				{
					if (pathIndex == 2 || pathIndex == 3)
					{
						Platform::g_pathPlatforms[2].speed = -speedLimit;
						Platform::g_pathPlatforms[3].speed = Platform::g_pathPlatforms[2].speed;
						if (pathIndex == 2)
						{
							Platform::GetOrigin(Platform::g_pathPlatforms[3].platformIndex, &pairedPlatformOrigin);
							if (platformOrigin.x - pairedPlatformOrigin.x > 0)
								pairedPlatformOrigin.x = platformOrigin.x - 0x4D648;
							Platform::SetOrigin(
								Platform::g_pathPlatforms[3].platformIndex, pairedPlatformOrigin.x, pairedPlatformOrigin.y, pairedPlatformOrigin.z);
						}
						else
						{
							Platform::GetOrigin(Platform::g_pathPlatforms[2].platformIndex, &pairedPlatformOrigin);
							if (platformOrigin.x - pairedPlatformOrigin.x > 0)
								pairedPlatformOrigin.x = platformOrigin.x - 0x4D648;
							Platform::SetOrigin(
								Platform::g_pathPlatforms[2].platformIndex, pairedPlatformOrigin.x, pairedPlatformOrigin.y, pairedPlatformOrigin.z);
						}
					}
					else
					{
						Platform::g_pathPlatforms[pathIndex].speed = -speedLimit;
					}
				}

				Platform::g_pathPlatforms[pathIndex].previousX = platformOrigin.x;
				Platform::g_pathPlatforms[pathIndex].previousZ = platformOrigin.z;
				direction.x = targetPosition.x * 0x20 - platformOrigin.x;
				direction.y = targetPosition.y * 0x20 - platformOrigin.y;
				direction.z = targetPosition.z * 0x20 - platformOrigin.z;
				if (abs(direction.x) < 0x4000 && abs(direction.y) < 0x4000 && abs(direction.z) < 0x4000)
					Platform::g_pathPlatforms[pathIndex].pathPosition += 0x1000;

				while (abs(direction.x) > 0x4000 || abs(direction.y) > 0x4000 || abs(direction.z) > 0x4000)
				{
					direction.x >>= 2;
					direction.y >>= 2;
					direction.z >>= 2;
				}

				Nu3D::Math::NormalizeToFixedPoint(&direction, &direction);
				int32_t movementSpeed = Renderer::g_frameDelta * Platform::g_pathPlatforms[pathIndex].speed;
				Platform::SetVelocity(Platform::g_pathPlatforms[pathIndex].platformIndex,
					movementSpeed * direction.x >> 10,
					movementSpeed * direction.y >> 10,
					movementSpeed * direction.z >> 10);
			}

			if (Platform::g_pathPlatforms[pathIndex].secondaryLinkIndex != 0)
			{
				Nu3D::Link::SetPositionRawAndCommit(
					Platform::g_pathPlatforms[pathIndex].secondaryLinkIndex, platformOrigin.x >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);
			}
			Nu3D::Link::SetPositionRawAndCommit(
				Platform::g_pathPlatforms[pathIndex].primaryLinkIndex, platformOrigin.x >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			if (g_groundSlamTimer != 0 && Platform::g_pathPlatforms[pathIndex].facingAngle != 0x400
				&& (Platform::GetFlags(Platform::g_pathPlatforms[pathIndex].platformIndex) & Platform::PLATFORM_FLAG_BUZZ_CONTACT)
					== Platform::PLATFORM_FLAG_BUZZ_CONTACT)
			{
				g_groundSlamTimer = 0;
				AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
				Buzz::Launch(-0xC00, 2);
				g_slammedPlatformRotation = -0x200;
				g_slammedPlatformLinkId = Platform::g_pathPlatforms[pathIndex].primaryLinkIndex;
				Platform::g_pathPlatforms[pathIndex].emitParticles = 0;
			}

			if (Platform::g_pathPlatforms[pathIndex].facingAngle != 0x400 && g_framePulseOutputs.sixtyFourTick != 0
				&& Platform::g_pathPlatforms[pathIndex].emitParticles != 0)
			{
				int32_t particleAngle = ((uint16_t)Platform::g_pathPlatforms[pathIndex].facingAngle + 0x400) & 0xFFF;
				if (Nu3D::Math::IsWithinDistance(&Camera::g_renderCameraTransform.pos, &platformOrigin, 600) != 0)
				{
					Nu3D::Particles::SpawnFromPreset(platformOrigin.x + Numerics::g_sinCosLUT[particleAngle],
						platformOrigin.y - 0x4000,
						platformOrigin.z + Numerics::g_sinCosLUT[(particleAngle + 0x400) & 0xFFF],
						0x73,
						2);
				}
			}
		}

		// FUNCTION: TOY2 0x0042CA60 [PROVISIONAL]
		void Interactions()
		{
			if (Sector::g_currentSectorIndex == 2 || Sector::g_currentSectorIndex == 4 || Sector::g_currentSectorIndex == 5)
			{
				g_state.platform3Rotation = Platform::StepTiltPhysics(3, 4, g_state.platform3Rotation, 2, -0xE0, 0xE0, 0x80);
				Platform::CommitRotationToLink(3, 6);
				g_state.platform4Rotation = Platform::StepTiltPhysics(4, 5, g_state.platform4Rotation, 2, -0xE0, 0xE0, 0x80);
				Platform::CommitRotationToLink(4, 7);
			}

			if (g_zoneRenderData[4].visibilityDepth != 0)
			{
				g_state.fanPhase = (g_state.fanPhase + Renderer::g_frameDelta * 6) & 0xFFF;
				if (g_state.fanPhase > 0x800)
				{
					if (g_state.fanBlend > 0)
						g_state.fanBlend -= 2;
				}
				else if (g_state.fanBlend < 0x80)
				{
					g_state.fanBlend += 2;
				}

				g_state.evenFanRotation -= g_state.fanBlend * Renderer::g_frameDelta * 3;
				g_state.oddFanRotation -= (0x80 - g_state.fanBlend) * Renderer::g_frameDelta * 3;
				Nu3D::Link::SetRotationRelative8bit(0, 0, 0, g_state.evenFanRotation);
				Nu3D::Link::SetRotationRelative8bit(1, 0, 0, g_state.oddFanRotation);
				Nu3D::Link::SetRotationRelative8bit(2, 0, 0, g_state.evenFanRotation);
				Nu3D::Link::SetRotationRelative8bit(3, 0, 0, g_state.oddFanRotation);

				if (Sector::g_currentSectorIndex == 4)
				{
					Levels::RecordData* fanRecords = Levels::g_recordData[1];
					for (int32_t fanRecordIndex = 0; fanRecordIndex < fanRecords->recordCount; fanRecordIndex += 2)
					{
						Vector3I* fanEndpoints = &fanRecords->data[fanRecordIndex];
						g_state.fanBlend = 0x80 - g_state.fanBlend;
						if (g_state.fanBlend > 0x20)
						{
							Vector3I particlePosition = {
								fanEndpoints[0].x << 5,
								fanEndpoints[0].y << 5,
								fanEndpoints[0].z << 5,
							};
							if (g_state.fanBlend > 0x40 || g_state.fanPhase < 0xA00)
								SpawnFanParticle(&particlePosition, 3);

							int32_t fanStartX = fanEndpoints[0].x / 8;
							int32_t buzzOffsetX = fanStartX - g_buzzActor.posAngles.pos.x / 0x100;
							int32_t buzzOffsetY = fanEndpoints[0].y / 8 - g_buzzActor.posAngles.pos.y / 0x100;
							int32_t buzzOffsetZ = fanEndpoints[0].z / 8 - g_buzzActor.posAngles.pos.z / 0x100;
							int32_t fanLength = fanStartX - fanEndpoints[1].x / 8 - 0x20;
							if (buzzOffsetZ * buzzOffsetZ + buzzOffsetY * buzzOffsetY < 0x10000 && buzzOffsetX > 0 && buzzOffsetX < fanLength)
							{
								Collision::ApplySurfaceVelocity(0, g_state.fanBlend * buzzOffsetX * 0x20 / fanLength - g_state.fanBlend * 0x20, 0, 0);
							}
						}
					}
				}
			}

			int32_t platformFlags = Platform::GetFlags(0);
			if (g_groundSlamTimer != 0 && (platformFlags & Platform::PLATFORM_FLAG_BUZZ_CONTACT) == Platform::PLATFORM_FLAG_BUZZ_CONTACT)
			{
				g_groundSlamTimer = 0;
				Buzz::Launch(-0xC00, 2);
				Levels::DeactivateAmbientEmitter(0, 1);
				AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
				g_slammedPlatformRotation = -0x200;
				g_slammedPlatformLinkId = 9;
			}

			platformFlags = Platform::GetFlags(14);
			if (g_groundSlamTimer != 0 && (platformFlags & Platform::PLATFORM_FLAG_BUZZ_CONTACT) == Platform::PLATFORM_FLAG_BUZZ_CONTACT)
			{
				g_groundSlamTimer = 0;
				Buzz::Launch(-0xC00, 2);
				AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
				Levels::DeactivateAmbientEmitter(1, 1);
				g_slammedPlatformRotation = -0x200;
				g_slammedPlatformLinkId = 28;
			}

			Renderer::BlitTextureByIndexOffset(5, 0, 0, 0x40, 0x40, 0, (-g_framePulsePhases.sixtyFourTick - 1) & 0x3F, 0, 0x40);

			if (g_slammedPlatformRotation != 0)
			{
				g_slammedPlatformRotation += Renderer::g_frameDelta * 0x20;
				if (g_slammedPlatformRotation > 0)
					g_slammedPlatformRotation = 0;

				Vector3I linkRotation;
				Nu3D::Link::GetRotation8Bit(g_slammedPlatformLinkId, &linkRotation);
				Nu3D::Link::SetRotationAbsolute8bit(g_slammedPlatformLinkId, 0, linkRotation.y, g_slammedPlatformRotation);
			}

			UpdatePathPlatform(0, 125);
			UpdatePathPlatform(1, 125);
			UpdatePathPlatform(2, 125);
			UpdatePathPlatform(3, 125);
			UpdatePathPlatform(4, 125);

			if (Sector::g_currentSectorIndex != 3 && g_hiddenCollectiblesVisible != 0)
			{
				for (int32_t hiddenLinkIndex = 16; hiddenLinkIndex < 20; hiddenLinkIndex++)
					Nu3D::Link::SetScaleFromFixedOffsets(hiddenLinkIndex, 0, 0, 0);
				g_hiddenCollectiblesVisible = 0;
			}
			if (Sector::g_currentSectorIndex == 3 && g_hiddenCollectiblesVisible == 0)
			{
				for (int32_t hiddenLinkIndex = 16; hiddenLinkIndex < 20; hiddenLinkIndex++)
					Nu3D::Link::SetScaleFromFixedOffsets(hiddenLinkIndex, 0x1000, 0x1000, 0x1000);
				g_hiddenCollectiblesVisible = 1;
			}

			if (g_footingType == 0x20)
				Collision::ApplySurfaceVelocity(0, 0, 0, 0x400);
			if (g_footingType == 0x21)
				Collision::ApplySurfaceVelocity(0, 0x400, 0, 0);
			if (g_footingType == 0x22)
				Collision::ApplySurfaceVelocity(0, -0x200, 0, 0x376);
			if (g_footingType == 0x23)
				Collision::ApplySurfaceVelocity(0, -0x292, 0, -0x310);
			if (g_footingType == 0x24)
				Collision::ApplySurfaceVelocity(0, -0x400, 0, 0);

			Actor::ItemReturnReward(8,
				0x1F,
				"mmm.. mumpf! mmmmu mo mmmme ^hover boots^ mmmm mub ^mouth^ mmmum mi mo mmmump!",
				"wow! thanks buzz! in return for finding my ^mouth^ i will let you use the ^hover boots^. you can float higher by holding down the ^jump^ "
				"button and you can float lower by letting go of the ^jump^ button.",
				"with the ^hover boots^ you can float higher by holding down the ^jump^ button and you can float lower by letting go of the ^jump^ button.",
				-1,
				0xE00);
			Actor::CollectQuestReward(6, 0x1D, -1, 0, 0);
			Actor::RotatingHint(0x21, 0x22, g_rotatingHintSubtitles);

			Actor::Toy2Actor* pilot = &Actor::g_creatureActors[7];
			if ((pilot->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				pilot->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					if (g_levelObjectiveProgress == 5)
					{
						Dialogue::Begin(7, 0x21, "thanks for finding my ^little tike passengers^ buzz! here is a pizza planet ^token^!", -1, 0, 1);
						g_levelObjectiveProgress = -1;
					}
					else
					{
						Dialogue::Begin(7,
							0x21,
							"hi buzz! if you can find ^five little tike passengers^ for my next flight i will give you a pizza planet ^token^.",
							-1,
							0,
							-1);
					}
				}
			}

			Actor::PlayPeriodicHintSound(5, 0xB5);
			Actor::Toy2Actor* challengeActor = &Actor::g_creatureActors[5];
			if ((challengeActor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				challengeActor->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (HUD::g_challengeState == HUD::CHALLENGE_STATE_INACTIVE)
				{
					AudioManager::PlaySoundEffect(0xB6, &challengeActor->pos);
					Dialogue::Begin(
						5, 0x1E, "hi buzz! if you can bring me ^five^ weights before you run out of time, i will give you a pizza planet ^token^.", -1, 0, -1);
					g_specialPickupCount = 0;
					HUD::g_challengeState = HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA;
					for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
					{
						*g_state.hiddenCollectibles[hiddenIndex].verticalPosition = g_state.hiddenCollectibles[hiddenIndex].savedVerticalPosition;
						Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 56, 0x1000, 0x1000, 0x1000);
					}
				}
				else if (g_specialPickupCount < 5)
				{
					Dialogue::Begin(5, 0x1E, "quick! you need to find more weights!", -1, 0, -1);
				}
				else if (HUD::g_challengeState != HUD::CHALLENGE_STATE_COMPLETE)
				{
					AudioManager::PlaySoundEffect(0xB7, &challengeActor->pos);
					Dialogue::Begin(5, 0x1E, "well done! you have found all of my weights! here is your pizza planet ^token^!", -1, 0, 2);
					HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
				}
			}

			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
				AndysHouse::g_raceCheckpointPassCount = 160;
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
			{
				if (g_framePulseOutputs.sixtyFourTick != 0)
					AndysHouse::g_raceCheckpointPassCount--;
				if (AndysHouse::g_raceCheckpointPassCount < 100)
				{
					AndysHouse::g_raceCheckpointPassCount = 100;
					HUD::g_challengeState = HUD::CHALLENGE_STATE_INACTIVE;
					for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
					{
						*g_state.hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
						Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 56, 0, 0, 0);
					}
				}
			}

			Actor::Toy2Actor* prospector = &Actor::g_creatureActors[32];
			if (g_state.prospectorState == PROSPECTOR_STATE_IDLE && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &prospector->pos, 300) != 0
				&& g_buzzActor.posAngles.pos.y < -0x3660E && g_buzzActor.collisionFlags != 0)
			{
				g_state.prospectorState = PROSPECTOR_STATE_DIALOGUE;
				Dialogue::Begin(32, 0x20, "ha ha ha ha ... defeat the ^prospector^ boss to get a pizza planet ^token^!", -1, 0, -1);
			}

			if (g_state.prospectorState == PROSPECTOR_STATE_DIALOGUE && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				prospector->movementData = CreatureBehaviour::g_prospectorMovementData + 14;
				g_levelInteractionTimer = 180;
				g_state.prospectorState = PROSPECTOR_STATE_ACTIVE;
				prospector->movementCommandTimer = 0;
				prospector->creatureRam->initialFacingAngle = 0;
			}
			if (g_state.prospectorState > PROSPECTOR_STATE_ACTIVE)
			{
				if (g_state.prospectorState < PROSPECTOR_STATE_TOKEN_DELAY_END)
					g_state.prospectorState += Renderer::g_frameDelta;
				else if (g_state.prospectorState != PROSPECTOR_STATE_TOKEN_AWARDED)
				{
					Collectables::Activate(4, 0);
					g_state.prospectorState = PROSPECTOR_STATE_TOKEN_AWARDED;
				}
			}

			Levels::RecordData* flareRecords = Levels::g_recordData[10];
			int32_t nearestDistanceSquared = INT_MAX;
			int32_t nearestFlareIndex = 0;
			for (int32_t flareIndex = 0; flareIndex < flareRecords->recordCount; flareIndex++)
			{
				Vector3I* flare = &flareRecords->data[flareIndex];
				int32_t cameraOffsetY = (Camera::g_renderCameraTransform.pos.y - flare->y * 0x20) >> 8;
				int32_t cameraOffsetZ = (Camera::g_renderCameraTransform.pos.z - flare->z * 0x20) >> 8;
				int32_t cameraOffsetX = (Camera::g_renderCameraTransform.pos.x - flare->x * 0x20) >> 8;
				if (cameraOffsetX * cameraOffsetX + cameraOffsetY * cameraOffsetY + cameraOffsetZ * cameraOffsetZ < 1000000)
				{
					Renderer::LensFlare::RegisterLight(flare->x * 0x20, flare->y * 0x20, flare->z * 0x20, 0x30, 0x40, 0x50, 0x40);
					int32_t buzzOffsetZ = (g_buzzActor.posAngles.pos.z - flare->z * 0x20) >> 8;
					int32_t buzzOffsetY = (g_buzzActor.posAngles.pos.y - flare->y * 0x20 - 0x2000) >> 8;
					int32_t buzzOffsetX = (g_buzzActor.posAngles.pos.x - flare->x * 0x20) >> 8;
					int32_t distanceSquared = buzzOffsetX * buzzOffsetX + buzzOffsetZ * buzzOffsetZ + buzzOffsetY * buzzOffsetY;
					if (distanceSquared < nearestDistanceSquared)
					{
						nearestDistanceSquared = distanceSquared;
						nearestFlareIndex = flareIndex;
					}
				}
			}

			if (nearestDistanceSquared < 0x10000)
			{
				Vector3I* nearestFlare = &flareRecords->data[nearestFlareIndex];
				Lighting::DynamicLight* light = &Lighting::g_lightingState.dynamicLights[1];
				light->sourceId = reinterpret_cast<int32_t>(nearestFlare);
				light->position.x = nearestFlare->x << 5;
				light->position.y = nearestFlare->y << 5;
				light->colour.r = 0x6F;
				light->position.z = nearestFlare->z << 5;
				light->colour.g = 0x7F;
				light->colour.b = 0x8F;
				light->lifetime = 1;
			}
			else
			{
				Lighting::g_lightingState.dynamicLights[1].lifetime = 0;
			}

			PlayLevelMusic();
		}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0042BE60 [PROVISIONAL]
		void ProsPLevel13(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AirportInfiltration::g_state.prospectorTintToggle = (AirportInfiltration::g_state.prospectorTintToggle - 1) & 1;

			if (actor->actorPhase != AirportInfiltration::g_state.previousProspectorPhase)
			{
				AirportInfiltration::g_state.previousProspectorPhase = actor->actorPhase;
				AirportInfiltration::g_state.prospectorPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;

				int32_t soundIndex = *g_randDatBufferPtr++ & 3;
				if (soundIndex == 3)
					soundIndex = 0;
				AudioManager::Preset::PlayOneShotSound2(soundIndex + 0xC4, actor);
			}

			if (AirportInfiltration::g_state.prospectorState == AirportInfiltration::PROSPECTOR_STATE_ACTIVE)
			{
				AirportInfiltration::g_state.prospectorAttackTimer -= Renderer::g_frameDelta;
				if (AirportInfiltration::g_state.prospectorAttackTimer < 0)
				{
					AirportInfiltration::g_state.prospectorAttackTimer = *g_randDatBufferPtr++ * 2 + 400;
					AudioManager::Preset::PlayOneShotSound2(0xC3, actor);
				}

				AirportInfiltration::g_state.prospectorPhaseTimer -= Renderer::g_frameDelta;
				if (AirportInfiltration::g_state.prospectorPhaseTimer < 0)
				{
					AirportInfiltration::g_state.prospectorPhaseTimer = 0;
					actor->creatureRam->defenseMode = 6;
				}
				else if (AirportInfiltration::g_state.prospectorTintToggle != 0)
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

			if ((context->targetFlags & 1) != 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 200) != 0
				&& actor->primaryAnimIdx == 4)
			{
				actor->reservedArea[1] = 0xD0;
				actor->reservedArea[2] = 0xD0;
				Actor::SetAnimation(actor, 3, 9);
				actor->creatureRam->speedTarget = 0;
				AirportInfiltration::g_prospectorEffectTimer = 0x2C;
				if (AudioManager::IsActorSoundPlaying(actor) == 0)
					AudioManager::Preset::PlayOneShotSound2(0xC2, actor);
			}

			if (actor->primaryAnimIdx == 3 && (actor->animationFramePosition & (int32_t)0xFFFF0000) > 0x150000)
			{
				actor->movementCommandTimer = 0;
				actor->movementData = g_prospectorMovementData + 16;
				actor->creatureRam->speedTarget = 0x10;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TRACKS_TARGET | Actor::ACTOR_FLAG_TARGETS_BUZZ);
			}

			if (AirportInfiltration::g_prospectorEffectTimer != 0)
			{
				AirportInfiltration::g_prospectorEffectTimer -= Renderer::g_frameDelta;
				if (AirportInfiltration::g_prospectorEffectTimer <= 0)
				{
					int32_t yawAngle = actor->yawAngle;
					Nu3D::Particles::SpawnInstance(actor->pos.x + (Numerics::g_sinCosLUT[(yawAngle + 0x400) & 0xFFF] >> 3),
						actor->pos.y - 0x800,
						actor->pos.z + (Numerics::g_sinCosLUT[(yawAngle - 0x800) & 0xFFF] >> 3),
						Numerics::g_sinCosLUT[yawAngle] >> 2,
						0,
						Numerics::g_sinCosLUT[(yawAngle + 0x400) & 0xFFF] >> 2,
						0,
						(0x7FF - yawAngle) & 0xFFF,
						0,
						0x68);
					AudioManager::PlaySoundEffect(0xA6, &actor->pos);
					AirportInfiltration::g_prospectorEffectTimer = 0;
				}
			}

			if (AirportInfiltration::g_state.prospectorState == AirportInfiltration::PROSPECTOR_STATE_ACTIVE)
			{
				HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 54 / 20;
			}

			if (actor->actorPhase < 10 && AirportInfiltration::g_state.prospectorState == AirportInfiltration::PROSPECTOR_STATE_ACTIVE)
			{
				actor->movementData = g_prospectorMovementData + 45;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TARGETS_BUZZ | Actor::ACTOR_FLAG_DAMAGES_BUZZ);
				actor->movementCommandTimer = 0;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AirportInfiltration::g_state.prospectorState = AirportInfiltration::PROSPECTOR_STATE_DEFEATED;
				g_hudActorAnimationFrame = 0;
				AirportInfiltration::g_prospectorEffectTimer = 0;
				actor->creatureRam->speedTarget = 0;
			}
		}

		// FUNCTION: TOY2 0x0042C150 [MATCHED]
		void Pilot(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0 && actor->actorPhase == 0x66)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x5A;
				AudioManager::PlaySoundEffect(0x6B, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x6C, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}

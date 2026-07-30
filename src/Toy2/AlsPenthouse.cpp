#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "Toy2/Weather.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <limits.h>

extern "C" double __cdecl sqrt(double);

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;
	extern uint8_t g_environmentTintRed;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintBlue;

	namespace AlsPenthouse
	{
		struct ObjectGroup
		{
			int16_t mask;
			int16_t primaryLinkId;
			int16_t alternateLinkId;
			int16_t rotationLinkId;
			int16_t rotationX;
			int16_t rotationY;
			int16_t rotationZ;
		};
		enum ObjectGroupPrimaryCursor
		{
			PRIMARY_CURSOR_MASK = -1,
			PRIMARY_CURSOR_LINK = 0,
			PRIMARY_CURSOR_ALTERNATE_LINK = 1,
		};
		enum ObjectGroupAlternateCursor
		{
			ALTERNATE_CURSOR_MASK = -2,
			ALTERNATE_CURSOR_PRIMARY_LINK = -1,
			ALTERNATE_CURSOR_LINK = 0,
		};

		enum GunslingerEncounterState
		{
			GUNSLINGER_ENCOUNTER_ACTIVE = 2,
			GUNSLINGER_ENCOUNTER_DEFEATED = 3,
			GUNSLINGER_ENCOUNTER_COMPLETE = 200,
		};

		struct HiddenCollectibleState
		{
			int32_t* verticalPosition;
			int32_t savedVerticalPosition;
		};
		struct RaisedPlatformLink
		{
			uint8_t platformId;
			uint8_t linkId;
		};
		struct MoveableObjectInitTable
		{
			MoveableObject::InitEntry entries[3];
			int16_t terminator;
		};
		enum GroundSlamTargetLinkOffset
		{
			GROUND_SLAM_SOURCE_LINK = 0,
			GROUND_SLAM_HIDDEN_LINK = 1,
			GROUND_SLAM_REPLACEMENT_LINK = 2,
			GROUND_SLAM_SECOND_REPLACEMENT_LINK = 3,
		};

		// GLOBAL: TOY2 0x0052FD10
		int32_t g_gunslingerPhaseTimer;
		// GLOBAL: TOY2 0x0052FD2C
		int32_t g_previousGunslingerPhase;
		// GLOBAL: TOY2 0x0052FD40
		int32_t g_gunslingerEncounterState;
		// GLOBAL: TOY2 0x0052FD70
		int32_t g_gunslingerTintToggle;
		// GLOBAL: TOY2 0x0052FD08
		int32_t g_objectGroupFlashPhase;
		// GLOBAL: TOY2 0x0052FD14
		int32_t g_objectGroupMask;
		// GLOBAL: TOY2 0x0052FD3C
		uint32_t g_activeLinkMask;
		// GLOBAL: TOY2 0x004F3C54
		char g_waterButtonInstructions[] = {
#include "Toy2/AlsPenthouseWaterButtonInstructions.inc"
		};
		// GLOBAL: TOY2 0x004F3D04
		char g_trainSwitchInstructions[] = {
#include "Toy2/AlsPenthouseTrainSwitchInstructions.inc"
		};
		// GLOBAL: TOY2 0x004F3E60
		extern const MoveableObjectInitTable g_moveableObjectInitTable = {
			{
				{ 30, 19, 13 },
				{ 29, 20, 0 },
				{ 81, 21, 14 },
			},
			-1,
		};
		// GLOBAL: TOY2 0x004F3E74
		int32_t g_groundSlamTargetLinkIds[24] = {
			50,
			59,
			66,
			73,
			51,
			60,
			67,
			74,
			58,
			65,
			72,
			79,
			52,
			61,
			68,
			75,
			53,
			62,
			69,
			76,
			54,
			63,
			70,
			77,
		};
		// GLOBAL: TOY2 0x004F3F18
		int16_t g_tokenLinkIds[6] = { 96, 98, 99, 100, 97, 0 };
		// GLOBAL: TOY2 0x004F3F24
		extern const Collectables::TokenDialogueValue g_tokenDialogueValues[] = {
			{ 120 },
			{ 22 },
			{ reinterpret_cast<int32_t>(g_waterButtonInstructions) },
			{ 0xC00 },
			{ 119 },
			{ 21 },
			{ reinterpret_cast<int32_t>(g_trainSwitchInstructions) },
			{ 0xC00 },
			{ -1 },
		};
		// GLOBAL: TOY2 0x004F3F48
		ObjectGroup g_objectGroups[7] = {
			{ 1, 36, 17, 84, -350, 0, 0 },
			{ 2, 37, 18, 84, 0, 0, 0 },
			{ 4, 31, 12, 86, 175, 0, 0 },
			{ 8, 32, 13, 86, 350, 0, 0 },
			{ 16, 33, 14, 86, 0, 0, 0 },
			{ 32, 34, 15, 85, 0, 0, -350 },
			{ 64, 35, 16, 85, 0, 0, 0 },
		};
		// GLOBAL: TOY2 0x004F3FAC
		RaisedPlatformLink g_raisedPlatformLinks[4] = {
			{ 7, 1 },
			{ 8, 2 },
			{ 6, 3 },
			{ 9, 4 },
		};
		// GLOBAL: TOY2 0x004F4024
		uint8_t g_initialHiddenLinkIds[33] = {
			0x2C,
			0x2B,
			0x14,
			0x15,
			0x16,
			0x17,
			0x5B,
			0x0C,
			0x0D,
			0x0E,
			0x0F,
			0x10,
			0x11,
			0x12,
			0x1F,
			0x20,
			0x21,
			0x22,
			0x23,
			0x24,
			0x25,
			0x42,
			0x43,
			0x44,
			0x45,
			0x46,
			0x48,
			0x49,
			0x4A,
			0x4B,
			0x4C,
			0x4D,
			0x4F,
		};
		// GLOBAL: TOY2 0x0052FD58
		int32_t g_groundSlamTargetTimers[6];
		// GLOBAL: TOY2 0x0052FDFC
		int32_t g_waterLevel;
		// GLOBAL: TOY2 0x0052FD30
		int32_t g_targetWaterLevel;
		// GLOBAL: TOY2 0x0052FD78
		int32_t g_waterLevelPhase;
		// GLOBAL: TOY2 0x0052FDB8
		int32_t g_objectReplacementPhase;
		// GLOBAL: TOY2 0x0052FDAC
		int32_t g_drainLinkScalePhase;
		// GLOBAL: TOY2 0x0052FD7C
		int32_t g_platform14VerticalVelocity;
		// GLOBAL: TOY2 0x0052FD1C
		int32_t g_platform14VerticalOffset;
		// GLOBAL: TOY2 0x0052FD90
		int32_t g_platform13VerticalVelocity;
		// GLOBAL: TOY2 0x0052FD28
		int32_t g_platform13VerticalOffset;
		// GLOBAL: TOY2 0x0052FD50
		int32_t g_platform16VerticalVelocity;
		// GLOBAL: TOY2 0x0052FD18
		int32_t g_platform16VerticalOffset;
		// GLOBAL: TOY2 0x0052FD88
		int32_t g_platform15VerticalVelocity;
		// GLOBAL: TOY2 0x0052FD20
		int32_t g_platform15VerticalOffset;
		// GLOBAL: TOY2 0x0052FD0C
		int32_t g_trainRotationAngle;
		// GLOBAL: TOY2 0x0052FD4C
		int32_t g_trainPathRecordType;
		// GLOBAL: TOY2 0x0052FD84
		int32_t g_trainPathPointIndex;
		// GLOBAL: TOY2 0x0052FDBC
		int32_t g_trainPathDirection;
		// GLOBAL: TOY2 0x0052FD98
		Vector3I g_trainPosition;
		// GLOBAL: TOY2 0x0052FD74
		int32_t g_linkReplacementTimer;
		// GLOBAL: TOY2 0x0052FD44
		int32_t g_replacedLinkId;
		// GLOBAL: TOY2 0x0052FD24
		int32_t g_trainCollisionTimer;
		// GLOBAL: TOY2 0x0052FDF0
		int32_t g_trainSpeed;
		// GLOBAL: TOY2 0x0052FDB4
		int32_t g_trainSoundTimer;
		// GLOBAL: TOY2 0x0052FDF8
		int32_t g_groundSlamTargetFlashPhase;
		// GLOBAL: TOY2 0x0052FD48
		uint32_t g_groundSlamTargetMask;
		// GLOBAL: TOY2 0x0052FD34
		int32_t g_gunslingerActorIndex;
		// GLOBAL: TOY2 0x0052FD8C
		int32_t g_cannonFireTimer;
		// GLOBAL: TOY2 0x0052FD80
		int32_t g_sector2ParticleTimer;
		// GLOBAL: TOY2 0x0052FDF4
		int32_t g_sector2ParticleVariant;
		// GLOBAL: TOY2 0x0052FDB0
		int32_t g_sector4ParticleTimer;
		// GLOBAL: TOY2 0x0052FDC4
		int32_t g_sector4ParticlePositionIndex;
		// GLOBAL: TOY2 0x0052FD38
		int32_t g_platform16RotationPhase;
		// GLOBAL: TOY2 0x0052FD54
		int32_t g_platform17RotationPhase;
		// GLOBAL: TOY2 0x0052FDA8
		int32_t g_sector5IconPhase;
		// GLOBAL: TOY2 0x0052FDC0
		int32_t g_platformCollisionState;
		// GLOBAL: TOY2 0x0052FDC8
		HiddenCollectibleState g_hiddenCollectibles[5];

		STATIC_ASSERT(sizeof(HiddenCollectibleState) == 0x8);

		// FUNCTION: TOY2 0x00429CE0 [TOOL]
		void InitHiddenCollectibles()
		{
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 101)
					g_hiddenCollectibles[0].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 102)
					g_hiddenCollectibles[1].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 103)
					g_hiddenCollectibles[2].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 104)
					g_hiddenCollectibles[3].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 105)
					g_hiddenCollectibles[4].verticalPosition = &pickup->position.y;
			}

			for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
			{
				g_hiddenCollectibles[hiddenIndex].savedVerticalPosition = *g_hiddenCollectibles[hiddenIndex].verticalPosition;
				*g_hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
				Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 101, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x00428BA0 [MATCHED]
		void UpdateObjectGroupFlash()
		{
			int32_t phase = Renderer::g_frameDelta;
			phase += g_objectGroupFlashPhase;
			g_objectGroupFlashPhase = phase & 0x1F;

			if (g_objectGroupFlashPhase < 24 && (g_objectGroupMask & 0x800) == 0)
			{
				g_objectGroupMask |= 0x800;
				int16_t* group = &g_objectGroups[0].primaryLinkId;
				do
				{
					if ((g_objectGroupMask & group[PRIMARY_CURSOR_MASK]) != 0)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(group[PRIMARY_CURSOR_LINK], 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(group[PRIMARY_CURSOR_ALTERNATE_LINK], 0, 0, 0);
					}
					group += sizeof(ObjectGroup) / sizeof(int16_t);
				} while ((int32_t)group < (int32_t)&g_objectGroups[7].primaryLinkId);
				return;
			}

			if (g_objectGroupFlashPhase > 24 && (g_objectGroupMask & 0x800) != 0)
			{
				g_objectGroupMask &= ~0x800;
				int16_t* group = &g_objectGroups[0].alternateLinkId;
				do
				{
					if ((g_objectGroupMask & group[ALTERNATE_CURSOR_MASK]) != 0)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(group[ALTERNATE_CURSOR_LINK], 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(group[ALTERNATE_CURSOR_PRIMARY_LINK], 0, 0, 0);
					}
					group += sizeof(ObjectGroup) / sizeof(int16_t);
				} while ((int32_t)group < (int32_t)&g_objectGroups[7].alternateLinkId);
			}
		}

		// FUNCTION: TOY2 0x004292C0 [PROVISIONAL]
		void UpdateRaisedPlatforms(uint32_t requestedMask)
		{
			Vector3I origin;
			uint32_t platformMask = 0x10;
			RaisedPlatformLink* platformLink = g_raisedPlatformLinks;
			for (int32_t platformIndex = 0; platformIndex < 4; platformIndex++, platformLink++, platformMask *= 2)
			{
				if ((g_activeLinkMask & platformMask) == 0 && (requestedMask & platformMask) != 0)
				{
					Platform::GetOrigin(platformLink->platformId, &origin);
					origin.y += 0xE10;
					Platform::SetOrigin(platformLink->platformId, origin.x, origin.y, origin.z);
					Nu3D::Link::SetScaleFromFixedOffsets(platformLink->linkId, 0x1000, 0x960, 0x1000);
					g_activeLinkMask |= platformMask;
				}

				if ((g_activeLinkMask & platformMask) != 0 && (requestedMask & platformMask) == 0)
				{
					Platform::GetOrigin(platformLink->platformId, &origin);
					origin.y -= 0xE10;
					Platform::SetOrigin(platformLink->platformId, origin.x, origin.y, origin.z);
					Nu3D::Link::SetScaleFromFixedOffsets(platformLink->linkId, 0x1000, 0xFFE, 0x1000);
					g_activeLinkMask &= -1 - platformMask;
				}
			}
		}

		// FUNCTION: TOY2 0x00429800 [PROVISIONAL]
		void CompleteGroundSlamTarget(int32_t timerOffset)
		{
			int32_t secondReplacementLinkId = g_groundSlamTargetLinkIds[timerOffset + GROUND_SLAM_SECOND_REPLACEMENT_LINK];
			int32_t hiddenLinkId = g_groundSlamTargetLinkIds[timerOffset + GROUND_SLAM_HIDDEN_LINK];
			int32_t sourceLinkId = g_groundSlamTargetLinkIds[timerOffset + GROUND_SLAM_SOURCE_LINK];
			int32_t replacementLinkId = g_groundSlamTargetLinkIds[timerOffset + GROUND_SLAM_REPLACEMENT_LINK];
			Vector3I position;
			Nu3D::Link::GetRotation8Bit(sourceLinkId, &position);
			Nu3D::Link::SetRotationRelative8bit(replacementLinkId, 0, position.y, 0);
			Nu3D::Link::SetRotationRelative8bit(secondReplacementLinkId, 0, position.y, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(replacementLinkId, 0x1000, 0x1000, 0x1000);
			Nu3D::Link::SetScaleFromFixedOffsets(secondReplacementLinkId, 0x1000, 0x1000, 0x1000);
			Nu3D::Link::SetScaleFromFixedOffsets(sourceLinkId, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(hiddenLinkId, 0, 0, 0);

			Nu3D::Link::GetCurrentPosFixed(sourceLinkId, &position);
			for (int32_t particleIndex = 0; particleIndex < 4; particleIndex++)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(position.x, position.y - 0x3000, position.z, 0x23, 0xE);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
			}
			AudioManager::PlaySoundEffect(-2, &position);
		}

		// FUNCTION: TOY2 0x00428700 [MATCHED]
		void FireCannonProjectile(const Vector3I* cannonPosition, int32_t yawAngle)
		{
			if (g_cannonFireTimer <= 1000)
				return;

			int32_t cannonX = cannonPosition->x;
			int32_t cannonY = cannonPosition->y - 0x4000;
			int32_t cannonZ = cannonPosition->z;
			int32_t deltaX = (cannonX + ((*g_randDatBufferPtr++ - 0x80) << 7) - g_buzzActor.posAngles.pos.x) >> 5;
			int32_t deltaZ = (cannonZ + ((*g_randDatBufferPtr++ - 0x80) << 7) - g_buzzActor.posAngles.pos.z) >> 5;
			int32_t horizontalDistance = (int32_t)sqrt((float)(deltaX * deltaX + deltaZ * deltaZ));
			if (horizontalDistance < 2000)
				return;

			int32_t horizontalSpeed = horizontalDistance * 2 / 3;
			int32_t travelTime = (horizontalDistance << 12) / horizontalSpeed;
			int32_t velocityX = (Numerics::g_sinCosLUT[(yawAngle + 0x400) & 0xFFF] * horizontalSpeed) / 0x4000;
			int32_t velocityY = ((g_buzzActor.posAngles.pos.y - cannonY) << 7) / travelTime - (travelTime << 7) / 0x100;
			int32_t velocityZ = (Numerics::g_sinCosLUT[(yawAngle - 0x800) & 0xFFF] * horizontalSpeed) / 0x4000;

			Nu3D::Particles::SpawnInstance(cannonX, cannonY, cannonZ, velocityX, velocityY, velocityZ, 0x80, 0, 0, 0x5C);
			AudioManager::PlaySoundEffect(0x92, cannonPosition);
		}

		// STUB: TOY2 0x00428890
		void Method2(uint32_t requestedMask) {}

		// STUB: TOY2 0x00428E70
		void Method8() {}

		// FUNCTION: TOY2 0x00429D70 [MATCHED]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x71);
			Collectables::LoadTokenTable(g_tokenDialogueValues);
			Collectables::Activate(3, 1);
			MoveableObject::InitTable(g_moveableObjectInitTable.entries);
			InitHiddenCollectibles();
			Levels::DeactivateAmbientEmitter(3, 1);

			g_groundSlamTargetTimers[0] = 0;
			g_groundSlamTargetTimers[1] = 0;
			g_groundSlamTargetTimers[2] = 0;
			g_groundSlamTargetTimers[3] = 0;
			g_groundSlamTargetTimers[4] = 0;
			g_groundSlamTargetTimers[5] = 0;

			g_environmentSurfaceY = 0;
			g_environmentEffectType = 1;
			g_previousBuzzEnvironmentY = 0;
			g_environmentTintBlue = 0x68;
			g_environmentTintGreen = 0x68;
			g_environmentTintRed = 0x80;
			HUD::g_challengeState = 0;
			AndysHouse::g_raceCheckpointPassCount = 0;
			g_waterLevel = 0;
			g_targetWaterLevel = 0;
			g_activeLinkMask = 0;
			g_waterLevelPhase = 0;
			g_objectReplacementPhase = 0;

			for (int32_t linkIndex = 0; linkIndex < 33; linkIndex++)
				Nu3D::Link::SetScaleFromFixedOffsets(g_initialHiddenLinkIds[linkIndex], 0, 0, 0);

			UpdateRaisedPlatforms(0x10);
			g_drainLinkScalePhase = 0;
			g_platform14VerticalVelocity = 0;
			g_platform14VerticalOffset = 0;
			g_platform13VerticalVelocity = 0;
			g_platform13VerticalOffset = 0;
			g_platform16VerticalVelocity = 0;
			g_platform16VerticalOffset = 0;
			g_platform15VerticalVelocity = 0;
			g_platform15VerticalOffset = 0;
			g_objectGroupMask = 0;
			g_trainRotationAngle = 0;
			Method2(0x25);
			g_objectGroupFlashPhase = 0;
			g_trainPathRecordType = 1;
			g_trainPathPointIndex = 6;
			g_trainPathDirection = 0;
			g_trainPosition.x = Levels::g_recordData[1]->data[6].x << 5;
			g_trainPosition.y = Levels::g_recordData[1]->data[6].y << 5;
			g_trainPosition.z = Levels::g_recordData[1]->data[6].z << 5;
			Method8();

			g_linkReplacementTimer = 0;
			g_replacedLinkId = 8;
			Nu3D::Link::SetScaleFromFixedOffsets(0x31, 0, 0, 0);
			g_trainCollisionTimer = 0;
			g_trainSpeed = 0x100;
			g_trainSoundTimer = 0x100;
			g_groundSlamTargetFlashPhase = 0;
			g_groundSlamTargetMask = 0;
			g_gunslingerActorIndex = 11;
			Nu3D::Link::SetScaleFromFixedOffsets(0x30, 0, 0, 0);

			int32_t previousGunslingerPhase = Actor::g_creatureActors[11].actorPhase;
			int32_t gunslingerZ = Actor::g_creatureActors[11].pos.z + 0x10000;
			g_cannonFireTimer = 0;
			g_sector2ParticleTimer = 0;
			g_sector2ParticleVariant = 0;
			g_sector4ParticleTimer = 0;
			g_sector4ParticlePositionIndex = 0;
			g_gunslingerEncounterState = 0;
			g_gunslingerTintToggle = 0;
			g_gunslingerPhaseTimer = 0;
			g_previousGunslingerPhase = previousGunslingerPhase;
			Actor::g_creatureActors[11].pos.z = gunslingerZ;
			g_platform16RotationPhase = 0;
			g_platform17RotationPhase = 0;
			g_sector5IconPhase = 0;
			g_platformCollisionState = 0;
			Platform::DisableCollision(0x1A);
			Platform::DisableCollision(0x1B);
		}

		// FUNCTION: TOY2 0x00429FB0 [PROVISIONAL]
		void UpdateFloatingPlatform(int32_t platformId, int32_t linkId, int32_t waterSurfaceY, int32_t* verticalVelocity, int32_t* verticalOffset)
		{
			Vector3I platformOrigin;
			Platform::GetOrigin(platformId, &platformOrigin);

			int32_t waterOffset;
			if (waterSurfaceY < g_waterLevel)
				waterOffset = 0;
			else
				waterOffset = g_waterLevel - waterSurfaceY;

			if (g_groundSlamTimer == -40 && (Platform::GetFlags(platformId) & 3) == 2)
				*verticalVelocity = 0x500;

			if (*verticalOffset < 0)
				*verticalVelocity += Renderer::g_frameDelta * 0x30;
			else
				*verticalVelocity -= Renderer::g_frameDelta * 0x30;

			int32_t previousOffset = *verticalOffset;
			*verticalOffset += *verticalVelocity * Renderer::g_frameDelta;
			if (*verticalOffset >= 0 && previousOffset < 0)
			{
				if (*verticalVelocity > 0x200)
					*verticalVelocity >>= 1;
				else
					*verticalVelocity = 0x200;

				if (waterOffset != 0)
					Nu3D::Particles::SpawnFromPreset(platformOrigin.x, platformOrigin.y - *verticalOffset, platformOrigin.z, 0x39, 2);
			}

			if (*verticalOffset > 0x2000)
				*verticalVelocity = 0;
			if (*verticalOffset < -0x800)
				*verticalVelocity = 0;

			Vector3I targetPosition;
			Nu3D::Link::GetTargetPosFixed(linkId, &targetPosition);
			Nu3D::Link::SetPositionRawAndCommit(linkId, platformOrigin.x >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);

			if (waterOffset > -0x1000)
				targetPosition.y += waterOffset - (*verticalOffset * waterOffset) / 0x1000;
			else
				targetPosition.y += waterOffset + *verticalOffset;

			targetPosition.y -= platformOrigin.y;
			if (targetPosition.y < -0x800)
				targetPosition.y = -0x800;
			else if (targetPosition.y > 0x800)
				targetPosition.y = 0x800;

			Platform::SetVelocity(platformId, 0, targetPosition.y, 0);
		}

		// STUB: TOY2 0x0042A130
		void Interactions() {}

		STATIC_ASSERT(sizeof(ObjectGroup) == 0xE);
		STATIC_ASSERT(sizeof(RaisedPlatformLink) == 0x2);
		STATIC_ASSERT(sizeof(MoveableObjectInitTable) == 0x14);
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x004282D0 [MATCHED]
		void GunsLLevel11(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			AlsPenthouse::g_gunslingerTintToggle = (AlsPenthouse::g_gunslingerTintToggle - 1) & 1;

			if (actor->actorPhase != AlsPenthouse::g_previousGunslingerPhase)
			{
				AlsPenthouse::g_previousGunslingerPhase = actor->actorPhase;
				AlsPenthouse::g_gunslingerPhaseTimer = 60;
				actor->creatureRam->defenseMode = 4;
				AudioManager::PlaySoundEffect(0xA5, &actor->pos);
			}

			if (AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				AlsPenthouse::g_gunslingerPhaseTimer -= Renderer::g_frameDelta;
				if (AlsPenthouse::g_gunslingerPhaseTimer < 0)
				{
					AlsPenthouse::g_gunslingerPhaseTimer = 0;
					actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (AlsPenthouse::g_gunslingerTintToggle != 0)
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

			if (g_buzzActor.posAngles.pos.y < 0x1BCA1 || g_buzzActor.posAngles.pos.y > 0x22405)
			{
				actor->motionTargetPos.x = actor->boundary.x;
				actor->motionTargetPos.z = actor->boundary.z;
				actor->creatureRam->defenseMode = 4;
			}
			else if (AlsPenthouse::g_gunslingerPhaseTimer == 0)
			{
				actor->creatureRam->defenseMode = 7;
			}

			if (actor->previousActorPhase != 0)
			{
				int32_t fireProjectile = 0;
				Vector4I projectilePosition;
				if (actor->previousActorPhase > 20)
				{
					fireProjectile = 1;
					projectilePosition.x = 0;
					projectilePosition.y = 0;
					projectilePosition.z = 0;
					Actor::ResolveBoneAttachmentPos(&projectilePosition, actor, 15);
					actor->previousActorPhase = 10;
					AudioManager::PlaySoundEffect(0x93, &actor->pos);
				}

				actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
				if (actor->previousActorPhase <= 0)
				{
					projectilePosition.x = 0;
					projectilePosition.y = 0;
					projectilePosition.z = 0;
					Actor::ResolveBoneAttachmentPos(&projectilePosition, actor, 16);
					actor->previousActorPhase = 0;
					AudioManager::PlaySoundEffect(0x94, &actor->pos);
					fireProjectile = 1;
				}

				if (fireProjectile != 0)
				{
					int32_t projectileAngle =
						Nu3D::Math::CartesianToFixedAngle(
							g_buzzActor.posAngles.pos.x - projectilePosition.x, g_buzzActor.posAngles.pos.z - projectilePosition.z)
						& 0xFFF;
					if (((projectileAngle - actor->yawAngle + 0x100) & 0xFFF) > 0x200)
						projectileAngle = actor->yawAngle;

					Nu3D::Particles::SpawnInstance(projectilePosition.x,
						projectilePosition.y,
						projectilePosition.z,
						Numerics::g_sinCosLUT[projectileAngle] >> 2,
						0x200,
						Numerics::g_sinCosLUT[(projectileAngle + 0x400) & 0xFFF] >> 2,
						0,
						0,
						0,
						0x61);

					if ((actor->actorFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0)
					{
						for (int32_t particleCount = 5; particleCount != 0; particleCount--)
						{
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(projectilePosition.x, projectilePosition.y, projectilePosition.z, 100, 15);
							particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
						}
					}
				}
			}

			if (AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				g_hudActorAnimationFrame = (actor->actorPhase - 9) * 54 / 20;
				if (Sector::g_activeSectorIndex == 2 && g_buzzActor.posAngles.pos.y < 0x22405)
					HUD::g_slideTimers[HUD::SLIDE_BOSS_STATUS] = 90;
			}

			if (actor->actorPhase < 10 && AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				actor->movementData = g_gunslingerMovementData + 52;
				actor->creatureRam->defenseMode = 4;
				actor->actorFlags &= ~Actor::ACTOR_FLAG_DAMAGES_BUZZ;
				AudioManager::PlaySoundEffect(-2, &actor->pos);
				AlsPenthouse::g_gunslingerEncounterState = AlsPenthouse::GUNSLINGER_ENCOUNTER_DEFEATED;
				g_hudActorAnimationFrame = 0;
				actor->movementCommandTimer = 0;
			}

			if (AlsPenthouse::g_gunslingerEncounterState >= AlsPenthouse::GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				if (actor->pos.z < -0x5B4F0)
				{
					actor->boundary.x = -0x9D020;
					actor->boundary.z = -0x62040;
					actor->creatureRam->boundHalfX = 0x12B;
					actor->creatureRam->boundHalfZ = 0x70;
				}
				else
				{
					actor->boundary.x = -0xA85A0;
					actor->boundary.z = -0x43FC0;
					actor->creatureRam->boundHalfX = 0x60;
					actor->creatureRam->boundHalfZ = 0x17E;
				}
			}

			if (AlsPenthouse::g_gunslingerEncounterState == AlsPenthouse::GUNSLINGER_ENCOUNTER_COMPLETE)
			{
				actor->pos.x = actor->boundary.x;
				actor->pos.z = actor->boundary.z;
			}
		}

		// FUNCTION: TOY2 0x00428650 [MATCHED]
		void Rabid(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
			{
				actor->previousActorPhase = (*g_randDatBufferPtr++ & 0x7F) + 0x3C;
				AudioManager::PlaySoundEffect(0x96, &actor->pos);
			}

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0 && actor->actorPhase == 0x66)
			{
				g_levelObjectiveProgress++;
				Particles::SpawnCollectSparkle(actor->pos.x, actor->pos.y - 0x2000, actor->pos.z, 0x32);
				AudioManager::PlaySoundEffect(0x96, &actor->pos);
				Actor::Kill(actor, 2);
			}
		}
	}
}

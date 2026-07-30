#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Renderer/Renderer.h"
#include "Random.h"
#include "Numerics.h"

#include <math.h>
#include <limits.h>

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;

	namespace ConstructionYard
	{
		enum DrillEncounterState
		{
			DRILL_ENCOUNTER_ACTIVE = 2,
			DRILL_ENCOUNTER_DEFEATED = 3,
		};

		struct HiddenCollectibleState
		{
			int32_t* verticalPosition;
			int32_t savedVerticalPosition;
		};

		struct MoveableObjectInitTable
		{
			MoveableObject::InitEntry entries[2];
			int16_t terminator;
		};

		// GLOBAL: TOY2 0x004F1570
		char g_paintMixingTokenInstructions[] = "if you can mix the paint to match the colors on the wall you will get a pizza planet ^token^.";

		// GLOBAL: TOY2 0x004F1BA4
		uint16_t g_platform1MotionScript[] = { 4, 4, 0, 20, 3, 0x7F, 0x60, 1, 0, 0, 0, 20, 3, 0x7F, 0x20, 0, 0xF, 0 };
		// GLOBAL: TOY2 0x004F1BC8
		uint16_t g_platform2MotionScript[] = { 4, 4, 1, 20, 8, 0x100, 3, 0x7F, 0x60, 9, 0x100, 1, 0, 0, 20, 3, 0x7F, 0x20, 0, 0x13, 0, 0 };
		// GLOBAL: TOY2 0x004F1BF4
		uint16_t g_platform3MotionScript[] = { 4, 4, 2, 20, 3, 0x3F, 0x60, 1, 0, 0, 0, 20, 6, 0x100, 3, 0x7F, 0x60, 0, 0x11, 0 };
		// GLOBAL: TOY2 0x004F1C1C
		uint16_t g_platform4MotionScript[] = { 4, 4, 3, 20, 3, 0x7F, 0x60, 1, 0, 0, 0, 20, 3, 0x7F, 0x20, 0, 0xF, 0 };
		// GLOBAL: TOY2 0x004F1C40
		uint16_t g_liftPairMotionScript[] = { 1, 0, 0x316, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10, 0 };
		// GLOBAL: TOY2 0x004F1C64
		uint16_t g_platformPairMotionScript[] = { 1, 0, 0x316, 0xC, 3, 0x7F, 0x20, 1, 0, 0, 0, 0xC, 3, 0x7F, 0x20, 0, 0x10, 0 };

		// GLOBAL: TOY2 0x004F1C88
		MoveableObjectInitTable g_moveableObjectInitTable = {
			{
				{ 0x20, 0, 3 },
				{ 0xC, 0x12, 5 },
			},
			-1,
		};

		// GLOBAL: TOY2 0x004F1C98
		int16_t g_tokenLinkIds[] = { 0x60, 0x62, 0x64, 0x61, 0x63, 0 };

		// GLOBAL: TOY2 0x004F1CA4
		extern const Collectables::TokenDialogueValue g_tokenDialogueValues[] = {
			{ 0x21 }, { reinterpret_cast<int32_t>(g_paintMixingTokenInstructions) }, { 0 }, { -1 }
		};

		// GLOBAL: TOY2 0x004F1CCC
		int32_t g_drillArenaXThresholds[4] = { -336724, -158292, -129428, 49196 };
		// GLOBAL: TOY2 0x004F1CDC
		int32_t g_drillArenaZThresholds[4] = { -496094, -317790, -289694, -111262 };
		// GLOBAL: TOY2 0x004F1CEC
		int32_t g_drillArenaXEdges[4] = { -336724, -158291, -129428, 49197 };
		// GLOBAL: TOY2 0x004F1CFC
		int32_t g_drillArenaZEdges[4] = { -496094, -317789, -289694, -111261 };

		// GLOBAL: TOY2 0x0052F760
		int32_t g_paintMixerPulseTimer;
		// GLOBAL: TOY2 0x0052F764
		int32_t g_secondPaintColor;
		// GLOBAL: TOY2 0x0052F768
		int32_t g_platform17MotionTimer;
		// GLOBAL: TOY2 0x0052F76C
		int32_t g_unusedState;
		// GLOBAL: TOY2 0x0052F770
		int32_t g_completedPaintMask;
		// GLOBAL: TOY2 0x0052F774
		int32_t g_platform16MotionBlend;
		// GLOBAL: TOY2 0x0052F778
		int32_t g_platform17MotionBlend;
		// GLOBAL: TOY2 0x0052F77C
		int32_t g_platform14MotionBlend;
		// GLOBAL: TOY2 0x0052F780
		int32_t g_platform15MotionBlend;
		// GLOBAL: TOY2 0x0052F784
		int32_t g_slammedPlatformFlags;
		// GLOBAL: TOY2 0x0052F788
		int32_t g_platform4MotionBlend;
		// GLOBAL: TOY2 0x0052F78C
		int32_t g_platform1MotionBlend;
		// GLOBAL: TOY2 0x0052F790
		int32_t g_platform2MotionBlend;
		// GLOBAL: TOY2 0x0052F794
		int32_t g_platform3MotionBlend;
		// GLOBAL: TOY2 0x0052F798
		int32_t g_drillTintToggle;
		// GLOBAL: TOY2 0x0052F79C
		int32_t g_thirdDrillCycleTimer;
		// GLOBAL: TOY2 0x0052F7A0
		int32_t g_secondDrillCycleTimer;
		// GLOBAL: TOY2 0x0052F7A4
		int32_t g_firstDrillCycleTimer;
		// GLOBAL: TOY2 0x0052F7A8
		int32_t g_thirdDrillVerticalOffset;
		// GLOBAL: TOY2 0x0052F7AC
		int32_t g_firstDrillVerticalOffset;
		// GLOBAL: TOY2 0x0052F7B0
		int32_t g_secondDrillVerticalOffset;
		// GLOBAL: TOY2 0x0052F7B4
		int32_t g_secondDrillVerticalVelocity;
		// GLOBAL: TOY2 0x0052F7B8
		int32_t g_firstDrillVerticalVelocity;
		// GLOBAL: TOY2 0x0052F7BC
		int32_t g_thirdDrillVerticalVelocity;
		// GLOBAL: TOY2 0x0052F7C0
		int32_t g_paintPuzzleTarget;
		// GLOBAL: TOY2 0x0052F7C4
		int32_t g_paintPuzzleStation;
		// GLOBAL: TOY2 0x0052F7C8
		int32_t g_workLightIntensity;
		// GLOBAL: TOY2 0x0052F7CC
		int32_t g_drillArenaRegionCode;
		// GLOBAL: TOY2 0x0052F7D0
		int32_t g_portalGateScale;
		// GLOBAL: TOY2 0x0052F7D4
		int32_t g_buzzInSludge;
		// GLOBAL: TOY2 0x0052F7D8
		int32_t g_paintPuzzleTimer;
		// GLOBAL: TOY2 0x0052F7DC
		int32_t g_platform21VerticalSpeed;
		// GLOBAL: TOY2 0x0052F7E0
		int32_t g_rotatingPlatformState;
		// GLOBAL: TOY2 0x0052F7E4
		int32_t g_firstPortalRecordIndex;
		// GLOBAL: TOY2 0x0052F7E8
		int32_t g_secondPortalRecordIndex;
		// GLOBAL: TOY2 0x0052F7EC
		int32_t g_ambientProjectileTimer;
		// GLOBAL: TOY2 0x0052F7F0
		int32_t g_gatePortalEntryByteOffset;
		// GLOBAL: TOY2 0x0052F7F4
		int32_t g_workLightFlickerTimer;
		// GLOBAL: TOY2 0x0052F7F8
		int32_t g_platform1LinkState;
		// GLOBAL: TOY2 0x0052F7FC
		int32_t g_platform2LinkState;
		// GLOBAL: TOY2 0x0052F800
		int32_t g_platform3LinkState;
		// GLOBAL: TOY2 0x0052F804
		int32_t g_drillEncounterState;
		// GLOBAL: TOY2 0x0052F808
		int32_t g_platform4LinkState;
		// GLOBAL: TOY2 0x0052F80C
		int32_t g_firstPaintColor;
		// GLOBAL: TOY2 0x0052F810
		int32_t g_workLightTargetIntensity;
		// GLOBAL: TOY2 0x0052F814
		HiddenCollectibleState g_hiddenCollectibles[5];
		// GLOBAL: TOY2 0x0052F83C
		int32_t g_fallingParticleTimer;
		// GLOBAL: TOY2 0x0052F840
		Vector3I g_rotatingPlatformSoundPosition;
		// GLOBAL: TOY2 0x0052F850
		int32_t g_ambientProjectilePathIndex;
		// GLOBAL: TOY2 0x0052F854
		int32_t g_platform1MotionTimer;
		// GLOBAL: TOY2 0x0052F858
		int32_t g_platform4MotionTimer;
		// GLOBAL: TOY2 0x0052F85C
		int32_t g_platform3MotionTimer;
		// GLOBAL: TOY2 0x0052F860
		int32_t g_platform2MotionTimer;
		// GLOBAL: TOY2 0x0052F864
		int32_t g_drillTintTimer;
		// GLOBAL: TOY2 0x0052F868
		int32_t g_paintPuzzleInvalidSequence;
		// GLOBAL: TOY2 0x0052F86C
		int32_t g_paintMixerScale;
		// GLOBAL: TOY2 0x0052F870
		uint16_t* g_platform2MotionCursor;
		// GLOBAL: TOY2 0x0052F874
		uint16_t* g_platform1MotionCursor;
		// GLOBAL: TOY2 0x0052F878
		uint16_t* g_platform4MotionCursor;
		// GLOBAL: TOY2 0x0052F87C
		uint16_t* g_platform3MotionCursor;
		// GLOBAL: TOY2 0x0052F880
		int32_t g_gatePortalRecordIndex;
		// GLOBAL: TOY2 0x0052F884
		int32_t g_previousDrillPhase;
		// GLOBAL: TOY2 0x0052F888
		int32_t g_platform14MotionTimer;
		// GLOBAL: TOY2 0x0052F88C
		int32_t g_platform16MotionTimer;
		// GLOBAL: TOY2 0x0052F890
		int32_t g_platform15MotionTimer;
		// GLOBAL: TOY2 0x0052F894
		uint16_t* g_platform15MotionCursor;
		// GLOBAL: TOY2 0x0052F898
		uint16_t* g_platform14MotionCursor;
		// GLOBAL: TOY2 0x0052F89C
		uint16_t* g_platform17MotionCursor;
		// GLOBAL: TOY2 0x0052F8A0
		uint16_t* g_platform16MotionCursor;
		// GLOBAL: TOY2 0x0052F8A4
		int32_t g_fallingParticlePathIndex;

		STATIC_ASSERT(sizeof(HiddenCollectibleState) == 0x8);
		STATIC_ASSERT(sizeof(MoveableObjectInitTable) == 0xE);

		// FUNCTION: TOY2 0x0041BC20 [PROVISIONAL]
		void MoveDrills(int32_t* cycleTimer, int32_t* verticalVelocity, int32_t* verticalOffset, int32_t drillLinkId, int32_t cycleDuration, int32_t platformId)
		{
			Vector3I drillPosition;
			Nu3D::Link::GetCurrentPosFixed(drillLinkId, &drillPosition);
			if (Nu3D::Math::IsWithinDistance(&drillPosition, &g_buzzActor.posAngles.pos, 0x480) == 0)
				return;

			Nu3D::Link::GetTargetPosFixed(drillLinkId, &drillPosition);
			*cycleTimer += Renderer::g_frameDelta;
			if (*cycleTimer < 50)
			{
				drillPosition.y -= *cycleTimer * 0x400;
				Nu3D::Link::SetPositionRawAndCommit(drillLinkId, drillPosition.x >> 5, drillPosition.y >> 5, drillPosition.z >> 5);
				if (*cycleTimer < 5)
					Collision::MarkPlatformAsMoving(platformId);
				else
					Platform::DisableCollision(platformId);
			}
			else if (*cycleTimer < 150)
			{
				drillPosition.y -= 0xC800;
				Nu3D::Link::SetPositionRawAndCommit(drillLinkId, drillPosition.x >> 5, drillPosition.y >> 5, drillPosition.z >> 5);
				*verticalOffset = 0;
				*verticalVelocity = 0;
			}
			else if (*cycleTimer < 250)
			{
				*verticalVelocity += Renderer::g_frameDelta * 0x180;
				*verticalOffset += *verticalVelocity;
				if (*verticalOffset > 0xC800)
				{
					*verticalOffset = 0xC800;
					if (*verticalVelocity > 3000)
					{
						Nu3D::Particles::SpawnFromPreset(drillPosition.x, drillPosition.y - 0xC00, drillPosition.z, 0x19, 2);
						AudioManager::PlaySoundEffect(0x69, &drillPosition);

						for (int32_t particleIndex = 0; particleIndex < 8; particleIndex++)
						{
							Nu3D::Particles::ParticleInstance* particle =
								Nu3D::Particles::SpawnFromPreset(drillPosition.x, drillPosition.y, drillPosition.z, 0x42, 0xE);
							particle->groundAlignRot = *g_randDatBufferPtr++ << 4;
							particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
						}

						int32_t distanceResult = Nu3D::Math::IsWithinDistance(&drillPosition, &g_buzzActor.posAngles.pos, 500);
						if (distanceResult != 0)
							Camera::g_shakeTimer = 40 - ((int32_t)sqrt((float)distanceResult) >> 4);
					}
					*verticalVelocity = -(*verticalVelocity / 4);
				}

				drillPosition.y += *verticalOffset - 0xC800;
				Nu3D::Link::SetPositionRawAndCommit(drillLinkId, drillPosition.x >> 5, drillPosition.y >> 5, drillPosition.z >> 5);
				if (*verticalOffset > 0xB400)
					Collision::MarkPlatformAsMoving(platformId);
				else
					Platform::DisableCollision(platformId);
			}
			else if (*cycleTimer > cycleDuration)
			{
				*cycleTimer = 0;
				AudioManager::PlaySoundEffect(0x6A, &drillPosition);
			}

			Nu3D::Link::GetCurrentPosFixed(drillLinkId, &drillPosition);
			Nu3D::Link::SetPositionRawAndCommit(drillLinkId + 0x12, drillPosition.x >> 7, drillPosition.y >> 7, drillPosition.z >> 7);
		}

		// FUNCTION: TOY2 0x0041BEE0 [PROVISIONAL]
		void UpdateMovingPlatformLinks(int32_t baseLinkId, int32_t platformId, int32_t useZAxis, int32_t travelDistance)
		{
			Vector3I currentPosition;
			Vector3I targetPosition;
			Vector3I platformOrigin;
			Vector3I rotation;

			Nu3D::Link::GetCurrentPosFixed(baseLinkId, &currentPosition);
			Nu3D::Link::GetCurrentPosFixed(baseLinkId, &currentPosition);
			Nu3D::Link::GetTargetPosFixed(baseLinkId, &targetPosition);
			targetPosition.y = currentPosition.y - targetPosition.y;
			Nu3D::Link::SetScaleFromFixedOffsets(baseLinkId + 0x39, 0x1000, ((targetPosition.y + travelDistance) * 0x1000) / travelDistance, 0x1000);

			currentPosition.x >>= 5;
			currentPosition.y >>= 5;
			currentPosition.z >>= 5;
			Nu3D::Link::SetPositionRawAndCommit(baseLinkId + 8, currentPosition.x, currentPosition.y, currentPosition.z);

			Nu3D::Link::GetCurrentPosFixed(baseLinkId + 0x10, &targetPosition);
			targetPosition.y >>= 5;
			targetPosition.x = currentPosition.x;
			targetPosition.z = currentPosition.z;
			Nu3D::Link::SetPositionRawAndCommit(baseLinkId + 0x10, targetPosition.x, targetPosition.y, targetPosition.z);
			Nu3D::Link::SetPositionRawAndCommit(baseLinkId + 0x39, targetPosition.x, targetPosition.y, targetPosition.z);

			Nu3D::Link::GetCurrentPosFixed(baseLinkId + 0x14, &targetPosition);
			targetPosition.y >>= 5;
			if (useZAxis == 0)
			{
				targetPosition.x >>= 5;
				targetPosition.z = currentPosition.z;
			}
			else
			{
				targetPosition.x = currentPosition.x;
				targetPosition.z >>= 5;
			}
			Nu3D::Link::SetPositionRawAndCommit(baseLinkId + 0x14, targetPosition.x, targetPosition.y, targetPosition.z);

			Platform::GetOrigin(platformId, &platformOrigin);
			Platform::SetVelocity(
				platformId, targetPosition.x * 0x20 - platformOrigin.x, targetPosition.y * 0x20 - platformOrigin.y, targetPosition.z * 0x20 - platformOrigin.z);

			currentPosition.x >>= 2;
			currentPosition.y >>= 2;
			currentPosition.z >>= 2;
			Nu3D::Link::SetPositionRawAndCommit(baseLinkId + 4, currentPosition.x, currentPosition.y, currentPosition.z);

			Nu3D::Link::GetCurrentPosFixed(baseLinkId + 0x18, &targetPosition);
			targetPosition.y >>= 5;
			if (useZAxis == 0)
			{
				targetPosition.x >>= 5;
				targetPosition.z = currentPosition.z;
			}
			else
			{
				targetPosition.z >>= 5;
				targetPosition.x = currentPosition.x;
			}
			Nu3D::Link::SetPositionRawAndCommit(baseLinkId + 0x18, targetPosition.x, targetPosition.y, targetPosition.z);

			Nu3D::Link::GetRotation8Bit(baseLinkId, &rotation);
			Nu3D::Link::SetRotationAbsolute8bit(baseLinkId + 4, rotation.x, rotation.y, rotation.z);
		}

		// FUNCTION: TOY2 0x0041C100 [TOOL]
		void InitHiddenCollectibles()
		{
			Levels::RecordData* pickupRecords = Levels::g_recordData[63];
			Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
			for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
			{
				if (pickup->objectIndex == 110)
					g_hiddenCollectibles[0].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 111)
					g_hiddenCollectibles[1].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 112)
					g_hiddenCollectibles[2].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 113)
					g_hiddenCollectibles[3].verticalPosition = &pickup->position.y;
				if (pickup->objectIndex == 114)
					g_hiddenCollectibles[4].verticalPosition = &pickup->position.y;
			}

			for (int32_t hiddenIndex = 0; hiddenIndex < 5; hiddenIndex++)
			{
				g_hiddenCollectibles[hiddenIndex].savedVerticalPosition = *g_hiddenCollectibles[hiddenIndex].verticalPosition;
				*g_hiddenCollectibles[hiddenIndex].verticalPosition = INT_MIN;
				Nu3D::Link::SetScaleFromFixedOffsets(hiddenIndex + 110, 0, 0, 0);
			}
		}

		// FUNCTION: TOY2 0x0041C190 [PROVISIONAL]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x75);
			MoveableObject::InitTable(g_moveableObjectInitTable.entries);
			Collectables::LoadTokenTable(g_tokenDialogueValues);

			HUD::g_challengeState = 0;
			AndysHouse::g_raceCheckpointPassCount = 0;
			g_platform21VerticalSpeed = 0;
			g_slammedPlatformFlags = 0;

			int32_t category2EntryByteOffset = 0;
			int32_t portalEntryByteOffset = 0;
			while (Levels::g_portalZones[1].entries[portalEntryByteOffset / sizeof(Levels::PortalEntry)].recordIdx != 0xFF)
			{
				if (Levels::g_portalZones[1].entries[portalEntryByteOffset / sizeof(Levels::PortalEntry)].categoryIdx == 2)
					category2EntryByteOffset = portalEntryByteOffset;
				portalEntryByteOffset += sizeof(Levels::PortalEntry);
			}

			portalEntryByteOffset -= sizeof(Levels::PortalEntry);
			g_gatePortalEntryByteOffset = portalEntryByteOffset;
			int32_t portalEntryIndex = portalEntryByteOffset / sizeof(Levels::PortalEntry);
			int32_t category2EntryIndex = category2EntryByteOffset / sizeof(Levels::PortalEntry);
			uint32_t savedRecordIndex = Levels::g_portalZones[1].entries[portalEntryIndex].recordIdx;
			g_secondPortalRecordIndex = Levels::g_portalZones[1].entries[portalEntryIndex].categoryIdx;
			Levels::g_portalZones[1].entries[portalEntryIndex].recordIdx = Levels::g_portalZones[1].entries[category2EntryIndex].recordIdx;
			Levels::g_portalZones[1].entries[portalEntryIndex].categoryIdx = Levels::g_portalZones[1].entries[category2EntryIndex].categoryIdx;
			Levels::g_portalZones[1].entries[category2EntryIndex].recordIdx = static_cast<uint8_t>(savedRecordIndex);
			Levels::g_portalZones[1].entries[category2EntryIndex].categoryIdx = static_cast<uint8_t>(g_secondPortalRecordIndex);

			g_gatePortalRecordIndex = Levels::g_portalZones[1].entries[portalEntryByteOffset / sizeof(Levels::PortalEntry)].recordIdx;
			g_firstPortalRecordIndex = Levels::g_portalZones[2].entries[0].recordIdx;
			g_secondPortalRecordIndex = Levels::g_portalZones[2].entries[1].recordIdx;
			g_portalGateScale = 0x1000;
			g_workLightIntensity = 0;
			g_workLightTargetIntensity = 0x80;
			g_workLightFlickerTimer = 0x3C;
			g_unusedState = 0;
			g_drillArenaRegionCode = 0;
			g_environmentSurfaceY = 0;
			g_previousBuzzEnvironmentY = 0;
			g_environmentEffectType = 0;
			g_buzzInSludge = 0;
			g_drillEncounterState = 0;
			g_drillTintTimer = 0;
			g_previousDrillPhase = Actor::g_creatureActors[0x18].actorPhase;
			g_drillTintToggle = 0;

			g_platform1LinkState = 0;
			g_platform1MotionCursor = g_platform1MotionScript;
			g_platform1MotionTimer = 0;
			g_platform1MotionBlend = 0;
			g_platform2LinkState = 0;
			g_platform2MotionCursor = g_platform2MotionScript;
			g_platform2MotionTimer = 0;
			g_platform2MotionBlend = 0;
			g_platform3LinkState = 0;
			g_platform3MotionCursor = g_platform3MotionScript;
			g_platform3MotionTimer = 0;
			g_platform3MotionBlend = 0;
			g_platform4LinkState = 0;
			g_platform4MotionCursor = g_platform4MotionScript;
			g_platform4MotionTimer = 0;
			g_platform4MotionBlend = 0;

			Platform::SetRotationAngles(1, 0, 0xC00, 0);
			Platform::SetRotationAngles(2, 0, 0xC00, 0);
			Platform::SetRotationAngles(3, 0, 0, 0);
			Platform::SetRotationAngles(4, 0, 0, 0);
			Platform::AddFlags(1, 0x100);
			Platform::AddFlags(2, 0x100);
			Platform::AddFlags(3, 0x100);
			Platform::AddFlags(4, 0x100);

			g_ambientProjectilePathIndex = 0;
			g_fallingParticlePathIndex = 0;
			g_ambientProjectileTimer = 0;
			g_fallingParticleTimer = 0;
			Platform::AddFlags(0x1A, 0x100);
			Nu3D::Link::SetRotationRelative8bit(0x1C, 0, 0, 0x266);
			Nu3D::Link::SetRotationRelative8bit(0x1D, 0, 0, 0x266);
			g_rotatingPlatformState = 0;

			Nu3D::Link::SetScaleFromFixedOffsets(0x33, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x34, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x35, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x21, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x22, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x23, 0, 0, 0);

			g_paintPuzzleTarget = 0;
			g_platform14MotionCursor = g_platformPairMotionScript;
			g_platform15MotionCursor = g_platformPairMotionScript;
			g_paintPuzzleStation = 0;
			g_completedPaintMask = 0;
			g_paintMixerScale = 0;
			g_secondPaintColor = 0;
			g_firstPaintColor = 0;
			g_paintPuzzleTimer = 0;
			g_paintMixerPulseTimer = 0;
			g_paintPuzzleInvalidSequence = 0;

			Actor::Toy2Actor* paintMixer = &Actor::g_creatureActors[5];
			paintMixer->useTint = 1;
			paintMixer->scalePivotHeight = -0x8000;
			paintMixer->scaleZ = 0x1000;
			paintMixer->scaleX = 0x1000;
			paintMixer->scaleY = 0;

			g_platform14MotionTimer = 0;
			g_platform14MotionBlend = 0;
			g_platform15MotionTimer = 0;
			g_platform15MotionBlend = 0;
			g_platform16MotionCursor = g_liftPairMotionScript;
			g_platform16MotionTimer = 0;
			g_platform16MotionBlend = 0;
			g_platform17MotionCursor = g_liftPairMotionScript;
			g_platform17MotionTimer = 0;
			g_platform17MotionBlend = 0;

			g_firstDrillCycleTimer = 0;
			g_firstDrillVerticalOffset = 0;
			g_firstDrillVerticalVelocity = 0;
			Platform::DisableCollision(10);
			g_secondDrillCycleTimer = 0x21;
			g_secondDrillVerticalOffset = 0;
			g_secondDrillVerticalVelocity = 0;
			Platform::DisableCollision(12);
			g_thirdDrillCycleTimer = 0x42;
			g_thirdDrillVerticalOffset = 0;
			g_thirdDrillVerticalVelocity = 0;
			Platform::DisableCollision(11);

			MoveDrills(&g_firstDrillCycleTimer, &g_firstDrillVerticalVelocity, &g_firstDrillVerticalOffset, 0x24, 300, 10);
			MoveDrills(&g_secondDrillCycleTimer, &g_secondDrillVerticalVelocity, &g_secondDrillVerticalOffset, 0x25, 0x147, 12);
			MoveDrills(&g_thirdDrillCycleTimer, &g_thirdDrillVerticalVelocity, &g_thirdDrillVerticalOffset, 0x26, 0x15D, 11);

			Vector3I drillPosition;
			Nu3D::Link::GetCurrentPosFixed(0x24, &drillPosition);
			Nu3D::Link::SetPositionRawAndCommit(0x36, drillPosition.x >> 7, drillPosition.y >> 7, drillPosition.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(0x25, &drillPosition);
			Nu3D::Link::SetPositionRawAndCommit(0x37, drillPosition.x >> 7, drillPosition.y >> 7, drillPosition.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(0x26, &drillPosition);
			Nu3D::Link::SetPositionRawAndCommit(0x38, drillPosition.x >> 7, drillPosition.y >> 7, drillPosition.z >> 7);

			InitHiddenCollectibles();
			g_rotatingPlatformSoundPosition.x = -0x12FD0;
			g_rotatingPlatformSoundPosition.y = -0x23AF6;
			g_rotatingPlatformSoundPosition.z = 0x191B2;
		}

		// STUB: TOY2 0x0041C640
		void Interactions() {}
	}
}

namespace Toy2
{
	namespace CreatureBehaviour
	{
		// FUNCTION: TOY2 0x0041B780 [PROVISIONAL]
		void Drill(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			ConstructionYard::g_drillTintToggle = (ConstructionYard::g_drillTintToggle - 1) & 1;
			AudioManager::PlaySoundEffect(0x99, &actor->pos);

			if ((actor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				actor->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				AudioManager::PlaySoundEffect(0x9A, &actor->pos);
			}

			if (Camera::g_shakeTimer == 0 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 300) != 0)
			{
				Camera::g_shakeTimer = 20;
			}
			else if (Camera::g_shakeTimer < 20 && Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &actor->pos, 100) != 0)
			{
				Camera::g_shakeTimer = 40;
			}

			if (ConstructionYard::g_drillEncounterState > 1)
			{
				ConstructionYard::g_drillTintTimer -= Renderer::g_frameDelta;
				if (ConstructionYard::g_drillTintTimer < 0)
				{
					ConstructionYard::g_drillTintTimer = 0;
					actor->creatureRam->defenseMode = 7;
					actor->useTint = 0;
				}
				else if (ConstructionYard::g_drillTintToggle != 0)
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

			if (actor->actorPhase != ConstructionYard::g_previousDrillPhase)
			{
				ConstructionYard::g_previousDrillPhase = actor->actorPhase;
				ConstructionYard::g_drillTintTimer = 60;
				actor->creatureRam->defenseMode = 4;
			}

			if (g_discLauncherShotSlotsAvailable == 6 || ConstructionYard::g_drillEncounterState <= 1)
				actor->creatureRam->defenseMode = 4;
			else
				actor->creatureRam->defenseMode = 5;

			int32_t arenaRegionCode = 0;
			int32_t* arenaThreshold = ConstructionYard::g_drillArenaXThresholds;
			for (;;)
			{
				if (actor->pos.x < *arenaThreshold)
					break;
				arenaThreshold++;
				arenaRegionCode++;
				if (arenaThreshold >= ConstructionYard::g_drillArenaXThresholds + 4)
					break;
			}

			arenaThreshold = ConstructionYard::g_drillArenaZThresholds;
			for (;;)
			{
				if (actor->pos.z < *arenaThreshold)
					break;
				arenaThreshold++;
				arenaRegionCode += 0x10;
				if (arenaThreshold >= ConstructionYard::g_drillArenaZThresholds + 4)
					break;
			}

			if ((arenaRegionCode & 0x11) == 0x11)
			{
				if ((ConstructionYard::g_drillArenaRegionCode & 1) == 0)
				{
					int32_t regionIndex = (arenaRegionCode & 3) - 1;
					int32_t lowerEdge = ConstructionYard::g_drillArenaXEdges[regionIndex];
					int32_t upperEdge = ConstructionYard::g_drillArenaXEdges[regionIndex + 1];
					if (actor->pos.x < upperEdge - (upperEdge - lowerEdge) / 2)
						actor->pos.x = lowerEdge;
					else
						actor->pos.x = upperEdge;
					arenaRegionCode &= ~1;
				}

				if ((ConstructionYard::g_drillArenaRegionCode & 0x10) == 0)
				{
					int32_t regionIndex = ((arenaRegionCode >> 4) & 3) - 1;
					int32_t lowerEdge = ConstructionYard::g_drillArenaZEdges[regionIndex];
					int32_t upperEdge = ConstructionYard::g_drillArenaZEdges[regionIndex + 1];
					if (actor->pos.z < upperEdge - (upperEdge - lowerEdge) / 2)
						actor->pos.z = lowerEdge;
					else
						actor->pos.z = upperEdge;
					arenaRegionCode &= ~0x10;
				}
			}
			ConstructionYard::g_drillArenaRegionCode = arenaRegionCode;

			if ((uint32_t)actor->pos.y > (uint32_t)-639968)
				actor->pos.y = -639968;

			if (ConstructionYard::g_drillEncounterState < ConstructionYard::DRILL_ENCOUNTER_DEFEATED && g_framePulseOutputs.fourTick != 0)
			{
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(actor->pos.x, actor->pos.y, actor->pos.z, 4, 9);
				particle->lifetime = 32;
			}

			if (ConstructionYard::g_drillEncounterState == ConstructionYard::DRILL_ENCOUNTER_ACTIVE && g_buzzActor.posAngles.pos.y < -515621)
			{
				if (g_framePulseOutputs.thirtyTwoTick != 0)
				{
					int32_t particleX = actor->pos.x;
					int32_t particleY = actor->pos.y;
					int32_t particleZ = actor->pos.z;
					Nu3D::Particles::ParticleInstance* particle;
					if ((*g_randDatBufferPtr++ & 3) != 0)
					{
						particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 0x54, 0xE);
					}
					else
					{
						int32_t deltaY = particleY - g_buzzActor.posAngles.pos.y;
						int32_t deltaX = (particleX - g_buzzActor.posAngles.pos.x) >> 5;
						int32_t deltaZ = (particleZ - g_buzzActor.posAngles.pos.z) >> 5;
						int32_t projectileAngle = Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) & 0xFFF;
						int32_t horizontalDistance = (int32_t)sqrt((float)(deltaX * deltaX + deltaZ * deltaZ));
						int32_t horizontalSpeed = horizontalDistance * 3 / 4;
						int32_t trajectoryScale = (horizontalDistance << 12) / horizontalSpeed;
						particle = Nu3D::Particles::SpawnInstance(particleX,
							particleY,
							particleZ,
							Numerics::g_sinCosLUT[(projectileAngle - 0x800) & 0xFFF] * horizontalSpeed / 0x4000,
							(-trajectoryScale * 0x80) / 0x100 - (deltaY * 0x80) / trajectoryScale,
							Numerics::g_sinCosLUT[(projectileAngle - 0x400) & 0xFFF] * horizontalSpeed / 0x4000,
							0x80,
							0,
							0,
							0x54);
					}
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
					particle->width = 100;
					particle->height = 100;
				}

				g_hudActorAnimationFrame = actor->actorPhase * 54 / 30;
			}
		}

		// FUNCTION: TOY2 0x0041BB80 [MATCHED]
		void LTyke(Actor::Toy2Actor::ActorBehaviourContext* context)
		{
			Actor::Toy2Actor* actor = context->actor;
			actor->previousActorPhase -= (int16_t)Renderer::g_frameDelta;
			if (actor->previousActorPhase <= 0)
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

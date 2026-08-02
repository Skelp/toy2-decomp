#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/Lighting.h"
#include "Toy2/Particles.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Link.h"
#include "Nu3D/Camera.h"
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
	extern int32_t g_surfaceEffectState;

	namespace Platform
	{
		void StepMotionScript(int32_t platformIndex, int32_t linkId, int16_t** scriptPosition, int32_t* waitTimer, int32_t* speedScale);
	}

	namespace ConstructionYard
	{
		enum DrillEncounterState
		{
			DRILL_ENCOUNTER_ACTIVE = 2,
			DRILL_ENCOUNTER_DEFEATED = 3,
			DRILL_ENCOUNTER_TOKEN_DELAY_END = 0x78,
			DRILL_ENCOUNTER_TOKEN_AWARDED = 200,
		};

		enum SlammedPlatformFlags
		{
			SLAMMED_PLATFORM_FIRST = 1,
			SLAMMED_PLATFORM_SECOND = 2,
			SLAMMED_PLATFORM_THIRD = 4,
			SLAMMED_PLATFORM_LIFT_RETURNING = 8,
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

		struct PaintIndicatorEntry
		{
			int32_t completedMask;
			int32_t linkId;
		};

		// GLOBAL: TOY2 0x004F1570
		char g_paintMixingTokenInstructions[] = "if you can mix the paint to match the colors on the wall you will get a pizza planet ^token^.";

		// GLOBAL: TOY2 0x004F1B64
		PaintIndicatorEntry g_paintIndicatorEntries[] = {
			{ 1, 0x30 }, { 2, 0x31 }, { 4, 0x32 }, { 0, 0 },
			{ 1, -0x30 }, { 2, -0x31 }, { 4, -0x32 }, { 0, 0 }
		};

		// GLOBAL: TOY2 0x004F1BA4
		int16_t g_platform1MotionScript[] = { 4, 4, 0, 20, 3, 0x7F, 0x60, 1, 0, 0, 0, 20, 3, 0x7F, 0x20, 0, 0xF, 0 };
		// GLOBAL: TOY2 0x004F1BC8
		int16_t g_platform2MotionScript[] = { 4, 4, 1, 20, 8, 0x100, 3, 0x7F, 0x60, 9, 0x100, 1, 0, 0, 20, 3, 0x7F, 0x20, 0, 0x13, 0, 0 };
		// GLOBAL: TOY2 0x004F1BF4
		int16_t g_platform3MotionScript[] = { 4, 4, 2, 20, 3, 0x3F, 0x60, 1, 0, 0, 0, 20, 6, 0x100, 3, 0x7F, 0x60, 0, 0x11, 0 };
		// GLOBAL: TOY2 0x004F1C1C
		int16_t g_platform4MotionScript[] = { 4, 4, 3, 20, 3, 0x7F, 0x60, 1, 0, 0, 0, 20, 3, 0x7F, 0x20, 0, 0xF, 0 };
		// GLOBAL: TOY2 0x004F1C40
		int16_t g_liftPairMotionScript[] = { 1, 0, 0x316, 0xC, 3, 0xFF, 0x20, 1, 0, 0, 0, 0xC, 3, 0xFF, 0x20, 0, 0x10, 0 };
		// GLOBAL: TOY2 0x004F1C64
		int16_t g_platformPairMotionScript[] = { 1, 0, 0x316, 0xC, 3, 0x7F, 0x20, 1, 0, 0, 0, 0xC, 3, 0x7F, 0x20, 0, 0x10, 0 };

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

		// GLOBAL: TOY2 0x004F1CB8
		char* g_rotatingHintSubtitles[] = {
			"i think i saw hamm over by the ^wheelbarrow^.",
			"the ^foreman^ has lost his little tike workers. he is over by the ^trailer^.",
			"^slinky^ is right under the girders. he has a ^challenge^ for you!",
			"there is a paint mixing puzzle to solve in the ^trailer^!",
			"the ^jackhammer^ boss is at the top of the girders. i better warn you though, you need to help ^mr. potato head^ find his eye to get the ^disk launcher^ to defeat the ^jackhammer^ boss.",
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
		int16_t* g_platform2MotionCursor;
		// GLOBAL: TOY2 0x0052F874
		int16_t* g_platform1MotionCursor;
		// GLOBAL: TOY2 0x0052F878
		int16_t* g_platform4MotionCursor;
		// GLOBAL: TOY2 0x0052F87C
		int16_t* g_platform3MotionCursor;
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
		int16_t* g_platform15MotionCursor;
		// GLOBAL: TOY2 0x0052F898
		int16_t* g_platform14MotionCursor;
		// GLOBAL: TOY2 0x0052F89C
		int16_t* g_platform17MotionCursor;
		// GLOBAL: TOY2 0x0052F8A0
		int16_t* g_platform16MotionCursor;
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

		// FUNCTION: TOY2 0x0041C640 [PROVISIONAL]
		void Interactions()
		{
			Vector3I position;
			Nu3D::Link::GetCurrentPosFixed(12, &position);
			Nu3D::Link::SetPositionRawAndCommit(13, position.x >> 7, position.y >> 7, position.z >> 7);

			position.x = 0x51B97;
			position.y = -0x104FD;
			position.z = 0x5E58E;
			if (Nu3D::Math::IsWithinDistance(&position, &g_buzzActor.posAngles.pos, 250) != 0)
			{
				reinterpret_cast<uint8_t*>(&Levels::g_portalZones[1].entries[0])[g_gatePortalEntryByteOffset] = static_cast<uint8_t>(g_gatePortalRecordIndex);
				Levels::g_portalZones[2].entries[0].recordIdx = static_cast<uint8_t>(g_firstPortalRecordIndex);
				Levels::g_portalZones[2].entries[1].recordIdx = static_cast<uint8_t>(g_secondPortalRecordIndex);
				if (g_portalGateScale > 0)
					g_portalGateScale -= 0x200;
			}
			else
			{
				reinterpret_cast<uint8_t*>(&Levels::g_portalZones[1].entries[0])[g_gatePortalEntryByteOffset] = 0xFF;
				Levels::g_portalZones[2].entries[1].recordIdx = static_cast<uint8_t>(g_firstPortalRecordIndex);
				Levels::g_portalZones[2].entries[0].recordIdx = static_cast<uint8_t>(g_secondPortalRecordIndex);
				if (g_portalGateScale < 0x1000)
					g_portalGateScale += 0x200;
			}
			Nu3D::Link::SetScaleFromFixedOffsets(0x3F, 0x1000, g_portalGateScale, 0x1000);
			Nu3D::Link::SetScaleFromFixedOffsets(0x40, 0x1000, g_portalGateScale, 0x1000);

			g_platform1LinkState = Platform::StepTiltPhysics(1, 0, g_platform1LinkState, 2, -0xE0, 0xE0, 0x10);
			if (g_ledgeClimbPlatformIndex == 1 || g_ledgeClimbPlatformIndex == 7)
			{
				Platform::SetVelocity(1, 0, 0, 0);
				Platform::SetVelocity(7, 0, 0, 0);
			}
			else
				Platform::StepMotionScript(1, 0, &g_platform1MotionCursor, &g_platform1MotionTimer, &g_platform1MotionBlend);
			UpdateMovingPlatformLinks(0, 7, 0, 0x21980);

			g_platform2LinkState = Platform::StepTiltPhysics(2, 1, g_platform2LinkState, 2, -0xE0, 0xE0, 0x10);
			if (g_ledgeClimbPlatformIndex == 2 || g_ledgeClimbPlatformIndex == 8)
			{
				Platform::SetVelocity(2, 0, 0, 0);
				Platform::SetVelocity(8, 0, 0, 0);
			}
			else
				Platform::StepMotionScript(2, 1, &g_platform2MotionCursor, &g_platform2MotionTimer, &g_platform2MotionBlend);
			UpdateMovingPlatformLinks(1, 8, 1, 48000);

			g_platform3LinkState = Platform::StepTiltPhysics(3, 2, g_platform3LinkState, 2, -0xE0, 0xE0, 0x10);
			if (g_ledgeClimbPlatformIndex == 3 || g_ledgeClimbPlatformIndex == 9)
			{
				Platform::SetVelocity(3, 0, 0, 0);
				Platform::SetVelocity(9, 0, 0, 0);
			}
			else
				Platform::StepMotionScript(3, 2, &g_platform3MotionCursor, &g_platform3MotionTimer, &g_platform3MotionBlend);
			UpdateMovingPlatformLinks(2, 9, 0, 72000);

			g_platform4LinkState = Platform::StepTiltPhysics(4, 3, g_platform4LinkState, 2, -0xE0, 0xE0, 0x10);
			if (g_ledgeClimbPlatformIndex == 4 || g_ledgeClimbPlatformIndex == 6)
			{
				Platform::SetVelocity(4, 0, 0, 0);
				Platform::SetVelocity(6, 0, 0, 0);
			}
			else
				Platform::StepMotionScript(4, 3, &g_platform4MotionCursor, &g_platform4MotionTimer, &g_platform4MotionBlend);
			UpdateMovingPlatformLinks(3, 6, 1, 0xCE40);

			Platform::StepMotionScript(14, 0x29, &g_platform14MotionCursor, &g_platform14MotionTimer, &g_platform14MotionBlend);
			Platform::StepMotionScript(15, 0x2A, &g_platform15MotionCursor, &g_platform15MotionTimer, &g_platform15MotionBlend);
			Platform::StepMotionScript(16, 0x2B, &g_platform16MotionCursor, &g_platform16MotionTimer, &g_platform16MotionBlend);
			Platform::StepMotionScript(17, 0x2C, &g_platform17MotionCursor, &g_platform17MotionTimer, &g_platform17MotionBlend);

			Vector3I soundPosition;
			soundPosition.x = (Camera::g_renderCameraTransform.pos.x - g_rotatingPlatformSoundPosition.x) * 3 / 4 + g_rotatingPlatformSoundPosition.x;
			soundPosition.y = (Camera::g_renderCameraTransform.pos.y - g_rotatingPlatformSoundPosition.y) * 3 / 4 + g_rotatingPlatformSoundPosition.y;
			soundPosition.z = (Camera::g_renderCameraTransform.pos.z - g_rotatingPlatformSoundPosition.z) * 3 / 4 + g_rotatingPlatformSoundPosition.z;
			if (g_rotatingPlatformState != 0)
			{
				Vector3I rotation;
				Platform::GetRotation(0x1A, &rotation);
				if (g_rotatingPlatformState == 1)
				{
					if (rotation.z < -0x998)
					{
						g_rotatingPlatformState = 2;
						Platform::SetAngularVelocity(0x1A, 0, 0, 0);
						AudioManager::PlaySoundEffect(0x6D, &soundPosition);
					}
					else
					{
						Platform::SetAngularVelocity(0x1A, 0, 0, static_cast<int16_t>(Renderer::g_frameDelta) * -8);
						AudioManager::PlaySoundEffect(0x6E, &soundPosition);
					}
				}
				else
				{
					g_rotatingPlatformState += Renderer::g_frameDelta;
					if (g_rotatingPlatformState >= 0xF0)
					{
						if (rotation.z < -0x20)
							Platform::SetAngularVelocity(0x1A, 0, 0, static_cast<int16_t>(Renderer::g_frameDelta) * 8);
						else if (rotation.z < 0)
							Platform::SetAngularVelocity(0x1A, 0, 0, static_cast<int16_t>(-rotation.z));
						else
						{
							Platform::SetAngularVelocity(0x1A, 0, 0, 0);
							Platform::ClearFlags(0x1A, 8);
							g_rotatingPlatformState = 0;
						}
						AudioManager::PlaySoundEffect(g_rotatingPlatformState == 0 ? 0x6D : 0x6E, &soundPosition);
					}
				}
				int32_t linkedRotation = rotation.z / 4 + 0x266;
				Nu3D::Link::SetRotationRelative8bit(0x1C, 0, 0, linkedRotation);
				Nu3D::Link::SetRotationRelative8bit(0x1D, 0, 0, linkedRotation);
			}
			else if (g_buzzActor.airborneMode != 0 && g_groundSlamTimer != 0 && g_footingType == 0x24)
			{
				g_rotatingPlatformState = 1;
				Levels::DeactivateAmbientEmitter(3, 1);
				AudioManager::PlaySoundEffect(0x6D, &soundPosition);
			}

			MoveDrills(&g_firstDrillCycleTimer, &g_firstDrillVerticalVelocity, &g_firstDrillVerticalOffset, 0x24, 300, 10);
			MoveDrills(&g_secondDrillCycleTimer, &g_secondDrillVerticalVelocity, &g_secondDrillVerticalOffset, 0x25, 0x147, 12);
			MoveDrills(&g_thirdDrillCycleTimer, &g_thirdDrillVerticalVelocity, &g_thirdDrillVerticalOffset, 0x26, 0x15D, 11);

			if (g_fallingParticleTimer < 0)
			{
				Levels::RecordData* path = Levels::g_recordData[0];
				Vector3I* start = &path->data[g_fallingParticlePathIndex];
				position.x = start->x << 5;
				position.y = start->y << 5;
				position.z = start->z << 5;
				if (Nu3D::Math::IsWithinDistanceXZ(&position, &g_buzzActor.posAngles.pos, 0x500) != 0 && position.y < g_buzzActor.posAngles.pos.y
					&& g_buzzActor.posAngles.pos.y - position.y < 0x32000)
				{
					Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(position.x, position.y, position.z, 0x43, 0x13);
					particle->rotSpeed = (*g_randDatBufferPtr++ & 1) * 0x80 - 0x40;
					Vector3I* end = start + 1;
					if (abs(end->z - start->z) < abs(end->x - start->x))
						particle->velX = end->x < start->x ? -0x180 : 0x180;
					else
						particle->velZ = end->z < start->z ? -0x180 : 0x180;
				}
				g_fallingParticleTimer = (*g_randDatBufferPtr++ & 0x1F) + 0x10;
				g_fallingParticlePathIndex += 2;
				if (g_fallingParticlePathIndex > path->recordCount)
					g_fallingParticlePathIndex = 0;
			}
			g_fallingParticleTimer -= Renderer::g_frameDelta;
			if (g_ambientProjectileTimer < 0)
			{
				position.x = 0x783B;
				position.y = -0x7D05B;
				position.z = -0x742C4;
				if (Nu3D::Math::IsWithinDistanceXZ(&position, &g_buzzActor.posAngles.pos, 900) != 0
					&& position.y < g_buzzActor.posAngles.pos.y + 0x25800 && position.y > g_buzzActor.posAngles.pos.y - 0x19000)
				{
					g_ambientProjectilePathIndex = *g_randDatBufferPtr++ & 0xE;
					Levels::RecordData* path = Levels::g_recordData[1];
					Vector3I* start = &path->data[g_ambientProjectilePathIndex];
					Vector3I* end = start + 1;
					position.x = start->x * 0x20;
					position.y = start->y * 0x20;
					position.z = start->z * 0x20;
					int32_t deltaX = (position.x - end->x * 0x20) >> 8;
					int32_t deltaZ = (position.z - end->z * 0x20) >> 8;
					int32_t travelAngle = (Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ) - 0x800) & 0xFFF;
					int32_t distance = static_cast<int32_t>(sqrt(static_cast<double>(deltaX * deltaX + deltaZ * deltaZ)));
					int32_t horizontalSpeed = distance * 3;
					int32_t travelTime = (distance << 12) / horizontalSpeed;
					int32_t gravityDrop = travelTime * 0x90 / 64;
					int32_t velocityX = Numerics::g_sinCosLUT[travelAngle] * horizontalSpeed / 0x2000;
					int32_t velocityY = ((end->y * 0x20 - position.y) * 0x20) / travelTime - gravityDrop;
					int32_t velocityZ = Numerics::g_sinCosLUT[(travelAngle + 0x400) & 0xFFF] * horizontalSpeed / 0x2000;
					Nu3D::Particles::SpawnInstance(position.x, position.y, position.z, velocityX, velocityY, velocityZ, 0x90, 0,
						(*g_randDatBufferPtr++ & 1) * 0x80 - 0x40, 0x54);
				}
				g_ambientProjectileTimer = (*g_randDatBufferPtr++ & 0x1F) + 0x40;
			}
			g_ambientProjectileTimer -= Renderer::g_frameDelta;

			Platform::GetOrigin(0, &position);
			Actor::Toy2Actor* paintMixer = &Actor::g_creatureActors[5];
			paintMixer->pos = position;
			int32_t mixedColor = g_firstPaintColor + g_secondPaintColor * 3;
			if (g_paintPuzzleTimer < 1 && (mixedColor == 4 || mixedColor == 8 || mixedColor == 12))
			{
				g_paintMixerPulseTimer = 0x20;
				g_firstPaintColor = 0;
				g_secondPaintColor = 0;
				AudioManager::StartSoundSequenceOnActor(-6, &g_buzzActor.posAngles.pos);
			}

			g_paintPuzzleStation = 0;
			if (g_paintMixerPulseTimer > 0)
			{
				g_paintPuzzleInvalidSequence = 0;
				paintMixer->scaleY = static_cast<int16_t>(g_paintMixerScale * g_paintMixerPulseTimer / 32);
				g_paintMixerPulseTimer -= Renderer::g_frameDelta;
			}
			else if (g_buzzActor.airborneMode != 0 && g_groundSlamTimer != 0 && g_footingType > 0x1F && g_footingType < 0x24 && g_paintPuzzleTimer < 1)
			{
				g_paintPuzzleTarget = g_footingType - 0x1F;
				g_paintPuzzleTimer = 0x3C;
				Levels::DeactivateAmbientEmitter(0, 1);
				Levels::DeactivateAmbientEmitter(1, 1);
				Levels::DeactivateAmbientEmitter(2, 1);
			}

			Levels::RecordData* paintStations = Levels::g_recordData[3];
			for (int32_t station = 0; station <= paintStations->recordCount; ++station)
			{
				if (abs((position.x >> 5) - paintStations->data[station].x) < 200)
					g_paintPuzzleStation = station + 1;
			}
			if (g_paintPuzzleTimer == 0x3C && g_paintPuzzleStation > 4 && g_paintPuzzleStation - 3 == g_paintPuzzleTarget)
			{
				if (g_firstPaintColor == 0)
					g_firstPaintColor = g_paintPuzzleStation - 4;
				else if (g_secondPaintColor == 0)
				{
					g_secondPaintColor = g_paintPuzzleStation - 4;
					g_paintPuzzleInvalidSequence = 0;
				}
				else
					g_paintPuzzleInvalidSequence = 1;
			}
			if (g_paintPuzzleTarget > 1 && g_paintPuzzleTimer > 0)
			{
				int32_t buttonScale = g_paintPuzzleTimer > 0x34 ? (0x3C - g_paintPuzzleTimer) * 0x200 : (g_paintPuzzleTimer > 0xB ? 0x1000 : 0);
				Nu3D::Link::SetScaleFromFixedOffsets(g_paintPuzzleTarget + 0x1F, 0x1000, buttonScale, 0x1000);
			}

			if (g_paintPuzzleInvalidSequence == 0 && g_paintPuzzleStation >= 5 && g_paintPuzzleStation <= 7
				&& g_paintPuzzleStation - 3 == g_paintPuzzleTarget)
			{
				int16_t timer = static_cast<int16_t>(g_paintPuzzleTimer);
				switch (g_secondPaintColor * 3 + g_firstPaintColor - 1)
				{
					case 0: paintMixer->actorTint.r = 0x680; paintMixer->actorTint.g = 0; paintMixer->actorTint.b = 0; g_paintMixerScale = (0x3C - g_paintPuzzleTimer) * 0x20; break;
					case 1: paintMixer->actorTint.r = 0; paintMixer->actorTint.g = 0; paintMixer->actorTint.b = 0x600; g_paintMixerScale = (0x3C - g_paintPuzzleTimer) * 0x20; break;
					case 2: paintMixer->actorTint.r = 0x600; paintMixer->actorTint.g = 0x580; paintMixer->actorTint.b = 0; g_paintMixerScale = (0x3C - g_paintPuzzleTimer) * 0x20; break;
					case 4: paintMixer->actorTint.r = (0x3C - timer) * 14; paintMixer->actorTint.g = 0; paintMixer->actorTint.b = timer * 8 + 0x420; g_paintMixerScale = (0x7C - g_paintPuzzleTimer) * 0x20; break;
					case 5: paintMixer->actorTint.r = 0x600; paintMixer->actorTint.g = timer * 10 + 0x328; paintMixer->actorTint.b = 0; g_paintMixerScale = (0x7C - g_paintPuzzleTimer) * 0x20; break;
					case 6: paintMixer->actorTint.r = timer * 12 + 0x3B0; paintMixer->actorTint.g = 0; paintMixer->actorTint.b = (0x3C - timer) * 18; g_paintMixerScale = (0x7C - g_paintPuzzleTimer) * 0x20; break;
					case 8: paintMixer->actorTint.r = (timer * 3 + 12) * 8; paintMixer->actorTint.g = timer * 6 + 0x418; paintMixer->actorTint.b = 0; g_paintMixerScale = (0x7C - g_paintPuzzleTimer) * 0x20; break;
					case 9: paintMixer->actorTint.r = timer * 2 + 0x608; paintMixer->actorTint.g = (0x3C - timer) * 12; paintMixer->actorTint.b = 0; g_paintMixerScale = (0x7C - g_paintPuzzleTimer) * 0x20; break;
					case 10: paintMixer->actorTint.r = 0; paintMixer->actorTint.g = (0x3C - timer) * 16; paintMixer->actorTint.b = (timer * 3 + 12) * 8; g_paintMixerScale = (0x7C - g_paintPuzzleTimer) * 0x20; break;
				}
				paintMixer->scaleY = static_cast<int16_t>(g_paintMixerScale);
			}
			if (g_paintPuzzleTimer > 0)
				g_paintPuzzleTimer -= Renderer::g_frameDelta;

			mixedColor = g_secondPaintColor * 3 + g_firstPaintColor - 1;
			if (g_paintPuzzleStation > 0 && g_paintPuzzleStation < 4)
			{
				bool correctMix = (g_paintPuzzleStation == 1 && (mixedColor == 4 || mixedColor == 6))
					|| (g_paintPuzzleStation == 2 && (mixedColor == 5 || mixedColor == 9))
					|| (g_paintPuzzleStation == 3 && (mixedColor == 8 || mixedColor == 10));
				if (correctMix)
				{
					g_completedPaintMask |= 1 << (g_paintPuzzleStation - 1);
					g_paintMixerPulseTimer = 0x20;
					g_firstPaintColor = 0;
					g_secondPaintColor = 0;
					AudioManager::StartSoundSequenceOnActor(-5, &g_buzzActor.posAngles.pos);
				}
			}
			if (paintMixer->scaleY < 0x180)
				paintMixer->actorFlags &= ~Actor::ACTOR_FLAG_TARGETABLE;
			if (g_framePulseOutputs.eightTick != 0)
			{
				int32_t indicatorIndex = g_framePulsePhases.sixtyFourTick >> 3;
				PaintIndicatorEntry* indicator = &g_paintIndicatorEntries[indicatorIndex];
				if ((indicator->completedMask & g_completedPaintMask) != 0)
				{
					int32_t linkId = indicator->linkId;
					if (linkId < 0)
					{
						linkId = -linkId;
						Nu3D::Link::SetScaleFromFixedOffsets(linkId, 0, 0, 0);
						Nu3D::Link::SetScaleFromFixedOffsets(linkId + 3, 0x1000, 0x1000, 0x1000);
					}
					else
					{
						Nu3D::Link::SetScaleFromFixedOffsets(linkId, 0x1000, 0x1000, 0x1000);
						Nu3D::Link::SetScaleFromFixedOffsets(linkId + 3, 0, 0, 0);
					}
				}
			}
			if (g_completedPaintMask == 7 && Collectables::g_tokenStates[3].active == 0)
				Collectables::Activate(3, 0);

			Actor::PlayPeriodicHintSound(0x1A, 0xB5);
			Actor::Toy2Actor* challengeActor = &Actor::g_creatureActors[0x1A];
			if ((challengeActor->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				challengeActor->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (HUD::g_challengeState == 0)
				{
					AudioManager::Preset::PlayOneShotSound2(0xB6, challengeActor);
					Dialogue::Begin(0x1A, 0x1F, "hi buzz! if you can find ^five^ missing ^wrenches^ and bring them to me in time, i will give you a pizza planet ^token^!", -1, 0, -1);
					g_specialPickupCount = 0;
					HUD::g_challengeState = 1;
					for (int32_t i = 0; i < 5; ++i)
					{
						*g_hiddenCollectibles[i].verticalPosition = g_hiddenCollectibles[i].savedVerticalPosition;
						Nu3D::Link::SetScaleFromFixedOffsets(i + 0x6E, 0x1000, 0x1000, 0x1000);
					}
				}
				else if (g_specialPickupCount < 5)
					Dialogue::Begin(0x1A, 0x1F, "hurry up buzz! you still have ^wrenches^ to find!", -1, 0, -1);
				else if (HUD::g_challengeState != HUD::CHALLENGE_STATE_COMPLETE)
				{
					AudioManager::Preset::PlayOneShotSound2(0xB7, challengeActor);
					Dialogue::Begin(0x1A, 0x1F, "well done buzz! you have found all the ^wrenches^! here is your pizza planet ^token^!", -1, 0, 2);
					HUD::g_challengeState = 3;
				}
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
				AndysHouse::g_raceCheckpointPassCount = 150;
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
			{
				if (g_framePulseOutputs.sixtyFourTick != 0)
					--AndysHouse::g_raceCheckpointPassCount;
				if (AndysHouse::g_raceCheckpointPassCount < 100)
				{
					AndysHouse::g_raceCheckpointPassCount = 100;
					HUD::g_challengeState = 0;
					for (int32_t i = 0; i < 5; ++i)
					{
						*g_hiddenCollectibles[i].verticalPosition = INT_MIN;
						Nu3D::Link::SetScaleFromFixedOffsets(i + 0x6E, 0, 0, 0);
					}
				}
			}

			Actor::ItemReturnReward(0x15, 0x1E,
				"hi buzz! you need a ^disk launcher^ to defeat the angry ^jackhammer^ boss. find my missing ^eye^, i will give you the ^disk launcher^!",
				"wow! thanks buzz! in return for finding my ^eye^ i will let you use the ^disk launcher^. it should help you defeat the toughest enemies!",
				"the ^disk launcher^ will help you defeat the toughest enemies!", 0x440, 0xC40);
			Actor::CollectQuestReward(0x14, 0x1D, 0xE10, 0x6E0, 0);
			Actor::RotatingHint(0x11, 0x23, g_rotatingHintSubtitles);

			Actor::Toy2Actor* littleTikesOwner = &Actor::g_creatureActors[0x12];
			if ((littleTikesOwner->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				littleTikesOwner->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					if (g_levelObjectiveProgress == 5)
					{
						Dialogue::Begin(0x12, 0x20, "thanks for finding my ^little tikes^ buzz! here is a pizza planet ^token^!", -1, 0, 1);
						g_levelObjectiveProgress = -1;
					}
					else
						Dialogue::Begin(0x12, 0x20, "hi buzz! if you find my ^five^ missing ^little tikes^ and come back and find me, i will give you a pizza planet ^token^.", -1, 0, -1);
				}
			}

			if (Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, -0x5C801, 0x15F9F, -0x83718, -0x10E28) == 0 || g_buzzActor.posAngles.pos.y < -0x8D8A)
			{
				if (g_buzzInSludge != 0)
					g_surfaceEffectState = 0xB40018;
				g_buzzInSludge = 0;
				g_environmentSurfaceY = 0;
				g_environmentEffectType = 0;
			}
			else
			{
				g_environmentSurfaceY = -0x7A78;
				g_environmentEffectType = 2;
				g_buzzInSludge = 1;
			}

			Actor::Toy2Actor* drill = &Actor::g_creatureActors[0x18];
			drill->visibilityDistance = g_buzzActor.posAngles.pos.y > -0x89188 ? 0x708 : 0xED8;
			if (g_drillEncounterState == 0 && g_buzzActor.airborneMode != 0 && g_buzzActor.posAngles.pos.y < -0x9BFC5
				&& Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &drill->pos, 300) != 0)
			{
				g_drillEncounterState = 1;
				Dialogue::Begin(0x18, 0x22, "ha ha ha ha ... defeat the ^jackhammer^ boss to get a pizza planet ^token^! i hope you have the ^disk launcher^!", 0x580, 0xDC0, -1);
			}
			if (g_drillEncounterState == 1 && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				g_drillEncounterState = 2;
				drill->movementData += 12;
				drill->movementCommandTimer = 0;
				g_levelInteractionTimer = 0xB4;
			}
			if (g_drillEncounterState == DRILL_ENCOUNTER_ACTIVE)
			{
				if (drill->creatureId == 0)
				{
					g_drillEncounterState = Renderer::g_frameDelta + 3;
					g_hudActorAnimationFrame = 0;
				}
				else if (g_buzzActor.posAngles.pos.y < -0x7DE25)
					HUD::g_slideTimers[7] = 0x5A;
			}
			else if (g_drillEncounterState > DRILL_ENCOUNTER_ACTIVE && g_drillEncounterState < DRILL_ENCOUNTER_TOKEN_DELAY_END)
				g_drillEncounterState += Renderer::g_frameDelta;
			else if (g_drillEncounterState >= DRILL_ENCOUNTER_TOKEN_DELAY_END && g_drillEncounterState != DRILL_ENCOUNTER_TOKEN_AWARDED)
			{
				Collectables::Activate(4, 0);
				g_drillEncounterState = DRILL_ENCOUNTER_TOKEN_AWARDED;
			}

			g_workLightFlickerTimer -= Renderer::g_frameDelta;
			if (g_workLightFlickerTimer < 0)
			{
				g_workLightTargetIntensity = (g_workLightTargetIntensity - 0x80) & 0x80;
				g_workLightFlickerTimer = *g_randDatBufferPtr++ & 0x3F;
			}
			if (g_workLightIntensity < g_workLightTargetIntensity)
			{
				g_workLightIntensity += Renderer::g_frameDelta * 2;
				if (g_workLightIntensity > g_workLightTargetIntensity)
					g_workLightIntensity = g_workLightTargetIntensity;
			}
			else
			{
				g_workLightIntensity -= Renderer::g_frameDelta * 2;
				if (g_workLightIntensity < g_workLightTargetIntensity)
					g_workLightIntensity = g_workLightTargetIntensity;
			}
			if (g_workLightIntensity != 0)
			{
				int32_t red = g_workLightIntensity < 0x41 ? g_workLightIntensity * 2 : 0x80;
				int32_t blue = g_workLightIntensity < 0x41 ? 0 : g_workLightIntensity * 2 - 0x80;
				Levels::RecordData* lights = Levels::g_recordData[6];
				int32_t nearestDistance = INT_MAX;
				int32_t nearestIndex = 0;
				for (int32_t i = 0; i < lights->recordCount; ++i)
				{
					Vector3I* lightPosition = &lights->data[i];
					int32_t cameraX = (Camera::g_renderCameraTransform.pos.x - lightPosition->x * 0x20) >> 8;
					int32_t cameraY = (Camera::g_renderCameraTransform.pos.y - lightPosition->y * 0x20) >> 8;
					int32_t cameraZ = (Camera::g_renderCameraTransform.pos.z - lightPosition->z * 0x20) >> 8;
					if (cameraX * cameraX + cameraY * cameraY + cameraZ * cameraZ < 1000000)
					{
						Renderer::LensFlare::RegisterLight(lightPosition->x * 0x20, lightPosition->y * 0x20, lightPosition->z * 0x20, red, red, blue, 0x40);
						int32_t buzzX = (g_buzzActor.posAngles.pos.x - lightPosition->x * 0x20) >> 8;
						int32_t buzzY = (g_buzzActor.posAngles.pos.y - lightPosition->y * 0x20 - 0x2000) >> 8;
						int32_t buzzZ = (g_buzzActor.posAngles.pos.z - lightPosition->z * 0x20) >> 8;
						int32_t distance = buzzX * buzzX + buzzY * buzzY + buzzZ * buzzZ;
						if (distance < nearestDistance) { nearestDistance = distance; nearestIndex = i; }
					}
				}
				if (nearestDistance < 0x10000)
				{
					Vector3I* nearest = &lights->data[nearestIndex];
					Lighting::DynamicLight* light = &Lighting::g_lightingState.dynamicLights[1];
					light->sourceId = reinterpret_cast<int32_t>(nearest);
					light->position.x = nearest->x << 5; light->position.y = nearest->y << 5; light->position.z = nearest->z << 5;
					light->colour.r = static_cast<uint8_t>(red); light->colour.g = static_cast<uint8_t>(red); light->colour.b = static_cast<uint8_t>(blue);
					light->lifetime = 1;
				}
				else
					Lighting::g_lightingState.dynamicLights[1].lifetime = 0;
			}
			else
				Lighting::g_lightingState.dynamicLights[1].lifetime = 0;

			if (g_groundSlamTimer != 0)
			{
				for (int32_t i = 0; i < 3; ++i)
				{
					if ((g_slammedPlatformFlags & (1 << i)) == 0 && Platform::HadBuzzContactThisFrame(i + 0x16) != 0)
					{
						Platform::SetRotationAngles(i + 0x16, -0x180, 0, 0);
						Nu3D::Link::SetRotationRelative8bit(i + 0x41, -0x180, 0, 0);
						g_slammedPlatformFlags |= 1 << i;
						Levels::DeactivateAmbientEmitter(i + 4, 1);
					}
				}
			}
			int32_t targetOffset = (g_slammedPlatformFlags & SLAMMED_PLATFORM_THIRD) != 0 ? -0x63380
				: (g_slammedPlatformFlags & SLAMMED_PLATFORM_SECOND) != 0 ? -0x3B600
				: (g_slammedPlatformFlags & SLAMMED_PLATFORM_FIRST) != 0 ? -0x15E00 : 0;
			if (targetOffset != 0)
			{
				Vector3I targetPosition;
				Vector3I platformOrigin;
				Nu3D::Link::GetTargetPosFixed(0x44, &targetPosition);
				Platform::GetOrigin(0x15, &platformOrigin);
				Nu3D::Link::SetPositionRawAndCommit(0x44, platformOrigin.x >> 5, platformOrigin.y >> 5, platformOrigin.z >> 5);
				int32_t velocity;
				if ((g_slammedPlatformFlags & SLAMMED_PLATFORM_LIFT_RETURNING) == 0)
				{
					targetPosition.y += targetOffset;
					g_platform21VerticalSpeed += targetPosition.y < platformOrigin.y ? Renderer::g_frameDelta * 8 : Renderer::g_frameDelta * -8;
					if (g_platform21VerticalSpeed > 0x400) g_platform21VerticalSpeed = 0x400;
					if (g_platform21VerticalSpeed < 0) { g_platform21VerticalSpeed = 0; g_slammedPlatformFlags |= SLAMMED_PLATFORM_LIFT_RETURNING; }
					velocity = -g_platform21VerticalSpeed * Renderer::g_frameDelta;
				}
				else
				{
					g_platform21VerticalSpeed += platformOrigin.y + 0xE100 < targetPosition.y ? Renderer::g_frameDelta * 8 : Renderer::g_frameDelta * -8;
					if (g_platform21VerticalSpeed > 0x400) g_platform21VerticalSpeed = 0x400;
					if (g_platform21VerticalSpeed < 0) { g_platform21VerticalSpeed = 0; g_slammedPlatformFlags &= ~SLAMMED_PLATFORM_LIFT_RETURNING; }
					velocity = g_platform21VerticalSpeed * Renderer::g_frameDelta;
				}
				Platform::SetVelocity(0x15, 0, velocity, 0);
			}
			PlayLevelMusic();
		}
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

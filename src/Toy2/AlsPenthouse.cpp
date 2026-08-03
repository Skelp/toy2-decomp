#include "Toy2/Toy2.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/HiddenCollectible.h"
#include "Toy2/TokenDialogue.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/Lighting.h"
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

#include <limits.h>

extern "C" double __cdecl sqrt(double);
extern "C" int __cdecl abs(int);

namespace Toy2
{
	extern int32_t g_hudActorAnimationFrame;
	extern uint8_t g_environmentTintBlue;
	extern uint8_t g_environmentTintGreen;
	extern uint8_t g_environmentTintRed;

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

		struct RaisedPlatformLink
		{
			uint8_t platformId;
			uint8_t linkId;
		};
		struct GroundSlamObjectTrigger
		{
			uint8_t platformId;
			uint8_t ambientEmitterId;
			uint8_t replacementLinkId;
			uint8_t objectGroupSelector;
		};
		struct WaterButtonTrigger
		{
			uint8_t platformId;
			uint8_t raisedPlatformMask;
			uint8_t waterLevelUnits;
			uint8_t ambientEmitterId;
		};
		struct WaterLevelLink
		{
			int32_t minimumLevel;
			int32_t linkId;
			int32_t underwaterLinkMask;
		};
		struct MoveableObjectInitTable
		{
			MoveableObject::InitEntry entries[3];
			int16_t terminator;
		};
		struct TrainPathTransition
		{
			int16_t forwardRecordType;
			int16_t forwardDirection;
			int16_t reverseRecordType;
			int16_t reverseDirection;
		};
		STATIC_ASSERT(sizeof(Collectables::TokenDialogueTable<2>) == 0x24);
		STATIC_ASSERT(offsetof(Collectables::TokenDialogueTable<2>, terminator) == 0x20);
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
		// GLOBAL: TOY2 0x004F3E00
		TrainPathTransition g_trainPathTransitions[12] = {
			{ 2, 0, 7, 0 },
			{ 10, 0, 1, 1 },
			{ 8, 1, 5, 1 },
			{ 9, 1, 6, 0 },
			{ 3, 0, 6, 0 },
			{ 1, 0, 4, 0 },
			{ 0, 0, 1, 0 },
			{ 3, 1, 12, 0 },
			{ 11, 0, 4, 1 },
			{ 11, 1, 2, 1 },
			{ 10, 1, 9, 1 },
			{ 11, 1, 8, 0 },
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
		// GLOBAL: TOY2 0x004F3ED4
		Vector3I g_sector4ParticlePositions[4] = {
			{ 0x42DD1, 0x1F748, 0x42215 },
			{ 0x3DFD1, 0x1F748, 0x42215 },
			{ 0x3DFD1, 0x1F748, 0x3C75B },
			{ 0x42DD1, 0x1F748, 0x3C75B },
		};
		// GLOBAL: TOY2 0x004F3F04
		char* g_rotatingHintSubtitles[5] = {
			"^hamm^ is in the ^bathroom^! you can reach the ^bathroom^ through a hidden vent. look out for any ^locks^ that you can shoot!",
			"^jesse^ has lost her critters. she is on the ^kitchen table^.",
			"^bullseye^ has a ^challenge^ for you. he is in the entrance hall.",
			"you can get to the token on top of the ^train bed^ if you solve the train puzzle. you need to get the train to stop on the dead end track. you "
			"can change the track points using the yellow switches.",
			"the ^gunslinger^ boss is in the woody's roundup set in the ^trophy room^.",
		};
		// GLOBAL: TOY2 0x004F3F18
		int16_t g_tokenLinkIds[6] = { 96, 98, 99, 100, 97, 0 };
		// GLOBAL: TOY2 0x004F3F24
		extern const Collectables::TokenDialogueTable<2> g_tokenDialogueValues = {
			{
				{ 0x78, 0x16, g_waterButtonInstructions, 0xC00 },
				{ 0x77, 0x15, g_trainSwitchInstructions, 0xC00 },
			},
			-1,
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
		// GLOBAL: TOY2 0x004F3FCC
		WaterButtonTrigger g_waterButtonTriggers[4] = {
			{ 7, 0x10, 0, 3 },
			{ 8, 0x20, 0x14, 0 },
			{ 6, 0x40, 0x28, 1 },
			{ 9, 0x80, 0x48, 2 },
		};
		// GLOBAL: TOY2 0x004F3FDC
		GroundSlamObjectTrigger g_groundSlamObjectTriggers[3] = {
			{ 1, 10, 8, 3 },
			{ 3, 11, 9, 0 },
			{ 4, 12, 10, 0x60 },
		};
		// GLOBAL: TOY2 0x004F3FE8
		WaterLevelLink g_waterLevelLinks[5] = {
			{ -16000, 43, 0x100 },
			{ -32000, 20, 1 },
			{ -64000, 21, 2 },
			{ -96000, 22, 4 },
			{ -524288, 23, 8 },
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
		Collectables::HiddenCollectibleState g_hiddenCollectibles[5];

		STATIC_ASSERT(sizeof(Collectables::HiddenCollectibleState) == 0x8);

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

		// FUNCTION: TOY2 0x00428BA0 [EFFECTIVE]
		void UpdateObjectGroupFlash()
		{
			// Keep this read before the phase read. VC6 can reverse commutative global reads after unrelated source changes.
			int32_t phase = *reinterpret_cast<volatile int32_t*>(&Renderer::g_frameDelta);
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

		// FUNCTION: TOY2 0x004293D0 [PROVISIONAL]
		void UpdateObjectReplacement()
		{
			int32_t sourceLinkId;
			if ((g_activeLinkMask & 0x10) != 0)
				sourceLinkId = 1;
			else if ((g_activeLinkMask & 0x20) != 0)
				sourceLinkId = 2;
			else if ((g_activeLinkMask & 0x40) != 0)
				sourceLinkId = 3;
			else if ((g_activeLinkMask & 0x80) != 0)
				sourceLinkId = 4;

			g_objectReplacementPhase = (g_objectReplacementPhase + Renderer::g_frameDelta * 0x20) & 0x3FF;
			Vector3I sourcePosition;
			if ((g_activeLinkMask & 0x200) == 0 && g_objectReplacementPhase > 0x300)
			{
				g_activeLinkMask |= 0x200;
				Nu3D::Link::GetCurrentPosFixed(sourceLinkId, &sourcePosition);
				Nu3D::Link::SetPositionRawAndCommit(44, sourcePosition.x >> 5, sourcePosition.y >> 5, sourcePosition.z >> 5);
				Nu3D::Link::SetScaleFromFixedOffsets(sourceLinkId, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(44, 0x1000, 0x800, 0x1000);
				return;
			}

			if ((g_activeLinkMask & 0x200) != 0 && g_objectReplacementPhase < 0x300)
			{
				g_activeLinkMask &= ~0x200;
				Nu3D::Link::GetCurrentPosFixed(sourceLinkId, &sourcePosition);
				Nu3D::Link::SetPositionRawAndCommit(44, sourcePosition.x >> 5, sourcePosition.y >> 5, sourcePosition.z >> 5);
				Nu3D::Link::SetScaleFromFixedOffsets(sourceLinkId, 0x1000, 0x800, 0x1000);
				Nu3D::Link::SetScaleFromFixedOffsets(44, 0, 0, 0);
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
	}

	namespace Camera
	{
		extern Vector3I g_cutsceneFocusPosition;
		extern Vector3I g_cutsceneCameraPosition;
	}

	namespace AlsPenthouse
	{

		struct GroundSlamTargetTrigger
		{
			uint8_t platformId;
			uint8_t triggerLinkId;
			uint8_t flashLinkId;
			uint8_t ambientEmitterIndex;
		};
		enum GroundSlamTargetTriggerCursor
		{
			TRIGGER_CURSOR_PLATFORM = -2,
			TRIGGER_CURSOR_LINK = -1,
			TRIGGER_CURSOR_FLASH_LINK = 0,
			TRIGGER_CURSOR_AMBIENT_EMITTER = 1,
		};

		// GLOBAL: TOY2 0x004F3FB4
		GroundSlamTargetTrigger g_groundSlamTargetTriggers[6] = {
			{ 2, 11, 48, 9 },
			{ 17, 56, 48, 7 },
			{ 18, 57, 83, 8 },
			{ 5, 5, 48, 4 },
			{ 11, 7, 48, 6 },
			{ 10, 6, 48, 5 },
		};

		// FUNCTION: TOY2 0x00429910 [PROVISIONAL]
		void UpdateGroundSlamTargets()
		{
			uint8_t* trigger = &g_groundSlamTargetTriggers[0].flashLinkId;
			int32_t* targetLinkIds = g_groundSlamTargetLinkIds;
			int32_t* targetTimer = g_groundSlamTargetTimers;
			do
			{
				if (*targetTimer == 0)
				{
					Vector3I triggerPosition;
					Nu3D::Link::GetCurrentPosFixed(trigger[TRIGGER_CURSOR_LINK], &triggerPosition);
					if (Nu3D::Math::IsWithinDistance(&triggerPosition, &g_buzzActor.posAngles.pos, 0x460) != 0)
					{
						if (Nu3D::Math::IsWithinDistance(&triggerPosition, &g_buzzActor.posAngles.pos, 0x3E0) != 0)
						{
							if ((g_groundSlamTargetMask & 0x200) == 0 && g_groundSlamTargetFlashPhase > 0x300)
							{
								g_groundSlamTargetMask |= 0x200;
								Nu3D::Link::SetPositionRawAndCommit(
									trigger[TRIGGER_CURSOR_FLASH_LINK], triggerPosition.x >> 5, triggerPosition.y >> 5, triggerPosition.z >> 5);
								Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_LINK], 0, 0, 0);
								Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_FLASH_LINK], 0x1000, 0x1000, 0x1000);
							}
							else if ((g_groundSlamTargetMask & 0x200) != 0 && g_groundSlamTargetFlashPhase < 0x300)
							{
								g_groundSlamTargetMask &= ~0x200;
								Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_LINK], 0x1000, 0x1000, 0x1000);
								Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_FLASH_LINK], 0, 0, 0);
							}
						}
						else if ((g_groundSlamTargetMask & 0x200) != 0)
						{
							g_groundSlamTargetMask &= ~0x200;
							Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_LINK], 0x1000, 0x1000, 0x1000);
							Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_FLASH_LINK], 0, 0, 0);
						}
					}

					if (g_groundSlamTimer == -40 && (Platform::GetFlags(trigger[TRIGGER_CURSOR_PLATFORM]) & 3) == 2)
					{
						Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_LINK], 0x1000, 0x800, 0x1000);
						Platform::GetOrigin(trigger[TRIGGER_CURSOR_PLATFORM], &triggerPosition);
						triggerPosition.y += 0xE10;
						Platform::SetOrigin(trigger[TRIGGER_CURSOR_PLATFORM], triggerPosition.x, triggerPosition.y, triggerPosition.z);
						*targetTimer = 60;
						Nu3D::Link::GetCurrentPosFixed(*targetLinkIds, &triggerPosition);
						Camera::BeginScriptedCutsceneAtPoint(&triggerPosition, 0xB4, 0x20);
						Camera::g_cutsceneFocusPosition.y -= 0x2000;
						Camera::g_cutsceneCameraPosition.y -= 0x4000;
						Nu3D::Link::SetScaleFromFixedOffsets(trigger[TRIGGER_CURSOR_FLASH_LINK], 0, 0, 0);
						Levels::DeactivateAmbientEmitter(trigger[TRIGGER_CURSOR_AMBIENT_EMITTER], 1);
					}
				}

				targetTimer++;
				trigger += sizeof(GroundSlamTargetTrigger);
				targetLinkIds += 4;
			} while ((int32_t)targetTimer < (int32_t)(g_groundSlamTargetTimers + 6));
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

		// FUNCTION: TOY2 0x004295B0 [PROVISIONAL]
		void UpdateCannonTarget(int32_t sectorIndex, int32_t cannonLinkId, int32_t companionLinkId, uint32_t targetMask)
		{
			Vector3I cannonPosition;
			Nu3D::Link::GetCurrentPosFixed(cannonLinkId, &cannonPosition);
			if (Sector::g_currentSectorIndex != sectorIndex || Nu3D::Math::IsWithinDistance(&cannonPosition, &g_buzzActor.posAngles.pos, 0x480) == 0)
				return;

			if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &cannonPosition, 0x32) != 0)
			{
				int32_t damageAngle =
					Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - cannonPosition.x, g_buzzActor.posAngles.pos.z - cannonPosition.z);
				if ((g_groundSlamTargetMask & targetMask) != 0)
					Buzz::HandleDamage(damageAngle, 1);
				else
					Buzz::HandleDamage(damageAngle, 3);
			}

			if ((g_groundSlamTargetMask & targetMask) != 0)
			{
				if (g_framePulseOutputs.sevenTick != 0)
				{
					Nu3D::Particles::ParticleInstance* particle =
						Nu3D::Particles::SpawnFromPreset(cannonPosition.x, cannonPosition.y - 0x2000, cannonPosition.z, 0x11, 0x1A);
					particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				}
				return;
			}

			Vector3I cannonRotation;
			cannonRotation.x = g_buzzActor.posAngles.pos.x - cannonPosition.x;
			cannonRotation.z = g_buzzActor.posAngles.pos.z - cannonPosition.z;
			cannonRotation.y = cannonPosition.y;
			while (abs(cannonRotation.x) > 0x4000 || abs(cannonRotation.z) > 0x4000)
			{
				cannonRotation.x >>= 1;
				cannonRotation.z >>= 1;
			}

			int32_t targetAngle = Nu3D::Math::CartesianToFixedAngle(-cannonRotation.z, cannonRotation.x);
			Nu3D::Link::GetRotation8Bit(cannonLinkId, &cannonRotation);
			int32_t angleDelta = (targetAngle - cannonRotation.y) & 0xFFF;
			if (angleDelta < 0x800)
			{
				if (angleDelta < Renderer::g_frameDelta * 8)
					cannonRotation.y += angleDelta;
				else
					cannonRotation.y += Renderer::g_frameDelta * 8;
			}
			else if (0x1000 - angleDelta < Renderer::g_frameDelta * 8)
			{
				cannonRotation.y += angleDelta - 0x1000;
			}
			else
			{
				cannonRotation.y -= Renderer::g_frameDelta * 8;
			}

			Nu3D::Link::SetRotationRelative8bit(cannonLinkId, 0, cannonRotation.y, 0);
			Nu3D::Link::SetRotationRelative8bit(companionLinkId, 0, cannonRotation.y, 0);
			FireCannonProjectile(&cannonPosition, cannonRotation.y);
		}

		// FUNCTION: TOY2 0x00428890 [PROVISIONAL]
		void UpdateObjectGroups(uint32_t requestedMask)
		{
			ObjectGroup* group = g_objectGroups;
			do
			{
				if ((g_objectGroupMask & group->mask) != 0 && (requestedMask & group->mask) == 0)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(group->primaryLinkId, 0, 0, 0);
					Nu3D::Link::SetScaleFromFixedOffsets(group->alternateLinkId, 0, 0, 0);
					g_objectGroupMask &= -1 - group->mask;
					Nu3D::Link::SetRotationRelative8bit(group->rotationLinkId, group->rotationX, group->rotationY, group->rotationZ);
				}
				group++;
			} while (group < &g_objectGroups[7]);

			if ((g_objectGroupMask & 1) == 0 && (requestedMask & 1) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(36, 0x1000, 0x1000, 0x1000);
				g_objectGroupMask |= 1;
				g_trainPathTransitions[2].reverseRecordType = 5;
				g_trainPathTransitions[2].reverseDirection = 1;
				g_trainPathTransitions[0].forwardRecordType = 2;
				g_trainPathTransitions[0].forwardDirection = 0;
			}
			if ((g_objectGroupMask & 2) == 0 && (requestedMask & 2) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(37, 0x1000, 0x1000, 0x1000);
				g_objectGroupMask |= 2;
				g_trainPathTransitions[2].reverseRecordType = 1;
				g_trainPathTransitions[2].reverseDirection = 1;
				g_trainPathTransitions[0].forwardRecordType = 3;
				g_trainPathTransitions[0].forwardDirection = 0;
			}
			if ((g_objectGroupMask & 4) == 0 && (requestedMask & 4) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(31, 0x1000, 0x1000, 0x1000);
				g_objectGroupMask |= 4;
				g_trainPathTransitions[3].forwardRecordType = 9;
				g_trainPathTransitions[3].forwardDirection = 0;
				g_trainPathTransitions[9].reverseRecordType = 2;
				g_trainPathTransitions[9].reverseDirection = 1;
				g_trainPathTransitions[7].forwardRecordType = 3;
				g_trainPathTransitions[7].forwardDirection = 1;
			}
			if ((g_objectGroupMask & 8) == 0 && (requestedMask & 8) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(32, 0x1000, 0x1000, 0x1000);
				g_objectGroupMask |= 8;
				g_trainPathTransitions[3].forwardRecordType = 10;
				g_trainPathTransitions[3].forwardDirection = 1;
				g_trainPathTransitions[9].reverseRecordType = 4;
				g_trainPathTransitions[9].reverseDirection = 1;
				g_trainPathTransitions[7].forwardRecordType = 3;
				g_trainPathTransitions[7].forwardDirection = 1;
			}
			if ((g_objectGroupMask & 0x10) == 0 && (requestedMask & 0x10) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(33, 0x1000, 0x1000, 0x1000);
				g_objectGroupMask |= 0x10;
				g_trainPathTransitions[3].forwardRecordType = 8;
				g_trainPathTransitions[3].forwardDirection = 1;
				g_trainPathTransitions[9].reverseRecordType = 2;
				g_trainPathTransitions[9].reverseDirection = 1;
				g_trainPathTransitions[7].forwardRecordType = 4;
				g_trainPathTransitions[7].forwardDirection = 1;
			}
			if ((g_objectGroupMask & 0x20) == 0 && (requestedMask & 0x20) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(34, 0x1000, 0x1000, 0x1000);
				g_objectGroupMask |= 0x20;
				g_trainPathTransitions[10].reverseRecordType = 9;
				g_trainPathTransitions[10].reverseDirection = 1;
				g_trainPathTransitions[7].reverseRecordType = 12;
				g_trainPathTransitions[7].reverseDirection = 0;
			}
			if ((g_objectGroupMask & 0x40) == 0 && (requestedMask & 0x40) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(35, 0x1000, 0x1000, 0x1000);
				g_objectGroupMask |= 0x40;
				g_trainPathTransitions[10].reverseRecordType = 8;
				g_trainPathTransitions[10].reverseDirection = 0;
				g_trainPathTransitions[7].reverseRecordType = 11;
				g_trainPathTransitions[7].reverseDirection = 0;
			}
		}

		// FUNCTION: TOY2 0x00428C80 [MATCHED]
		void UpdateUnderwaterLinks(uint32_t requestedMask)
		{
			if (Camera::g_renderCameraTransform.pos.y > g_environmentSurfaceY)
				requestedMask = 0;

			if ((g_activeLinkMask & 0x100) != 0 && (requestedMask & 0x100) == 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(43, 0, 0, 0);
				g_activeLinkMask &= ~0x100;
			}
			if ((g_activeLinkMask & 1) != 0 && (requestedMask & 1) == 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(20, 0, 0, 0);
				g_activeLinkMask &= ~1;
			}
			if ((g_activeLinkMask & 2) != 0 && (requestedMask & 2) == 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(21, 0, 0, 0);
				g_activeLinkMask &= ~2;
			}
			if ((g_activeLinkMask & 4) != 0 && (requestedMask & 4) == 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(22, 0, 0, 0);
				g_activeLinkMask &= ~4;
			}
			if ((g_activeLinkMask & 8) != 0 && (requestedMask & 8) == 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(23, 0, 0, 0);
				g_activeLinkMask &= ~8;
			}

			if ((g_activeLinkMask & 0x100) == 0 && (requestedMask & 0x100) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(43, 0x1000, 0x1000, 0x1000);
				g_activeLinkMask |= 0x100;
			}
			if ((g_activeLinkMask & 1) == 0 && (requestedMask & 1) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(20, 0x1000, 0x1000, 0x1000);
				g_activeLinkMask |= 1;
			}
			if ((g_activeLinkMask & 2) == 0 && (requestedMask & 2) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(21, 0x1000, 0x1000, 0x1000);
				g_activeLinkMask |= 2;
			}
			if ((g_activeLinkMask & 4) == 0 && (requestedMask & 4) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(22, 0x1000, 0x1000, 0x1000);
				g_activeLinkMask |= 4;
			}
			if ((g_activeLinkMask & 8) == 0 && (requestedMask & 8) != 0)
			{
				Nu3D::Link::SetScaleFromFixedOffsets(23, 0x1000, 0x1000, 0x1000);
				g_activeLinkMask |= 8;
			}
		}

		// FUNCTION: TOY2 0x00428E70 [PROVISIONAL]
		void UpdateTrain()
		{
			if (g_trainPathRecordType == 0)
				return;

			if (Nu3D::Math::IsWithinDistance(&g_buzzActor.posAngles.pos, &g_trainPosition, 0x30) != 0)
			{
				int32_t damageAngle =
					Nu3D::Math::CartesianToFixedAngle(g_buzzActor.posAngles.pos.x - g_trainPosition.x, g_buzzActor.posAngles.pos.z - g_trainPosition.z);
				Buzz::HandleDamage(damageAngle, 1);
				g_trainSpeed = 0x10;
				if (g_trainSoundTimer > 40)
					g_trainSoundTimer = 40;
			}
			else if (g_trainSpeed < 0x100)
			{
				g_trainSpeed += 8;
			}

			if ((g_objectGroupMask & 0x400) == 0)
			{
				Levels::RecordData* pathRecord = Levels::g_recordData[g_trainPathRecordType];
				Vector3I pathDirection;
				pathDirection.x = pathRecord->data[g_trainPathPointIndex].x * 4 - g_trainPosition.x / 8;
				pathDirection.y = pathRecord->data[g_trainPathPointIndex].y * 4 - g_trainPosition.y / 8;
				pathDirection.z = pathRecord->data[g_trainPathPointIndex].z * 4 - g_trainPosition.z / 8;

				if (abs(pathDirection.x) < 0x400 && abs(pathDirection.y) < 0x400 && abs(pathDirection.z) < 0x400)
				{
					int32_t endPointIndex;
					if (g_trainPathDirection != 0)
					{
						g_trainPathPointIndex--;
						endPointIndex = -1;
					}
					else
					{
						g_trainPathPointIndex++;
						endPointIndex = pathRecord->recordCount;
					}

					if (g_trainPathPointIndex == endPointIndex)
					{
						TrainPathTransition* transition = &g_trainPathTransitions[g_trainPathRecordType - 1];
						if (g_trainPathDirection != 0)
						{
							g_trainPathDirection = transition->reverseDirection;
							g_trainPathRecordType = transition->reverseRecordType;
						}
						else
						{
							g_trainPathDirection = transition->forwardDirection;
							g_trainPathRecordType = transition->forwardRecordType;
						}

						if (g_trainPathDirection != 0)
							g_trainPathPointIndex = Levels::g_recordData[g_trainPathRecordType]->recordCount - 1;
						else
							g_trainPathPointIndex = 0;
					}
				}

				Nu3D::Math::NormalizeToFixedPoint(&pathDirection, &pathDirection);
				int32_t movementScale = Renderer::g_frameDelta * g_trainSpeed;
				g_trainPosition.x += movementScale * pathDirection.x >> 10;
				g_trainPosition.y += movementScale * pathDirection.y >> 10;
				g_trainPosition.z += movementScale * pathDirection.z >> 10;

				int32_t rotationDelta = (g_trainRotationAngle - Nu3D::Math::CartesianToFixedAngle(-pathDirection.z, -pathDirection.x)) & 0xFFF;
				if (rotationDelta > 0x800)
					g_trainRotationAngle += (0x1000 - rotationDelta) >> 2;
				else
					g_trainRotationAngle -= rotationDelta >> 2;

				Nu3D::Link::SetRotationRelative8bit(38, 0, g_trainRotationAngle, 0);
				Nu3D::Link::SetRotationRelative8bit(80, 0, g_trainRotationAngle, 0);
			}

			Nu3D::Link::SetPositionRawAndCommit(38, g_trainPosition.x >> 5, g_trainPosition.y >> 5, g_trainPosition.z >> 5);
			Nu3D::Link::SetPositionRawAndCommit(80, g_trainPosition.x >> 7, g_trainPosition.y >> 7, g_trainPosition.z >> 7);

			if (g_framePulseOutputs.fourTick != 0 && Nu3D::Math::IsWithinDistance(&Camera::g_renderCameraTransform.pos, &g_trainPosition, 0x300) != 0)
			{
				int32_t particleZ = g_trainPosition.z - (Numerics::g_sinCosLUT[g_trainRotationAngle & 0xFFF] >> 2);
				int32_t particleY = g_trainPosition.y - 0x4000;
				int32_t particleX = g_trainPosition.x - (Numerics::g_sinCosLUT[(g_trainRotationAngle + 0x400) & 0xFFF] >> 2);
				Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::SpawnFromPreset(particleX, particleY, particleZ, 0x11, 0x1B);
				particle->rotSpeed = *g_randDatBufferPtr++ - 0x80;
				g_trainSoundTimer -= Renderer::g_frameDelta;
				if (g_trainSoundTimer <= 0)
				{
					g_trainSoundTimer = *g_randDatBufferPtr++ + 30;
					AudioManager::PlaySoundEffect(0x97, &g_trainPosition);
				}
			}

			Vector3I collisionPosition;
			Nu3D::Link::GetCurrentPosFixed(30, &collisionPosition);
			if (g_trainCollisionTimer > 0)
			{
				g_trainCollisionTimer -= Renderer::g_frameDelta;
				if (Nu3D::Math::IsWithinDistance(&collisionPosition, &g_trainPosition, 0x60) != 0)
					g_trainCollisionTimer = 0xB4;
			}
			else if (Nu3D::Math::IsWithinDistance(&collisionPosition, &g_trainPosition, 0x60) != 0)
			{
				g_trainCollisionTimer = 0xB4;
				g_objectGroupMask |= 0x400;
			}
			else
			{
				g_objectGroupMask &= ~0x400;
			}

			if (g_trainPathRecordType == 0)
				Collision::MarkPlatformAsMoving(27);
		}

		// FUNCTION: TOY2 0x00429D70 [MATCHED]
		void Init()
		{
			Collectables::Init(g_tokenLinkIds, 0x71);
			Collectables::LoadTokenTable(reinterpret_cast<const Collectables::TokenDialogueValue*>(g_tokenDialogueValues.records));
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
			g_environmentTintRed = 0x68;
			g_environmentTintGreen = 0x68;
			g_environmentTintBlue = 0x80;
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
			UpdateObjectGroups(0x25);
			g_objectGroupFlashPhase = 0;
			g_trainPathRecordType = 1;
			g_trainPathPointIndex = 6;
			g_trainPathDirection = 0;
			g_trainPosition.x = Levels::g_recordData[1]->data[6].x << 5;
			g_trainPosition.y = Levels::g_recordData[1]->data[6].y << 5;
			g_trainPosition.z = Levels::g_recordData[1]->data[6].z << 5;
			UpdateTrain();

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

		// FUNCTION: TOY2 0x0042A130 [PROVISIONAL]
		void Interactions()
		{
			if (g_buzzActor.cosmicShieldTimer == 0)
			{
				if (g_platformCollisionState != 0)
				{
					Platform::DisableCollision(26);
					Collision::MarkPlatformAsMoving(25);
					g_platformCollisionState = 0;
				}
			}
			else if (g_platformCollisionState == 0)
			{
				Platform::DisableCollision(25);
				Collision::MarkPlatformAsMoving(26);
				g_platformCollisionState = 1;
			}

			g_waterLevelPhase += Renderer::g_frameDelta * 32;
			if (g_waterLevel < g_targetWaterLevel)
			{
				Vector3I* globalSoundPosition = reinterpret_cast<Vector3I*>(1);
				AudioManager::PlaySoundEffect(0x95, globalSoundPosition);
				g_waterLevel += Renderer::g_frameDelta * 0x200;
				if (g_waterLevel > g_targetWaterLevel)
					g_waterLevel = g_targetWaterLevel;
			}
			else if (g_waterLevel > g_targetWaterLevel)
			{
				Vector3I* globalSoundPosition = reinterpret_cast<Vector3I*>(1);
				AudioManager::PlaySoundEffect(0x95, globalSoundPosition);
				g_waterLevel -= Renderer::g_frameDelta * 0x40;
				if (g_waterLevel < g_targetWaterLevel)
					g_waterLevel = g_targetWaterLevel;
			}

			if (g_waterLevel == 0)
			{
				g_environmentEffectType = 0;
				UpdateUnderwaterLinks(0);
			}
			else
			{
				g_environmentSurfaceY = g_waterLevel + 0x2EA18;
				g_environmentEffectType = 1;
				for (int32_t levelIndex = 0; levelIndex < 5; levelIndex++)
				{
					WaterLevelLink* level = &g_waterLevelLinks[levelIndex];
					if (level->minimumLevel <= g_waterLevel)
					{
						Vector3I position;
						Nu3D::Link::GetTargetPosFixed(level->linkId, &position);
						position.y += g_waterLevel;
						Nu3D::Link::SetPositionRawAndCommit(level->linkId, position.x >> 5, position.y >> 5, position.z >> 5);
						UpdateUnderwaterLinks(level->underwaterLinkMask);
						break;
					}
				}
			}

			if (g_groundSlamTimer == -40)
			{
				for (int32_t objectIndex = 0; objectIndex < 3; objectIndex++)
				{
					GroundSlamObjectTrigger* trigger = &g_groundSlamObjectTriggers[objectIndex];
					if ((Platform::GetFlags(trigger->platformId) & 3) == 2)
					{
						Levels::DeactivateAmbientEmitter(trigger->ambientEmitterId, 1);
						g_replacedLinkId = trigger->replacementLinkId;
						g_linkReplacementTimer = 24;
						uint32_t mask;
						if (trigger->objectGroupSelector == 0)
							mask = (g_objectGroupMask >> 2 & 4) + (g_objectGroupMask & 12) * 2 + (g_objectGroupMask & ~28);
						else
							mask = trigger->objectGroupSelector ^ g_objectGroupMask;
						UpdateObjectGroups(mask);
					}
				}
				for (int32_t buttonIndex = 0; buttonIndex < 4; buttonIndex++)
				{
					WaterButtonTrigger* trigger = &g_waterButtonTriggers[buttonIndex];
					if ((Platform::GetFlags(trigger->platformId) & 3) == 2)
					{
						UpdateRaisedPlatforms(trigger->raisedPlatformMask);
						g_targetWaterLevel = trigger->waterLevelUnits * -0x640;
						Levels::DeactivateAmbientEmitter(trigger->ambientEmitterId, 1);
					}
				}
			}

			UpdateGroundSlamTargets();
			UpdateObjectGroupFlash();
			if (g_waterLevel < -64000 && g_drainLinkScalePhase != 0)
			{
				g_drainLinkScalePhase = 0;
				Nu3D::Link::SetScaleFromFixedOffsets(91, 0, 0, 0);
			}
			else if (g_waterLevel == -64000 && g_drainLinkScalePhase < 0x2AB)
			{
				g_drainLinkScalePhase += Renderer::g_frameDelta * 2;
				if (g_drainLinkScalePhase > 0x2AB)
					g_drainLinkScalePhase = 0x2AB;
				int32_t scale = Numerics::g_sinCosLUT[(g_drainLinkScalePhase + 0x155) & 0xFFF] >> 2;
				Nu3D::Link::SetScaleFromFixedOffsets(91, scale, scale, scale);
			}
			else if (g_waterLevel > -64000 && g_drainLinkScalePhase != 0)
			{
				g_drainLinkScalePhase -= Renderer::g_frameDelta * 2;
				if (g_drainLinkScalePhase < 1)
					g_drainLinkScalePhase = 0;
				int32_t scale = Numerics::g_sinCosLUT[(g_drainLinkScalePhase + 0x155) & 0xFFF] >> 2;
				Nu3D::Link::SetScaleFromFixedOffsets(91, scale, scale, scale);
			}

			UpdateObjectReplacement();
			Vector3I position = { 0x2CC2E, 0x2EE2B, -0x2FC03 };
			if (Nu3D::Math::IsWithinDistance(&position, &g_buzzActor.posAngles.pos, 0x80) != 0)
			{
				UpdateRaisedPlatforms(0x10);
				g_targetWaterLevel = 0;
			}
			UpdateFloatingPlatform(13, 39, -0xA28, &g_platform13VerticalVelocity, &g_platform13VerticalOffset);
			UpdateFloatingPlatform(14, 40, -0xA28, &g_platform14VerticalVelocity, &g_platform14VerticalOffset);
			UpdateFloatingPlatform(15, 41, -0x10428, &g_platform15VerticalVelocity, &g_platform15VerticalOffset);
			UpdateFloatingPlatform(16, 42, -0xA28, &g_platform16VerticalVelocity, &g_platform16VerticalOffset);
			UpdateTrain();

			int32_t replacedLinkId = g_replacedLinkId;
			if (g_linkReplacementTimer != 0)
			{
				if (g_linkReplacementTimer == 24)
				{
					Nu3D::Link::GetCurrentPosFixed(replacedLinkId, &position);
					Nu3D::Link::SetPositionRawAndCommit(49, position.x >> 5, position.y >> 5, position.z >> 5);
					Nu3D::Link::SetScaleFromFixedOffsets(replacedLinkId, 0, 0, 0);
					Nu3D::Link::SetScaleFromFixedOffsets(49, 0x1000, 0x1000, 0x1000);
				}
				g_linkReplacementTimer -= Renderer::g_frameDelta;
				if (g_linkReplacementTimer < 0)
					g_linkReplacementTimer = 0;
				if (g_linkReplacementTimer == 0)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(replacedLinkId, 0x1000, 0x1000, 0x1000);
					Nu3D::Link::SetScaleFromFixedOffsets(49, 0, 0, 0);
				}
			}

			g_cannonFireTimer += Renderer::g_frameDelta;
			if (g_cannonFireTimer > 300)
				g_cannonFireTimer += 1000;
			g_groundSlamTargetFlashPhase = (g_groundSlamTargetFlashPhase + Renderer::g_frameDelta * 32) & 0x3FF;
			UpdateCannonTarget(3, 50, 59, 1);
			UpdateCannonTarget(2, 51, 60, 2);
			UpdateCannonTarget(5, 52, 61, 8);
			UpdateCannonTarget(4, 53, 62, 0x10);
			UpdateCannonTarget(1, 54, 63, 0x20);
			UpdateCannonTarget(2, 58, 65, 4);
			if (g_cannonFireTimer > 1000)
				g_cannonFireTimer -= 1300;

			if (g_buzzActor.airborneMode != 0 && (Platform::GetFlags(12) & 3) == 2 && Platform::GetContactFaceNormal(12)->y < -0x3000)
			{
				Levels::DeactivateAmbientEmitter(13, 1);
				int32_t launchVelocity;
				if (g_groundSlamTimer == 0)
					launchVelocity = -0x980;
				else
				{
					g_groundSlamTimer = 0;
					launchVelocity = -0xC00;
				}
				Buzz::Launch(launchVelocity, 2);
				AudioManager::PlaySoundEffect(0x1C, &g_buzzActor.posAngles.pos);
			}

			if (g_platform17RotationPhase != 0)
			{
				if (g_platform17RotationPhase == 1)
					Platform::SetRotationAngles(23, 0, 0x3AE, 0);
				int32_t angle = Numerics::g_sinCosLUT[g_platform17RotationPhase & 0xFFF] / 18;
				Nu3D::Link::SetRotationRelative8bit(25, 0, angle, 0);
				Nu3D::Link::SetRotationRelative8bit(26, 0, angle, 0);
				if (g_platform17RotationPhase < 0x400)
					g_platform17RotationPhase += Renderer::g_frameDelta * 16;
			}
			if (g_platform16RotationPhase != 0)
			{
				if (g_platform16RotationPhase == 1)
					Platform::SetRotationAngles(22, 0, -0x424, 0);
				int32_t angle = Numerics::g_sinCosLUT[g_platform16RotationPhase & 0xFFF] * -2 / 33;
				Nu3D::Link::SetRotationRelative8bit(28, 0, angle, 0);
				Nu3D::Link::SetRotationRelative8bit(90, 0, angle, 0);
				if (g_platform16RotationPhase < 0x400)
					g_platform16RotationPhase += Renderer::g_frameDelta * 16;
			}

			Nu3D::Link::GetCurrentPosFixed(81, &position);
			Nu3D::Link::SetPositionRawAndCommit(87, position.x >> 7, position.y >> 7, position.z >> 7);
			Nu3D::Link::GetCurrentPosFixed(29, &position);
			Nu3D::Link::SetPositionRawAndCommit(64, position.x >> 7, position.y >> 7, position.z >> 7);
			for (int32_t targetIndex = 0; targetIndex < 6; targetIndex++)
			{
				if (g_groundSlamTargetTimers[targetIndex] > 0)
				{
					g_groundSlamTargetTimers[targetIndex] -= Renderer::g_frameDelta;
					if (g_groundSlamTargetTimers[targetIndex] < 1)
					{
						g_groundSlamTargetTimers[targetIndex] = -1;
						CompleteGroundSlamTarget(targetIndex * 4);
						g_groundSlamTargetMask |= 1 << targetIndex;
					}
				}
			}

			Actor::CollectQuestReward(0, 15, -1, 0, 0);
			Actor::RotatingHint(12, 20, g_rotatingHintSubtitles);
			Actor::PlayPeriodicHintSound(1, 0xB2);

			Actor::Toy2Actor* jessie = &Actor::g_creatureActors[1];
			if ((jessie->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				jessie->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				if (g_levelObjectiveProgress >= 0)
				{
					if (g_levelObjectiveProgress == 5)
					{
						AudioManager::Preset::PlayOneShotSound2(0xB4, jessie);
						Dialogue::Begin(1, 0x11, "thanks for finding my ^critters^ buzz! here is a pizza planet ^token^!", -1, 0, 1);
						g_levelObjectiveProgress = -1;
					}
					else
					{
						AudioManager::Preset::PlayOneShotSound2(0xB3, jessie);
						Dialogue::Begin(1,
							0x11,
							"howdy buzz! all my ^critters^ have escaped! if you can find all ^five^ of them and come and see me, i will give you a pizza "
							"planet ^token^.",
							-1,
							0,
							-1);
					}
				}
			}

			Actor::Toy2Actor* bullseye = &Actor::g_creatureActors[2];
			if ((bullseye->actorFlags & Actor::ACTOR_FLAG_INTERACTION_REQUESTED) != 0)
			{
				bullseye->actorFlags &= ~Actor::ACTOR_FLAG_INTERACTION_REQUESTED;
				AudioManager::PlaySoundEffect(0x91, &bullseye->pos);
				if (HUD::g_challengeState == HUD::CHALLENGE_STATE_INACTIVE)
				{
					Dialogue::Begin(2,
						0x12,
						"howdy buzz! if you can bring me ^five^ horseshoes before you run out of time, i will give you a pizza planet ^token^.",
						-1,
						0,
						-1);
					g_specialPickupCount = 0;
					HUD::g_challengeState = HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA;
					for (int32_t showIndex = 0; showIndex < 5; showIndex++)
					{
						*g_hiddenCollectibles[showIndex].verticalPosition = g_hiddenCollectibles[showIndex].savedVerticalPosition;
						Nu3D::Link::SetScaleFromFixedOffsets(showIndex + 101, 0x1000, 0x1000, 0x1000);
					}
				}
				else if (g_specialPickupCount < 5)
				{
					Dialogue::Begin(2, 0x12, "quick! you need to find more horseshoes!", -1, 0, -1);
				}
				else if (HUD::g_challengeState != HUD::CHALLENGE_STATE_COMPLETE)
				{
					Dialogue::Begin(2, 0x12, "well done! you found all of my horseshoes! here is your pizza planet ^token^!", -1, 0, 2);
					HUD::g_challengeState = HUD::CHALLENGE_STATE_COMPLETE;
				}
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_WAITING_FOR_CAMERA && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				HUD::g_challengeState = HUD::CHALLENGE_STATE_ACTIVE;
				AndysHouse::g_raceCheckpointPassCount = 0x7F;
			}
			if (HUD::g_challengeState == HUD::CHALLENGE_STATE_ACTIVE)
			{
				if (Sector::g_activeSectorIndex != 1 && Sector::g_activeSectorIndex != 4 && Sector::g_activeSectorIndex != 8)
					AndysHouse::g_raceCheckpointPassCount = 99;
				if (g_framePulseOutputs.sixtyFourTick != 0)
					AndysHouse::g_raceCheckpointPassCount--;
				if (AndysHouse::g_raceCheckpointPassCount < 100)
				{
					AndysHouse::g_raceCheckpointPassCount = 100;
					HUD::g_challengeState = HUD::CHALLENGE_STATE_INACTIVE;
					for (int32_t hideIndex = 0; hideIndex < 5; hideIndex++)
					{
						*g_hiddenCollectibles[hideIndex].verticalPosition = INT_MIN;
						Nu3D::Link::SetScaleFromFixedOffsets(hideIndex + 101, 0, 0, 0);
					}
				}
			}

			if ((g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) != 0)
				g_cannonFireTimer = 0;
			if (Sector::g_activeSectorIndex == 2)
			{
				g_sector2ParticleTimer -= Renderer::g_frameDelta;
				if (g_sector2ParticleTimer < 0)
				{
					g_sector2ParticleVariant++;
					int32_t particleX;
					int32_t particleZ;
					int32_t horizontalVelocity;
					if (g_sector2ParticleVariant == 1)
					{
						particleX = -0x5C7E2;
						particleZ = -0x55251;
						horizontalVelocity = 0x180;
					}
					else
					{
						g_sector2ParticleVariant = 0;
						particleX = -0x5E7B0;
						particleZ = -0x5721F;
						horizontalVelocity = -0x180;
					}
					Nu3D::Particles::SpawnInstance(
						particleX, 0x187A8, particleZ, horizontalVelocity, -0x800, horizontalVelocity, 0, 0, *g_randDatBufferPtr++ - 0x80, 0x4C);
					g_sector2ParticleTimer = 100;
				}
			}

			if (g_platform17RotationPhase == 0 && Actor::g_creatureActors[10].actorPhase != 1)
				g_platform17RotationPhase = 1;
			if (g_platform16RotationPhase == 0 && Actor::g_creatureActors[9].actorPhase != 1)
				g_platform16RotationPhase = 1;
			if (g_gunslingerEncounterState == 0 && Actor::IsInsideBounds(&g_buzzActor.posAngles.pos, -0xB17CE, -0xA02CE, -0x3AF7C, -0x2B52C) != 0
				&& g_buzzActor.posAngles.pos.y < 0x1F448)
			{
				g_gunslingerEncounterState = 1;
				Dialogue::Begin(11, 0x10, "ha ha ha ha ... defeat the ^gunslinger^ boss to get a pizza planet ^token^!", -1, 0, -1);
			}
			if (g_gunslingerEncounterState == 1 && Nu3D::Camera::g_viewHistoryInitialized == 0)
			{
				g_levelInteractionTimer = 0xB4;
				g_gunslingerEncounterState = GUNSLINGER_ENCOUNTER_ACTIVE;
				Actor::g_creatureActors[11].movementCommandTimer = 0;
				Actor::g_creatureActors[11].movementData = CreatureBehaviour::g_gunslingerMovementData + 14;
				Actor::g_creatureActors[11].creatureRam->initialFacingAngle = 0;
			}
			else if (g_gunslingerEncounterState > GUNSLINGER_ENCOUNTER_ACTIVE)
			{
				if (g_gunslingerEncounterState < 120)
					g_gunslingerEncounterState += Renderer::g_frameDelta;
				else if (g_gunslingerEncounterState != GUNSLINGER_ENCOUNTER_COMPLETE)
				{
					Collectables::Activate(4, 0);
					g_gunslingerEncounterState = GUNSLINGER_ENCOUNTER_COMPLETE;
				}
			}

			if (Sector::g_currentSectorIndex == 1 && g_framePulseOutputs.twoTickCount != 0)
			{
				Vector3I sparklePosition = { 0x1187C, 0x2BD92, -0x13B72 };
				if (Nu3D::Math::IsWithinDistance(&Camera::g_renderCameraTransform.pos, &sparklePosition, 0x300) != 0)
				{
					sparklePosition.x += (*g_randDatBufferPtr++ - 0x80) * 0x20;
					sparklePosition.z += (*g_randDatBufferPtr++ - 0x80) * 0x40;
					Nu3D::Particles::SpawnFromPreset(sparklePosition.x, sparklePosition.y, sparklePosition.z, 0x65, 10);
				}
			}

			if (Sector::g_activeSectorIndex != 4)
			{
				Levels::RecordData* flareRecords = Levels::g_recordData[19];
				int32_t nearestDistanceSquared = INT_MAX;
				int32_t nearestFlareIndex = 0;
				for (int32_t flareIndex = 0; flareIndex < flareRecords->recordCount; flareIndex++)
				{
					Vector3I* flare = &flareRecords->data[flareIndex];
					int32_t cameraOffsetY = (Camera::g_renderCameraTransform.pos.y - flare->y * 0x20) >> 8;
					int32_t cameraOffsetZ = (Camera::g_renderCameraTransform.pos.z - flare->z * 0x20) >> 8;
					int32_t cameraOffsetX = (Camera::g_renderCameraTransform.pos.x - flare->x * 0x20) >> 8;
					if (cameraOffsetY * cameraOffsetY + cameraOffsetZ * cameraOffsetZ + cameraOffsetX * cameraOffsetX < 1000000)
					{
						Renderer::LensFlare::RegisterLight(flare->x * 0x20, flare->y * 0x20, flare->z * 0x20, 0x20, 0x20, 0x20, 0x80);
						int32_t buzzOffsetY = (g_buzzActor.posAngles.pos.y - flare->y * 0x20 - 0x2000) >> 8;
						int32_t buzzOffsetZ = (g_buzzActor.posAngles.pos.z - flare->z * 0x20) >> 8;
						int32_t buzzOffsetX = (g_buzzActor.posAngles.pos.x - flare->x * 0x20) >> 8;
						int32_t distanceSquared = buzzOffsetY * buzzOffsetY + buzzOffsetZ * buzzOffsetZ + buzzOffsetX * buzzOffsetX;
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
					light->position.x = nearestFlare->x << 5;
					light->position.y = nearestFlare->y << 5;
					light->position.z = nearestFlare->z << 5;
					light->colour.r = 0x7F;
					light->colour.g = 0x7F;
					light->colour.b = 0x7F;
					light->lifetime = 1;
					light->sourceId = reinterpret_cast<int32_t>(nearestFlare);
				}
				else
				{
					Lighting::g_lightingState.dynamicLights[1].lifetime = 0;
				}
			}

			if (Sector::g_currentSectorIndex == 5)
			{
				Renderer::BlitTextureByIndexOffset(7, 0x80, 0xA0, 0x20, 0x20, 0, (Numerics::g_sinCosLUT[g_sector5IconPhase] >> 9) & 0x1F, 0x20, 0);
				g_sector5IconPhase = (g_sector5IconPhase + Renderer::g_frameDelta * 8) & 0xFFF;
			}

			Actor::Toy2Actor* captive = &Actor::g_creatureActors[31];
			int32_t captiveY = captive->pos.y;
			if ((captive->actorFlags & Actor::ACTOR_FLAG_ACTIVE) != 0 && captive->pos.y > MoveableObject::g_objects[1].position.y - 0x3C00)
			{
				int32_t offsetX = (MoveableObject::g_objects[1].position.x - captive->pos.x) >> 5;
				int32_t offsetZ = (MoveableObject::g_objects[1].position.z - captive->pos.z) >> 5;
				if (offsetX * offsetX + offsetZ * offsetZ < 490000 && captive->pos.y >= MoveableObject::g_objects[1].position.y - 0x3400)
				{
					captiveY = MoveableObject::g_objects[1].position.y - 0x3C00;
					uint32_t captiveAngle = Nu3D::Math::CartesianToFixedAngle(offsetX, offsetZ);
					captive->pos.x = MoveableObject::g_objects[1].position.x - (Numerics::g_sinCosLUT[captiveAngle & 0xFFF] * 700 >> 9);
					captive->pos.z = MoveableObject::g_objects[1].position.z - (Numerics::g_sinCosLUT[(captiveAngle + 0x400) & 0xFFF] * 700 >> 9);
				}
			}
			captive->pos.y = captiveY;

			if (Sector::g_currentSectorIndex == 4)
			{
				g_sector4ParticleTimer -= Renderer::g_frameDelta;
				if (g_sector4ParticleTimer < 1)
				{
					g_sector4ParticleTimer = 60;
					g_sector4ParticlePositionIndex += 3;
					if (g_sector4ParticlePositionIndex > 11)
						g_sector4ParticlePositionIndex = 0;
					Vector3I* particlePosition =
						reinterpret_cast<Vector3I*>(reinterpret_cast<uint8_t*>(g_sector4ParticlePositions) + g_sector4ParticlePositionIndex * 4);
					Nu3D::Particles::SpawnFromPreset(particlePosition->x, particlePosition->y, particlePosition->z, 0x74, 2);
				}
			}
			if (g_forcedFacingActive == 2)
				Levels::DeactivateAmbientEmitter(1, 0);
			PlayLevelMusic();
		}

		STATIC_ASSERT(sizeof(ObjectGroup) == 0xE);
		STATIC_ASSERT(sizeof(TrainPathTransition) == 0x8);
		STATIC_ASSERT(sizeof(RaisedPlatformLink) == 0x2);
		STATIC_ASSERT(sizeof(GroundSlamObjectTrigger) == 0x4);
		STATIC_ASSERT(sizeof(WaterButtonTrigger) == 0x4);
		STATIC_ASSERT(sizeof(WaterLevelLink) == 0xC);
		STATIC_ASSERT(sizeof(GroundSlamTargetTrigger) == 0x4);
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

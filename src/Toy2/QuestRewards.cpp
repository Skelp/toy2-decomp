#include "Toy2/Actor.h"
#include "Toy2/BuzzLightPreset.h"
#include "Toy2/Lighting.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collectables.h"
#include "Toy2/Collision.h"
#include "Toy2/Dialogue.h"
#include "Toy2/Gadget.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "SaveManager.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "Toy2/Toy2Internal.h"

// The quest rewards, the hints they show and the geometry a level unlock
// reveals: retail holds 0x004A1CE0 through 0x004A28B0 as one object, in the
// order of this file.
namespace Toy2
{
	namespace Actor
	{
		// FUNCTION: TOY2 0x004A1CE0 [MATCHED]
		void CollectQuestReward(int32_t actorIndex, int32_t dialogueRecordIndex, int32_t actorFacingAngle, int32_t cameraFacingAngle, int32_t tokenIndex)
		{
			if (Collectables::g_tokenStates[tokenIndex].active == 0 && (g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) != 0
				&& (Toy2::g_gameplayStateFlags & 1) == 0)
			{
				g_coinQuestHintTimer -= Renderer::g_frameDelta;
				if (g_coinQuestHintTimer < 0)
				{
					g_coinQuestHintTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
					AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
						AudioManager::g_oneShotPresets[0xAC].encodedSoundIndex - 1,
						AudioManager::g_oneShotPresets[0xAC].baseFrequency,
						AudioManager::g_oneShotPresets[0xAC].leftVolume,
						&g_creatureActors[actorIndex],
						0);
				}
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_INTERACTION_REQUESTED) == 0)
				return;

			g_creatureActors[actorIndex].actorFlags &= ~ACTOR_FLAG_INTERACTION_REQUESTED;
			if (Collectables::g_tokenStates[tokenIndex].active != 0)
				return;

			if (Toy2::g_buzzActor.coinsCollected >= 50)
			{
				AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
					AudioManager::g_oneShotPresets[0xAE].encodedSoundIndex - 1,
					AudioManager::g_oneShotPresets[0xAE].baseFrequency,
					AudioManager::g_oneShotPresets[0xAE].leftVolume,
					&g_creatureActors[actorIndex],
					0);
				g_coinTokenAwarded = 1;
				Dialogue::Begin(
					actorIndex, dialogueRecordIndex, "well done buzz! here is your pizza planet ^token^.", actorFacingAngle, cameraFacingAngle, tokenIndex);
				return;
			}

			AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
				AudioManager::g_oneShotPresets[0xAD].encodedSoundIndex - 1,
				AudioManager::g_oneShotPresets[0xAD].baseFrequency,
				AudioManager::g_oneShotPresets[0xAD].leftVolume,
				&g_creatureActors[actorIndex],
				0);
			Dialogue::Begin(actorIndex,
				dialogueRecordIndex,
				"hi buzz! if you can bring me ^fifty^ coins, i will give you a pizza planet ^token^.",
				actorFacingAngle,
				cameraFacingAngle,
				-1);
		}

		// FUNCTION: TOY2 0x004A1E60 [MATCHED]
		void RotatingHint(int32_t actorIndex, int32_t dialogueRecordIndex, char** subtitles)
		{
			if (g_rotatingHintIndex == -1 && (g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_TARGETABLE) != 0)
				g_rotatingHintIndex = *g_randDatBufferPtr++ & 3;

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) != 0 && (g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
			{
				g_rotatingHintSoundTimer -= Renderer::g_frameDelta;
				if (g_rotatingHintSoundTimer < 0)
				{
					g_rotatingHintSoundTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
					AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
						AudioManager::g_oneShotPresets[0xAA].encodedSoundIndex - 1,
						AudioManager::g_oneShotPresets[0xAA].baseFrequency,
						AudioManager::g_oneShotPresets[0xAA].leftVolume,
						&g_creatureActors[actorIndex],
						0);
				}
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_INTERACTION_REQUESTED) == 0)
				return;

			if (g_rotatingHintIndex == -1)
				g_rotatingHintIndex = 0;
			g_creatureActors[actorIndex].actorFlags &= ~ACTOR_FLAG_INTERACTION_REQUESTED;

			char* subtitle;
			int32_t collectedTokenFlags = SaveManager::g_save0Data.tokens[g_levelFileIndex];
			if (collectedTokenFlags >= 0x1F)
			{
				subtitle = "hey! you have all the ^tokens^ buzz!";
			}
			else
			{
				AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
					AudioManager::g_oneShotPresets[0xAB].encodedSoundIndex - 1,
					AudioManager::g_oneShotPresets[0xAB].baseFrequency,
					AudioManager::g_oneShotPresets[0xAB].leftVolume,
					&g_creatureActors[actorIndex],
					0);
				while (((collectedTokenFlags >> g_rotatingHintIndex) & 1) != 0)
				{
					g_rotatingHintIndex++;
					if (g_rotatingHintIndex > 4)
						g_rotatingHintIndex = 0;
				}

				subtitle = subtitles[g_rotatingHintIndex++];
				if (g_rotatingHintIndex > 4)
					g_rotatingHintIndex = 0;
			}
			Dialogue::Begin(actorIndex, dialogueRecordIndex, subtitle, -1, 0, -1);
		}

	}
}

namespace Toy2
{
	namespace Gadget
	{
		// FUNCTION: TOY2 0x004A2080 [PROVISIONAL]
		void InitLevelUnlockGeometry()
		{
			Actor::g_coinTokenAwarded = 0;
			Actor::g_coinQuestHintTimer = 200;
			Actor::g_periodicHintSoundTimer = 250;
			Actor::g_itemReturnHintSoundTimer = 150;
			Actor::g_rotatingHintIndex = -1;
			Actor::g_rotatingHintSoundTimer = 100;

			LevelUnlockInfo* levelUnlock = &g_levelUnlockInfo[g_levelFileIndex - 1];
			if ((g_unlocks & levelUnlock->unlockFlag) == 0)
			{
				g_unlockNodeState = levelUnlock->modelNodeIndex;
				if (g_unlockNodeState != 0)
				{
					HideModelNode(9, g_unlockNodeState);
					if (g_unlockNodeState == 1)
					{
						HideModelNode(9, 0x0B);
						HideModelNode(9, 0x0C);
						HideModelNode(9, 0x0F);
					}
				}
			}
			else
			{
				int32_t linkId;
				if (g_levelFileIndex == 1)
					linkId = 0x46;
				else if (g_levelFileIndex == 4)
					linkId = 0x6D;
				else if (g_levelFileIndex == 7)
					linkId = 0x3F;
				else if (g_levelFileIndex == 10)
					linkId = 0x6F;
				else if (g_levelFileIndex == 13)
					linkId = 0x3F;

				Nu3D::Link::SetScaleFromFixedOffsets(linkId, 0, 0, 0);
				Levels::RecordData* pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
				Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
				for (int32_t pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
				{
					if (pickup->objectIndex == linkId)
					{
						pickup->position.y = INT_MIN;
						break;
					}
				}
				g_unlockNodeState = 0;
			}

			Levels::RecordData* pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
			UnlockGeometryEntry* entries;
			Collectables::PickupRecord* pickup;
			int32_t pickupIndex;

			if ((g_unlocks & 1) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit1Geometry, 8);
				pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
			}
			else
			{
				entries = g_unlockBit1Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 2) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit2Geometry, 4);
				pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
			}
			else
			{
				entries = g_unlockBit2Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 4) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit4Geometry, 4);
				pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
			}
			else
			{
				entries = g_unlockBit4Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 8) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit8Geometry, 4);
				pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
			}
			else
			{
				entries = g_unlockBit8Geometry;
				while (entries->levelIndex != 0xFF)
				{
					if (entries->levelIndex == g_levelFileIndex)
					{
						pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
						for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
						{
							if (pickup->objectIndex == entries->unlockedLinkId)
							{
								Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x1000, 0x1000, 0x1000);
								pickup->facingAngle = 0;
								pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
								break;
							}
						}
					}
					entries++;
				}
			}

			if ((g_unlocks & 0x10) != 0)
			{
				ApplyUnlockToGeometry(g_unlockBit16Geometry, 4);
				return;
			}

			entries = g_unlockBit16Geometry;
			while (entries->levelIndex != 0xFF)
			{
				if (entries->levelIndex == g_levelFileIndex)
				{
					pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
					for (pickupIndex = 0; pickupIndex < pickupRecords->recordCount; pickup++, pickupIndex++)
					{
						if (pickup->objectIndex == entries->unlockedLinkId)
						{
							Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, 0x0C00, 0x0C00, 0x0C00);
							pickup->facingAngle = 0;
							pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
							break;
						}
					}
				}
				entries++;
			}
		}

	}
}

namespace Toy2
{
	namespace Actor
	{
		// FUNCTION: TOY2 0x004A2480 [PROVISIONAL]
		void ItemReturnReward(int32_t actorIndex,
			int32_t dialogueRecordIndex,
			char* missingItemSubtitle,
			char* itemReturnedSubtitle,
			char* usageHintSubtitle,
			int32_t actorFacingAngle,
			int32_t cameraFacingAngle)
		{
			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) != 0 && (g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
			{
				g_itemReturnHintSoundTimer -= Renderer::g_frameDelta;
				if (g_itemReturnHintSoundTimer < 0)
				{
					g_itemReturnHintSoundTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
					AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
						AudioManager::g_oneShotPresets[0xAF].encodedSoundIndex - 1,
						AudioManager::g_oneShotPresets[0xAF].baseFrequency,
						AudioManager::g_oneShotPresets[0xAF].leftVolume,
						&g_creatureActors[actorIndex],
						0);
				}
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_INTERACTION_REQUESTED) == 0)
				return;

			g_creatureActors[actorIndex].actorFlags &= ~ACTOR_FLAG_INTERACTION_REQUESTED;
			if (Gadget::g_unlockNodeState == 0)
			{
				Dialogue::Begin(actorIndex, dialogueRecordIndex, usageHintSubtitle, actorFacingAngle, cameraFacingAngle, -1);
			}
			if (Gadget::g_unlockNodeState > 0)
			{
				Dialogue::Begin(actorIndex, dialogueRecordIndex, missingItemSubtitle, actorFacingAngle, cameraFacingAngle, -1);
				AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
					AudioManager::g_oneShotPresets[0xB0].encodedSoundIndex - 1,
					AudioManager::g_oneShotPresets[0xB0].baseFrequency,
					AudioManager::g_oneShotPresets[0xB0].leftVolume,
					&g_creatureActors[actorIndex],
					0);
			}
			if (Gadget::g_unlockNodeState >= 0)
				return;

			AudioManager::PlayOneShotSound3DActor(&g_creatureActors[actorIndex],
				AudioManager::g_oneShotPresets[0xB1].encodedSoundIndex - 1,
				AudioManager::g_oneShotPresets[0xB1].baseFrequency,
				AudioManager::g_oneShotPresets[0xB1].leftVolume,
				&g_creatureActors[actorIndex],
				0);
			ShowModelNode(9, -Gadget::g_unlockNodeState);
			if (Gadget::g_unlockNodeState == -1)
			{
				ShowModelNode(9, 0xB);
				ShowModelNode(9, 0xC);
				ShowModelNode(9, 0xF);
			}
			Dialogue::Begin(actorIndex, dialogueRecordIndex, itemReturnedSubtitle, actorFacingAngle, cameraFacingAngle, -1);

			Gadget::g_unlockNodeState = 0;
			uint16_t unlockFlag = Gadget::g_levelUnlockInfo[g_levelFileIndex - 1].unlockFlag;
			g_unlocks |= unlockFlag;
			switch (unlockFlag)
			{
				case 1:
					Gadget::ApplyUnlockToGeometry(Gadget::g_unlockBit1Geometry, 8);
					break;
				case 2:
					Gadget::ApplyUnlockToGeometry(Gadget::g_unlockBit2Geometry, 4);
					break;
				case 4:
					Gadget::ApplyUnlockToGeometry(Gadget::g_unlockBit4Geometry, 4);
					break;
				case 8:
					Gadget::ApplyUnlockToGeometry(Gadget::g_unlockBit8Geometry, 4);
					break;
				case 0x10:
					Gadget::ApplyUnlockToGeometry(Gadget::g_unlockBit16Geometry, 4);
					break;
			}
		}

		// FUNCTION: TOY2 0x004A26F0 [PROVISIONAL]
		void PlayPeriodicHintSound(int32_t actorIndex, int32_t soundPresetIndex)
		{
			if (soundPresetIndex == 0xB5)
			{
				if (Toy2::HUD::g_challengeState != 0)
					return;
			}
			else if (Toy2::g_levelObjectiveProgress < 0)
			{
				return;
			}

			if ((g_creatureActors[actorIndex].actorFlags & ACTOR_FLAG_ACTIVE) == 0 || (Toy2::g_gameplayStateFlags & 1) != 0)
				return;

			g_periodicHintSoundTimer -= Renderer::g_frameDelta;
			if (g_periodicHintSoundTimer >= 0)
				return;

			g_periodicHintSoundTimer = *g_randDatBufferPtr++ * 2 + 0xF0;
			AudioManager::OneShotSoundPreset* preset = &AudioManager::g_oneShotPresets[soundPresetIndex];
			AudioManager::PlayOneShotSound3DActor(
				&g_creatureActors[actorIndex], preset->encodedSoundIndex - 1, preset->baseFrequency, preset->leftVolume, &g_creatureActors[actorIndex], 0);
		}

	}
}

namespace Toy2
{
	namespace Gadget
	{
		// FUNCTION: TOY2 0x004A27A0 [PROVISIONAL]
		void ApplyUnlockToGeometry(const UnlockGeometryEntry* entries, int32_t scale)
		{
			while (entries->levelIndex != 0xFF)
			{
				if (entries->levelIndex == g_levelFileIndex)
				{
					Nu3D::Link::SetScaleFromFixedOffsets(entries->lockedLinkId, 0, 0, 0);
					int32_t fixedScale = scale << 10;
					Nu3D::Link::SetScaleFromFixedOffsets(entries->unlockedLinkId, fixedScale, fixedScale, fixedScale);
					Platform::DisableCollision(entries->platformIndex);

					Levels::RecordData* pickupRecords = Levels::g_recordData[Levels::RECORD_TYPE_PICKUPS];
					int32_t pickupCount = pickupRecords->recordCount;
					Collectables::PickupRecord* pickup = reinterpret_cast<Collectables::PickupRecord*>(pickupRecords + 1);
					for (int32_t pickupIndex = 0; pickupIndex < pickupCount; pickup++, pickupIndex++)
					{
						if (pickup->objectIndex == entries->unlockedLinkId)
						{
							pickup->facingAngle = 0x1F;
							break;
						}
					}
				}

				entries++;
			}
		}

	}
}

namespace Toy2
{
	namespace Actor
	{
		// FUNCTION: TOY2 0x004A28B0 [MATCHED]
		void PopulateActiveActors()
		{
			int32_t actorIndex = 0;
			while (g_activeActors[actorIndex] != 0)
			{
				g_renderActors[actorIndex] = g_activeActors[actorIndex];
				actorIndex++;
			}
			g_renderActors[actorIndex++] = (Toy2Actor*)&Toy2::g_buzzActor;
			g_renderActors[actorIndex] = 0;
		}
	}
}

#include "Toy2/Collectables.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Dialogue.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Particles.h"
#include "SaveManager.h"

#include <limits.h>
#include <string.h>

namespace Toy2
{
	namespace Collectables
	{
		// STUB: TOY2 0x004A0F80
		void Interactions() {}

		// GLOBAL: TOY2 0x00830CAC
		int32_t g_exitLevelAfterToken;

		// GLOBAL: TOY2 0x00830CC4
		int32_t g_tokenCollectionState;

		// FUNCTION: TOY2 0x004A0C80 [PROVISIONAL]
		void Init(int16_t* tokenLinkIds, int32_t firstHiddenLinkId)
		{
			int32_t collectedTokens = SaveManager::g_save0Data.tokens[g_levelFileIndex];
			int32_t i = 0;
			g_tokenCollectionState = 0;

			if (tokenLinkIds == 0)
			{
				g_exitLevelAfterToken = 1;
				int32_t* active = &g_tokenStates[0].active;
				do
				{
					*active = 0;
					active += sizeof(TokenState) / sizeof(int32_t);
				} while (reinterpret_cast<int32_t>(active) < reinterpret_cast<int32_t>(&g_tokenStates[5].active));
				return;
			}

			g_exitLevelAfterToken = 0;
			TokenState* token = g_tokenStates;
			do
			{
				i = 0;
				if ((collectedTokens & 1) != 0)
					Nu3D::Link::CopyShapeId(*tokenLinkIds, firstHiddenLinkId);

				token->linkId = *tokenLinkIds++;
				token->active = 0;
				Nu3D::Link::SetScaleFromFixedOffsets(token->linkId, 0, 0, 0);

				Levels::RecordData* pickupRecords = Levels::g_recordData[63];
				PickupRecord* pickup = reinterpret_cast<PickupRecord*>(pickupRecords + 1);
				for (i = 0; i < pickupRecords->recordCount; i++, pickup++)
				{
					if (pickup->objectIndex == token->linkId)
					{
						pickup->position.y = INT_MIN;
						pickupRecords = Levels::g_recordData[63];
						token->verticalPosition = &pickup->position.y;
						break;
					}
				}

				collectedTokens >>= 1;
				for (i = 0; i < pickupRecords->recordCount; i++, pickup++)
				{
					if (pickup->objectIndex == firstHiddenLinkId)
					{
						pickup->position.y = INT_MIN;
						break;
					}
				}

				Nu3D::Link::SetScaleFromFixedOffsets(firstHiddenLinkId, 0, 0, 0);
				token++;
				firstHiddenLinkId++;
			} while (reinterpret_cast<int32_t>(token) < reinterpret_cast<int32_t>(&g_tokenStates[5]));
		}

		// GLOBAL: TOY2 0x00830CCC
		TokenState g_tokenStates[5];

		// GLOBAL: TOY2 0x0050A150
		TokenDialogueEntry g_tokenDialogueEntries[10];

		// GLOBAL: TOY2 0x004DF69C
		extern const int32_t g_tokenCutsceneScript[] = { 1, -1, 0, 4, 2, 3, -1, 10, 7, -1, 8, -1 };

		// GLOBAL: TOY2 0x005546B8
		PickupTable g_pickupTable;

		// GLOBAL: TOY2 0x00556FAC
		Levels::RecordData* g_originalPickupRecords;

		// FUNCTION: TOY2 0x00447DB0 [PROVISIONAL]
		void BuildPickupTable()
		{
			Vector3I position;
			PosAndAngles groundProbe;
			int32_t firstObjectIndex = 0x30;
			if (g_levelFileIndex == 4)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 5)
				firstObjectIndex = 0x50;
			else if (g_levelFileIndex == 10)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 11)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 14)
				firstObjectIndex = 0x60;
			else if (g_levelFileIndex == 16)
				firstObjectIndex = 400;

			g_originalPickupRecords = Levels::g_recordData[63];
			PickupRecord* destination = g_pickupTable.records;
			if (Levels::g_recordData[59] != 0)
			{
				for (int32_t i = 0; i < Levels::g_recordData[59]->recordCount; i++)
				{
					groundProbe.pos.x = Levels::g_recordData[59]->data[i].x << 5;
					groundProbe.pos.z = Levels::g_recordData[59]->data[i].z << 5;
					groundProbe.pos.y = (Levels::g_recordData[59]->data[i].y - 10) << 5;
					Levels::g_recordData[59]->data[i].y = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
				}
			}

			if (Levels::g_recordData[63] != 0)
			{
				PickupRecord* source = reinterpret_cast<PickupRecord*>(Levels::g_recordData[63] + 1);
				for (int32_t i = 0; i < Levels::g_recordData[63]->recordCount; i++)
				{
					groundProbe.pos.x = source->position.x << 5;
					groundProbe.pos.z = source->position.z << 5;
					groundProbe.pos.y = (source->position.y - 10) << 5;
					source->groundHeight = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
					if (source->groundHeight == 0x7FFF)
						source->groundHeight = 0x7FFE;
					if (source->groundHeight - source->position.y > 0x1800)
						source->groundHeight = 0x7FFF;
					source->facingAngle = 0x11;
					*destination++ = *source++;
				}
				g_pickupTable.recordType = g_originalPickupRecords->recordType;
				g_pickupTable.recordCount = g_originalPickupRecords->recordCount;
			}
			else
			{
				g_pickupTable.recordType = 0;
				g_pickupTable.recordCount = 0;
			}

			Levels::g_recordData[63] = reinterpret_cast<Levels::RecordData*>(&g_pickupTable);

			if (g_levelFileIndex == 5)
			{
				Levels::g_objectListBase->entries[89]->z = -18000;
				Nu3D::Link::GetCurrentPosFixed(89, &position);
				Nu3D::Link::SetPositionRawAndCommit(89, position.x >> 5, position.y >> 5, -18000);
			}

			if (Levels::g_objectListBase->count > 47 && firstObjectIndex <= Levels::g_objectListBase->count)
			{
				for (int32_t objectIndex = firstObjectIndex; objectIndex <= Levels::g_objectListBase->count; objectIndex++)
				{
					if (Levels::g_objectListBase->entries[objectIndex] != 0)
					{
						destination->position.x = Levels::g_objectListBase->entries[objectIndex]->x;
						destination->position.y = Levels::g_objectListBase->entries[objectIndex]->y;
						destination->position.z = Levels::g_objectListBase->entries[objectIndex]->z;
						destination->objectIndex = objectIndex;
						destination->facingAngle = Levels::g_objectListBase->entries[objectIndex]->facingAngle >> 3;

						groundProbe.pos.x = destination->position.x << 5;
						groundProbe.pos.z = destination->position.z << 5;
						groundProbe.pos.y = (destination->position.y - 10) << 5;
						destination->groundHeight = Nu3D::Collision::GetGroundHeightEx(&groundProbe, 0, 0) >> 5;
						if (destination->groundHeight == 0x7FFF)
							destination->groundHeight = 0x7FFE;
						if (destination->groundHeight - destination->position.y > 0x1800)
							destination->groundHeight = 0x7FFF;

						destination++;
						g_pickupTable.recordCount++;
					}
				}
			}
		}

		// FUNCTION: TOY2 0x004025C0 [PROVISIONAL]
		void LoadTokenTable(const TokenDialogueValue* values)
		{
			memset(g_tokenDialogueEntries, 0, sizeof(g_tokenDialogueEntries));

			TokenDialogueEntry* destination = g_tokenDialogueEntries;
			do
			{
				int32_t tokenId = values++->number;
				if (tokenId < 0)
					return;

				destination->tokenId = tokenId;
				destination->dialogueRecordIndex = values++->number;
				destination->subtitle = values++->text;
				destination->facingAngle = values++->number;
				destination++;
			} while (destination < &g_tokenDialogueEntries[10]);
		}

		// FUNCTION: TOY2 0x00402610 [TOOL]
		void Token(int32_t tokenId)
		{
			if (Nu3D::Camera::g_viewHistoryInitialized != 0)
				return;

			if (Camera::g_scriptedCameraState != 0)
			{
				if (Camera::g_cameraMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
				{
					Camera::g_cameraMarkerParticle->lifetime = 1;
					Camera::g_cameraMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
				}
				if (Camera::g_targetMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
				{
					Camera::g_targetMarkerParticle->lifetime = 1;
					Camera::g_targetMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
				}

				g_buzzActor.actorFlags |= 1;
				int32_t cameraY = g_buzzActor.posAngles.pos.y - 0x3000;
				Camera::g_gameplayCamera.roll = g_buzzActor.posAngles.angles.yaw;
				g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
				Camera::g_gameplayCamera.angles.yaw = 0x4B0;
				Camera::g_gameplayCamera.position.view.pos.y = cameraY;
				Camera::g_gameplayCamera.target.view.visorAimAngles.pitch = 0;
				Camera::g_gameplayCamera.position.view.lookAt.x = Camera::g_gameplayCamera.position.view.pos.x;
				Camera::g_gameplayCamera.state.fields.modeTransitionState = 0;
				Camera::g_scriptedCameraState = 0;

				Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
			}

			int32_t entryIndex = 0;
			for (; entryIndex < 10; entryIndex++)
			{
				if (g_tokenDialogueEntries[entryIndex].tokenId == tokenId)
					break;
			}

			g_gameplayStateFlags |= GAMEPLAY_STATE_CUTSCENE_ACTIVE;
			Nu3D::Camera::g_viewHistoryInitialized = 1;
			Camera::g_cutsceneElapsedTime = 0;
			Camera::g_cutsceneCommandCursor = g_tokenCutsceneScript;
			Camera::g_cutsceneWaitTimer = 0;
			Camera::g_cutsceneSegmentProgress = 0;
			Camera::g_cutsceneMoveSpeed = 0;
			Camera::g_nextCutsceneMoveSpeed = 0;
			Camera::g_cutsceneSegmentDuration = 0;
			Camera::g_cutsceneFocusPathPoint = 0;
			Camera::g_cutsceneCameraPathPoint = 0;
			Camera::g_cutsceneRecordType = g_tokenDialogueEntries[entryIndex].dialogueRecordIndex;

			char* subtitle = g_tokenDialogueEntries[entryIndex].subtitle;
			if (subtitle != 0)
			{
				Dialogue::WrapSubtitleText(subtitle);
				Dialogue::g_subtitleActive = 1;
				for (int32_t i = 0; i < 36; i++)
					Dialogue::g_subtitleCells.pairs[i] = 0x00200020;
				Dialogue::g_subtitleTextCursor = Dialogue::g_wrappedSubtitleText;
				Dialogue::g_subtitleColumn = 0;
				Dialogue::g_subtitleRowStart = 0;
				Dialogue::g_subtitleVisibleStart = -72;
				Dialogue::g_subtitleColour = 0x00808080;
				Dialogue::g_subtitleCharacterDelay = 2;
				Dialogue::g_subtitleCharacterStyle = 0;
				Dialogue::g_subtitleBoxScale = 0;
				Dialogue::g_subtitlePageState = 0;
				AudioManager::PlaySoundEffect(0x1D, 0);
			}
			else
			{
				Dialogue::g_subtitleActive = 0;
			}

			g_buzzActor.posAngles.angles.yaw = (uint16_t)g_tokenDialogueEntries[entryIndex].facingAngle;
			g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
			Camera::g_gameplayCamera.roll = g_buzzActor.posAngles.angles.yaw;
		}

		// FUNCTION: TOY2 0x004A0DB0 [PROVISIONAL]
		void Activate(int32_t tokenIndex, int32_t skipCutscene)
		{
			if (g_tokenStates[tokenIndex].active != 0)
				return;

			g_tokenStates[tokenIndex].active = 1;
			int32_t* verticalPosition = g_tokenStates[tokenIndex].verticalPosition;
			Vector3I position;
			Nu3D::Link::GetCurrentPosFixed(g_tokenStates[tokenIndex].linkId, &position);
			*verticalPosition = position.y >> 5;

			if (skipCutscene == 0)
			{
				g_tokenStates[tokenIndex].timer = 0x84;
				Camera::BeginScriptedCutsceneAtPoint(&position, 0xB4, 0x10);
				if (Camera::g_scriptedCameraState != 0)
					Camera::g_scriptedCameraState = 5;
			}
			else
			{
				Nu3D::Link::SetScaleFromFixedOffsets(g_tokenStates[tokenIndex].linkId, 0x1000, 0x1000, 0x1000);
			}
		}

		// FUNCTION: TOY2 0x004A0E60 [MATCHED]
		void Deactivate(int32_t tokenIndex)
		{
			g_tokenStates[tokenIndex].active = 0;
			*g_tokenStates[tokenIndex].verticalPosition = INT_MIN;
			Nu3D::Link::SetScaleFromFixedOffsets(g_tokenStates[tokenIndex].linkId, 0, 0, 0);
		}

		// FUNCTION: TOY2 0x004A50D0 [MATCHED]
		void CosmicShield(Buzz::GadgetPickup* pickup)
		{
			if (g_activeCosmicShieldPickup != 0)
			{
				g_activeCosmicShieldPickup->position.y = g_savedCosmicShieldPickupY;
				Nu3D::Link::SetScaleFromFixedOffsets(g_activeCosmicShieldPickup->linkId, 0x2000, 0x2000, 0x2000);
				Nu3D::Link::SetPositionRawAndCommit(g_activeCosmicShieldPickup->linkId,
					g_activeCosmicShieldPickup->position.x,
					g_activeCosmicShieldPickup->position.y,
					g_activeCosmicShieldPickup->position.z);
				g_activeCosmicShieldPickup = 0;
			}

			g_savedCosmicShieldPickupY = pickup->position.y;
			g_activeCosmicShieldPickup = pickup;
			g_buzzActor.cosmicShieldTimer = 900;
			Nu3D::Link::SetScaleFromFixedOffsets(pickup->linkId, 0x3000, 0x3000, 0x3000);
		}

		// FUNCTION: TOY2 0x004CD110 [MATCHED]
		int32_t ShowTokenSparkle(int32_t linkId) { return 1; }
	}
}

#include "Toy2/Collectables.h"
#include "Toy2/Buzz.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Levels.h"
#include "Toy2/Toy2.h"
#include "Nu3D/Link.h"

#include <limits.h>
#include <string.h>

namespace Toy2
{
	namespace Collectables
	{
		// STUB: TOY2 0x004A0C80
		void Init(int32_t, int32_t) {}

		// GLOBAL: TOY2 0x00830CCC
		TokenState g_tokenStates[5];

		// GLOBAL: TOY2 0x0050A150
		TokenDialogueEntry g_tokenDialogueEntries[10];

		// GLOBAL: TOY2 0x005546B8
		PickupTable g_pickupTable;

		// GLOBAL: TOY2 0x00556FAC
		Levels::RecordData* g_originalPickupRecords;

		// FUNCTION: TOY2 0x00447DB0
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

			if (g_originalPickupRecords == 0)
			{
				g_pickupTable.recordType = 0;
				g_pickupTable.recordCount = 0;
			}
			else
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

		// FUNCTION: TOY2 0x004025C0
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

		// FUNCTION: TOY2 0x004A0DB0
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

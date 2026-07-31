#pragma once

#include "Common.h"
#include "Numerics.h"

namespace Toy2
{
	namespace Buzz
	{
		struct GadgetPickup;
	}

	namespace Collectables
	{
		enum PickupFlags
		{
			PICKUP_FLAG_PERSISTENT = 0x80,
		};

		enum TokenCollectionState
		{
			TOKEN_COLLECTION_STATE_CUTSCENE = 9,
		};

		struct PickupRecord
		{
			Vector3I position;
			uint8_t objectIndex;
			uint8_t facingAngle;
			int16_t groundHeight;
		};

		struct PickupTable
		{
			uint16_t recordCount;
			uint16_t recordType;
			PickupRecord records[112];
		};

		struct TokenState
		{
			int32_t linkId;
			int32_t active;
			int32_t* verticalPosition;
			int32_t timer;
		};

		struct TokenDialogueEntry
		{
			int32_t tokenId;
			int32_t dialogueRecordIndex;
			char* subtitle;
			int32_t facingAngle;
		};

		union TokenDialogueValue
		{
			int32_t number;
			char* text;
		};

		extern TokenState g_tokenStates[5];
		extern TokenDialogueEntry g_tokenDialogueEntries[10];
		extern int32_t g_exitLevelAfterToken;
		extern int32_t g_tokenCollectionState;

		void Init(int16_t* tokenLinkIds, int32_t firstHiddenLinkId);
		void BuildPickupTable();
		void LoadTokenTable(const TokenDialogueValue* values);
		void Activate(int32_t tokenIndex, int32_t skipCutscene);
		void Deactivate(int32_t tokenIndex);
		void Interactions();
		void CosmicShield(Buzz::GadgetPickup* pickup);
		int32_t ShowTokenSparkle(int32_t linkId);

		STATIC_ASSERT(sizeof(TokenState) == 0x10);
		STATIC_ASSERT(sizeof(TokenDialogueEntry) == 0x10);
		STATIC_ASSERT(sizeof(TokenDialogueValue) == 0x4);
		STATIC_ASSERT(sizeof(PickupRecord) == 0x10);
		STATIC_ASSERT(sizeof(PickupTable) == 0x704);
	}
}

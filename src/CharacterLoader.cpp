#include "CharacterLoader.h"

#include "Nullsub.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"

#include <string.h>

namespace CharacterLoader
{
	// GLOBAL: TOY2 0x0053EEE0
	BoneTransform g_boneTransforms[300];

	// GLOBAL: TOY2 0x0054717C
	int16_t g_boneRemapCount;

	// GLOBAL: TOY2 0x00546D78
	int16_t g_processedBoneRemapCount;

	// GLOBAL: TOY2 0x00547CD0
	int16_t g_animationSlotCount;

	// GLOBAL: TOY2 0x00547CD4
	CharacterAnimationData* g_characterAnimationData[128];

	// GLOBAL: TOY2 0x0053E4C8
	int32_t g_alternateAllParse[128];

	// GLOBAL: TOY2 0x0053E8C8
	uint8_t* g_charFileDataCache[128];

	// GLOBAL: TOY2 0x004F6EA0
	Toy2::Actor::ActorCollisionVolume g_defaultCollisionVolume = { { 0, -250, 0 }, 1, { 512, 256, 512 }, 250 };

	// GLOBAL: TOY2 0x0053E6C8
	Toy2::Actor::ActorCollisionVolume* g_collisionVolumes[128];

	// GLOBAL: TOY2 0x0053EAC8
	ActorBounds g_actorBounds[128];

	// GLOBAL: TOY2 0x00547188
	uint8_t* g_animationDataBySlot[512];

	// STUB: TOY2 0x0043B0C0
	void LoadCharacterData(int32_t* loadedByteCount, uint8_t** dataBuffer, uint8_t* creatureList) {}

	// FUNCTION: TOY2 0x0043B9B0 [PROVISIONAL]
	void Start(int32_t* loadedByteCount, uint8_t** dataBuffer, uint8_t* creatureList)
	{
		Toy2::Animation::g_singleNodeIndex = -1;
		Nullsub9();
		InitGlobals();

		memset(g_boneTransforms, 0, sizeof(g_boneTransforms));

		for (int32_t collisionCreatureId = 0; collisionCreatureId < 128; ++collisionCreatureId)
			g_collisionVolumes[collisionCreatureId] = &g_defaultCollisionVolume;

		memset(g_animationDataBySlot, 0, sizeof(g_animationDataBySlot));
		memset(g_characterAnimationData, 0, sizeof(g_characterAnimationData));

		for (int32_t creatureId = 0; creatureId < 128; ++creatureId)
		{
			g_actorBounds[creatureId].offset.x = 0;
			g_actorBounds[creatureId].offset.y = 500;
			g_actorBounds[creatureId].offset.z = 0;
			g_actorBounds[creatureId].radius = 500;
		}

		LoadCharacterData(loadedByteCount, dataBuffer, creatureList);
	}

	// FUNCTION: TOY2 0x0043C700 [MATCHED]
	void InitGlobals()
	{
		g_boneRemapCount = 0;
		g_processedBoneRemapCount = 0;
		g_animationSlotCount = 0;
	}
}

#include "CharacterLoader.h"

#include "Nullsub.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"

#include <string.h>

namespace Toy2
{
	namespace Characters
	{
		// STUB: TOY2 0x0043D820
		int32_t LoadAll(char* filename, int32_t baseBoneIndex, uint8_t** dataBuffer) { return 0; }
	}
}

namespace CharacterLoader
{
	struct AnimationDataLink
	{
		int32_t slot;
		uint8_t data[1];

		AnimationDataLink* Previous() { return reinterpret_cast<AnimationDataLink**>(this)[-1]; }
	};

	namespace
	{
		struct AlternateAllFileHeader
		{
			int32_t recordOffsetInWords;
		};

		union AlternateAllReference
		{
			int32_t marker;
			uint8_t* previous;
		};

		struct AlternateAllRecord
		{
			int32_t recordCount;
			int32_t dataSizeInWords;
			Vector3I translation;
			int32_t nodeType;
			uint8_t reservedBeforeAlternateDataType[6];
			int16_t alternateDataType;
			uint8_t reservedBeforeSpecialTrackValues[16];
			union
			{
				int32_t specialTrackValues[2];
				struct
				{
					uint8_t reservedBeforeMetadataX[6];
					int16_t metadataX;
				};
			};
			uint8_t reservedBeforeMetadataZ[6];
			int16_t metadataZ;
			uint8_t reservedBeforeMetadataY[6];
			int16_t metadataY;
			uint8_t reservedBeforeFlags[2];
			uint16_t flags;
		};

		STATIC_ASSERT(sizeof(AlternateAllRecord) == 0x4C);
		STATIC_ASSERT(offsetof(AlternateAllRecord, dataSizeInWords) == 0x04);
		STATIC_ASSERT(offsetof(AlternateAllRecord, translation) == 0x08);
		STATIC_ASSERT(offsetof(AlternateAllRecord, nodeType) == 0x14);
		STATIC_ASSERT(offsetof(AlternateAllRecord, alternateDataType) == 0x1E);
		STATIC_ASSERT(offsetof(AlternateAllRecord, specialTrackValues) == 0x30);
		STATIC_ASSERT(offsetof(AlternateAllRecord, metadataX) == 0x36);
		STATIC_ASSERT(offsetof(AlternateAllRecord, metadataZ) == 0x3E);
		STATIC_ASSERT(offsetof(AlternateAllRecord, metadataY) == 0x46);
		STATIC_ASSERT(offsetof(AlternateAllRecord, flags) == 0x4A);
	}

	// GLOBAL: TOY2 0x0053EEE0
	BoneTransform g_boneTransforms[300];

	// GLOBAL: TOY2 0x0054717C
	int16_t g_boneRemapCount;

	// GLOBAL: TOY2 0x00547BB8
	int16_t g_boneRemapIndices[140];

	// GLOBAL: TOY2 0x00546D78
	int16_t g_processedBoneRemapCount;

	// GLOBAL: TOY2 0x00547CD0
	int16_t g_animationSlotCount;

	// GLOBAL: TOY2 0x00547CD4
	CharacterAnimationData* g_characterAnimationData[128];

	// GLOBAL: TOY2 0x0053E4C8
	uint8_t* g_alternateAllParse[128];

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

	// GLOBAL: TOY2 0x00546D90
	AnimationDataLink* g_animationDataLinkHead;

	// GLOBAL: TOY2 0x00546DF8
	uint8_t* g_remapDataLinkHead;

	// GLOBAL: TOY2 0x00547180
	int32_t g_specialTrackValues[2];

	// GLOBAL: TOY2 0x0054697C
	uint8_t* g_allDataReferences[512];

	// STUB: TOY2 0x0043B0C0
	void LoadCharacterData(int32_t* loadedByteCount, uint8_t** dataBuffer, uint8_t* creatureList) {}

	// FUNCTION: TOY2 0x0043ABC0 [PROVISIONAL]
	void LoadFirstSection(CharacterAnimationData* animationData, int8_t collectBoneRemaps)
	{
		int16_t clipIndex;
		int16_t nodeIndex;
		int16_t remapCount;
		Toy2::Animation::ClipHeader* firstClip = 0;
		for (clipIndex = 0; clipIndex < animationData->clipCount; ++clipIndex)
		{
			CharacterAnimationData::ClipReference* clipReference = &animationData->clips[clipIndex];
			if (clipReference->offset != 0)
			{
				clipReference->pointer = reinterpret_cast<Toy2::Animation::ClipHeader*>(reinterpret_cast<uint8_t*>(animationData) + clipReference->offset);
				if (firstClip == 0)
					firstClip = clipReference->pointer;
			}
		}

		if (firstClip == 0 || ! collectBoneRemaps)
			return;

		if (firstClip->headerSize < 0)
		{
			Toy2::Animation::g_clipHeaderSize = -firstClip->headerSize;
			Toy2::Animation::g_clipHasNegativeHeader = 1;
		}
		else
		{
			Toy2::Animation::g_clipHasNegativeHeader = 0;
			Toy2::Animation::g_clipHeaderSize = sizeof(Toy2::Animation::ClipHeader) - 4;
		}

		int16_t* nodeOffsets = reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(firstClip) + Toy2::Animation::g_clipHeaderSize);
		Toy2::Animation::g_clipNodeOffsets = nodeOffsets;
		Toy2::Animation::g_clipScaleFlags = reinterpret_cast<uint8_t*>(nodeOffsets) + firstClip->nodeOffsetCount * sizeof(int16_t);

		remapCount = g_boneRemapCount;
		for (nodeIndex = 0; nodeIndex < firstClip->nodeOffsetCount; ++nodeIndex)
		{
			if (nodeOffsets[nodeIndex] == -2)
			{
				g_boneRemapIndices[remapCount] = animationData->baseBoneIndex + nodeIndex;
				++remapCount;
				g_boneRemapCount = remapCount;
			}
		}
	}

	// FUNCTION: TOY2 0x0043AED0 [TOOL]
	void LoadBuzzLight(const char* filename,
		int32_t* loadedBoneCount,
		uint8_t** dataBuffer,
		int16_t creatureId,
		Toy2::Actor::ActorCollisionVolume** collisionVolume,
		ActorBounds* actorBounds)
	{
		// Retail leaves this pointer uninitialized on the first-load path.
		CharacterAnimationData* animationData;
		int32_t animationSlotBase = creatureId * 4;
		char loadFilename[256];
		uint8_t* alternateAllData;

		if (g_characterAnimationData[creatureId] != 0)
			return;

		g_characterAnimationData[creatureId] = animationData;
		animationData->modelId = 1;
		animationData->baseBoneIndex = static_cast<int16_t>(*loadedBoneCount);

		if (g_alternateAllParse[creatureId] == 0)
		{
			strcpy(loadFilename, filename);
			*loadedBoneCount += Toy2::Characters::LoadAll(loadFilename, *loadedBoneCount, dataBuffer);
		}
		else
		{
			alternateAllData = g_alternateAllParse[creatureId];
			*loadedBoneCount += AlternateAllParse(*loadedBoneCount, &alternateAllData);
		}

		if (g_boneTransforms[*loadedBoneCount - 1].trackType == 12)
		{
			--*loadedBoneCount;
			*collisionVolume = reinterpret_cast<Toy2::Actor::ActorCollisionVolume*>(g_boneTransforms[*loadedBoneCount].animationData);
			memcpy(actorBounds, g_specialTrackValues, sizeof(*actorBounds));
		}

		animationData->endBoneIndex = static_cast<int16_t>(*loadedBoneCount);
		for (AnimationDataLink* link = g_animationDataLinkHead; link != 0; link = link->Previous())
			g_animationDataBySlot[animationSlotBase + link->slot % 4] = link->data;

		for (int32_t boneIndex = animationData->baseBoneIndex; boneIndex < animationData->endBoneIndex; ++boneIndex)
		{
			g_boneTransforms[boneIndex].translation.x = 0;
			g_boneTransforms[boneIndex].translation.y = 0;
			g_boneTransforms[boneIndex].translation.z = 0;
		}
	}

	// FUNCTION: TOY2 0x0043D6B0 [PROVISIONAL]
	int32_t AlternateAllParse(int32_t baseBoneIndex, uint8_t** dataBuffer)
	{
		int32_t loadedBoneCount = 0;
		int32_t recordIndex = 0;
		AlternateAllFileHeader* header = reinterpret_cast<AlternateAllFileHeader*>(*dataBuffer);
		int32_t recordOffsetInWords = header->recordOffsetInWords;
		AlternateAllRecord* records = reinterpret_cast<AlternateAllRecord*>(*dataBuffer + recordOffsetInWords * 2);
		*dataBuffer += sizeof(*header);
		g_remapDataLinkHead = 0;
		g_animationDataLinkHead = 0;

		if (records->recordCount < 1)
			return 0;

		AlternateAllRecord* record = records;
		BoneTransform* transform = &g_boneTransforms[baseBoneIndex];
		do
		{
			int32_t nodeType = record->nodeType;
			if (nodeType < 0x100)
			{
				++loadedBoneCount;
				transform->translation.x = record->translation.x;
				transform->translation.y = record->translation.y;
				transform->translation.z = record->translation.z;
				transform->nodeType = nodeType;
				transform->animationData = *dataBuffer;
				transform->remapMetadata.x = record->metadataX;
				transform->remapMetadata.y = record->metadataY;
				transform->remapMetadata.z = record->metadataZ;
				transform->trackType = static_cast<uint8_t>(nodeType);

				if (static_cast<int8_t>(nodeType) == 9)
				{
					g_specialTrackValues[0] = record->specialTrackValues[0];
					g_specialTrackValues[1] = record->specialTrackValues[1];
					transform->trackType = 12;
				}

				if (record->alternateDataType != 0)
				{
					if (record->alternateDataType == 1)
						transform->trackType += 4;
					else
						transform->trackType += 8;

					uint8_t* alternateData = *dataBuffer;
					while (alternateData[3] != 0)
					{
						if ((alternateData[3] & 0x7C) == 0x34)
							alternateData += 0x24;
						else if ((alternateData[3] & 0x7C) == 0x3C)
							alternateData += 0x30;
					}
					transform->alternateData = alternateData + 4;
				}

				if ((record->flags & 0x40) == 0x40)
					transform->trackType += 2;

				++transform;
			}
			else
			{
				AlternateAllReference* reference = reinterpret_cast<AlternateAllReference*>(*dataBuffer);
				uint8_t* referenceData = reinterpret_cast<uint8_t*>(reference);
				if (reference->marker == 0x12345678)
				{
					reference->previous = g_allDataReferences[nodeType];
					referenceData = reinterpret_cast<uint8_t*>(reference + 1);
				}
				g_allDataReferences[nodeType] = referenceData;
			}

			*dataBuffer += record->dataSizeInWords * 2;
			++recordIndex;
			++record;
		} while (recordIndex < records->recordCount);

		return loadedBoneCount;
	}

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

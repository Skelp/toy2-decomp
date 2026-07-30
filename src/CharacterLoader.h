#pragma once

#include "Common.h"
#include "Numerics.h"

#include <stddef.h>

namespace Toy2
{
	namespace Actor
	{
		struct ActorCollisionVolume;
	}

	namespace Animation
	{
		struct ClipHeader;
	}

	namespace Characters
	{
		int32_t LoadAll(char* filename, int32_t baseBoneIndex, uint8_t** dataBuffer);
	}
}

namespace CharacterLoader
{
	struct ActorBounds
	{
		Vector3I16 offset;
		int16_t radius;
	};

	struct CharacterAnimationData
	{
		union ClipReference
		{
			uint32_t offset;
			Toy2::Animation::ClipHeader* pointer;
		};

		int16_t modelId;
		int16_t clipCount;
		int16_t baseBoneIndex;
		int16_t endBoneIndex;
		ClipReference clips[1];
	};

	struct BoneTransform
	{
		Vector3I translation;
		union
		{
			int32_t nodeType;
			uint8_t* alternateData;
		};
		Vector3I16 rotationAngles;
		int16_t unkInt6_;
		int16_t scaleX;
		int16_t scaleY;
		int16_t scaleZ;
		uint8_t hasScale;
		uint8_t track;
		uint8_t* animationData;
		int32_t unkInt10;
		Vector3I16 remapMetadata;
		int16_t unkInt11;
		int32_t unkInt13;
		int16_t remapSlot;
		uint8_t trackType;
		uint8_t trackTypePadding;
		int32_t unkInt15;
		int32_t unkInt16;
		int32_t unkInt17;
		int32_t unkInt18;
		int32_t unkInt19;
		int32_t unkInt20;
		int32_t unkInt21;
		int32_t unkInt22;
		Matrix3x3I16 rotation;
		int16_t unkInt27_;
	};

	extern BoneTransform g_boneTransforms[300];
	extern int16_t g_boneRemapCount;
	extern int16_t g_boneRemapIndices[140];
	extern int16_t g_processedBoneRemapCount;
	extern int16_t g_animationSlotCount;
	extern CharacterAnimationData* g_characterAnimationData[128];
	extern uint8_t* g_alternateAllParse[128];
	extern CharacterAnimationData* g_charFileDataCache[128];
	extern Toy2::Actor::ActorCollisionVolume g_defaultCollisionVolume;
	extern Toy2::Actor::ActorCollisionVolume* g_collisionVolumes[128];
	extern ActorBounds g_actorBounds[128];
	extern uint8_t* g_animationDataBySlot[512];

	void Start(int32_t* loadedByteCount, uint8_t** dataBuffer, uint8_t* creatureList);
	void LoadFirstSection(CharacterAnimationData* animationData, int8_t collectBoneRemaps);
	void Load(const char* filename,
		int32_t* loadedBoneCount,
		uint8_t** dataBuffer,
		int16_t creatureId,
		Toy2::Actor::ActorCollisionVolume** collisionVolume,
		ActorBounds* actorBounds);
	void LoadBuzzLight(const char* filename,
		int32_t* loadedBoneCount,
		uint8_t** dataBuffer,
		int16_t creatureId,
		Toy2::Actor::ActorCollisionVolume** collisionVolume,
		ActorBounds* actorBounds);
	int32_t AlternateAllParse(int32_t baseBoneIndex, uint8_t** dataBuffer);
	void InitGlobals();

	STATIC_ASSERT(sizeof(ActorBounds) == 8);
	STATIC_ASSERT(sizeof(BoneTransform) == 0x6C);
	STATIC_ASSERT(offsetof(BoneTransform, nodeType) == 0x0C);
	STATIC_ASSERT(offsetof(BoneTransform, animationData) == 0x20);
	STATIC_ASSERT(offsetof(BoneTransform, remapMetadata) == 0x28);
	STATIC_ASSERT(offsetof(BoneTransform, remapSlot) == 0x34);
	STATIC_ASSERT(offsetof(BoneTransform, trackType) == 0x36);
	STATIC_ASSERT(sizeof(CharacterAnimationData) == 0xC);
	STATIC_ASSERT(sizeof(CharacterAnimationData::ClipReference) == 4);
	STATIC_ASSERT(offsetof(CharacterAnimationData, modelId) == 0);
	STATIC_ASSERT(offsetof(CharacterAnimationData, clipCount) == 2);
	STATIC_ASSERT(offsetof(CharacterAnimationData, baseBoneIndex) == 4);
	STATIC_ASSERT(offsetof(CharacterAnimationData, endBoneIndex) == 6);
	STATIC_ASSERT(offsetof(CharacterAnimationData, clips) == 8);
}

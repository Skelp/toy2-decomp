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
		int16_t modelId;
		int16_t reserved;
		int16_t baseBoneIndex;
		int16_t reserved2;
		Toy2::Animation::ClipHeader* clips[1];
	};

	struct BoneTransform
	{
		Vector3I translation;
		int32_t unkInt4;
		Vector3I16 rotationAngles;
		int16_t unkInt6_;
		int16_t scaleX;
		int16_t scaleY;
		int16_t scaleZ;
		uint8_t hasScale;
		uint8_t track;
		int32_t unkInt9;
		int32_t unkInt10;
		Vector3I16 unkVec5;
		int16_t unkInt11;
		int32_t unkInt13;
		int16_t unkInt14;
		int16_t unkInt14_;
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
	extern int16_t g_processedBoneRemapCount;
	extern int16_t g_animationSlotCount;
	extern CharacterAnimationData* g_characterAnimationData[128];
	extern int32_t g_alternateAllParse[128];
	extern uint8_t* g_charFileDataCache[128];
	extern Toy2::Actor::ActorCollisionVolume g_defaultCollisionVolume;
	extern Toy2::Actor::ActorCollisionVolume* g_collisionVolumes[128];
	extern ActorBounds g_actorBounds[128];
	extern uint8_t* g_animationDataBySlot[512];

	void Start(int32_t* loadedByteCount, uint8_t** dataBuffer, uint8_t* creatureList);
	void InitGlobals();

	STATIC_ASSERT(sizeof(ActorBounds) == 8);
	STATIC_ASSERT(sizeof(BoneTransform) == 0x6C);
	STATIC_ASSERT(sizeof(CharacterAnimationData) == 0xC);
	STATIC_ASSERT(offsetof(CharacterAnimationData, modelId) == 0);
	STATIC_ASSERT(offsetof(CharacterAnimationData, baseBoneIndex) == 4);
	STATIC_ASSERT(offsetof(CharacterAnimationData, clips) == 8);
}

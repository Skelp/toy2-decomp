#pragma once

#include "Common.h"
#include "Numerics.h"

#include <stddef.h>

namespace Toy2
{
	namespace Collision
	{
		struct CollisionMeshInstance
		{
			Vector3I origin;
			int32_t platformIdx;
			int32_t collisionTree;
			Vector3I boundsMin;
			Vector3I boundsExt;
			int16_t typeFlags;
			int16_t unk;
			int32_t boundingSqRadius;
		};

		extern CollisionMeshInstance g_collisionMeshInstances[300];
		extern uint8_t g_mathScratch[1024];

		void BuildCollisionWorld(int32_t level, uint8_t** buffer, int32_t terrainNum);

		STATIC_ASSERT(sizeof(CollisionMeshInstance) == 0x34);
	}

	namespace Platform
	{
		struct PlatformState
		{
			Vector3I16 rotationAnglesFixed;
			int16_t motionState[13];
			uint8_t* contactFace;
			int16_t collisionMeshIndex;
			int16_t flags;
			uint8_t reserved[0xC];
		};

		int32_t GetFlags(int32_t platformIndex);
		void AddFlags(int32_t platformIndex, uint16_t flags);
		void ClearFlags(int32_t platformIndex, int32_t flags);
		void SetRotationAngles(int32_t platformIndex, int16_t x, int16_t y, int16_t z);
		void GetRotationAngles(int32_t platformIndex, Vector3I* angles);
		int32_t HadBuzzContactThisFrame(int32_t platformIndex);

		extern PlatformState g_platformStates[32];

		STATIC_ASSERT(offsetof(PlatformState, contactFace) == 0x20);
		STATIC_ASSERT(offsetof(PlatformState, collisionMeshIndex) == 0x24);
		STATIC_ASSERT(offsetof(PlatformState, flags) == 0x26);
		STATIC_ASSERT(sizeof(PlatformState) == 0x34);
	}
}

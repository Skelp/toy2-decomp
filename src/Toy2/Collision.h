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
		extern int16_t g_groundCollisionMeshIndex;
		extern uint8_t g_mathScratch[1024];

		void BuildCollisionWorld(int32_t level, uint8_t** buffer, int32_t terrainNum);
		void MarkPlatformAsMoving(int32_t platformIndex);
		int32_t GetGroundContactIdx();

		STATIC_ASSERT(sizeof(CollisionMeshInstance) == 0x34);
	}

	namespace Platform
	{
		struct CollisionFace
		{
			uint8_t faceData[0x20];
			Vector3I16 normal;
		};

		struct PlatformState
		{
			Vector3I16 rotationAnglesFixed;
			int16_t motionMode;
			Vector3I16 velocity;
			Vector3I16 remainingTranslation;
			Vector3I16 angularVelocity;
			Vector3I16 remainingRotation;
			CollisionFace* contactFace;
			int16_t collisionMeshIndex;
			int16_t flags;
			uint8_t reserved[0xC];
		};

		int32_t GetFlags(int32_t platformIndex);
		Vector3I16* GetContactFaceNormal(int32_t platformIndex);
		void SetOrigin(int32_t platformIndex, int32_t x, int32_t y, int32_t z);
		void AddFlags(int32_t platformIndex, uint16_t flags);
		void ClearFlags(int32_t platformIndex, int32_t flags);
		void DisableCollision(int32_t platformIndex);
		void SetVelocity(int32_t platformIndex, int32_t x, int32_t y, int32_t z);
		void SetAngularVelocity(int32_t platformIndex, int16_t x, int16_t y, int16_t z);
		void CopyVelocity(int32_t sourcePlatformIndex, int32_t destinationPlatformIndex);
		void GetOrigin(int32_t platformIndex, Vector3I* origin);
		void GetRotation(int32_t platformIndex, Vector3I* rotation);
		void SetRotationAngles(int32_t platformIndex, int16_t x, int16_t y, int16_t z);
		void GetRotationAngles(int32_t platformIndex, Vector3I* angles);
		void CommitRotationToLink(int32_t platformIndex, int32_t linkId);
		int32_t HadBuzzContactThisFrame(int32_t platformIndex);

		extern PlatformState g_platformStates[32];

		STATIC_ASSERT(offsetof(PlatformState, velocity) == 0x8);
		STATIC_ASSERT(offsetof(CollisionFace, normal) == 0x20);
		STATIC_ASSERT(offsetof(PlatformState, remainingTranslation) == 0xE);
		STATIC_ASSERT(offsetof(PlatformState, angularVelocity) == 0x14);
		STATIC_ASSERT(offsetof(PlatformState, remainingRotation) == 0x1A);
		STATIC_ASSERT(offsetof(PlatformState, contactFace) == 0x20);
		STATIC_ASSERT(offsetof(PlatformState, collisionMeshIndex) == 0x24);
		STATIC_ASSERT(offsetof(PlatformState, flags) == 0x26);
		STATIC_ASSERT(sizeof(PlatformState) == 0x34);
	}
}

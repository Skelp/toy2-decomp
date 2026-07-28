#pragma once

#include "Common.h"
#include "Numerics.h"

#include <stddef.h>

namespace Toy2
{
	namespace Platform
	{
		struct CollisionFace;
	}

	namespace Collision
	{
		enum CollisionMeshType
		{
			COLLISION_MESH_MOVING = 8,
		};

		enum CollisionContactFlags
		{
			COLLISION_CONTACT_MESH_INDEX_MASK = 0x7FF,
			COLLISION_CONTACT_SECONDARY_FACE = 0x4000,
		};

		struct MathScratchVector
		{
			Vector3I value;
			int32_t reserved;
		};

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

		struct CollisionQueryResult
		{
			uint32_t contactFlags;
			Platform::CollisionFace* face;
			uint8_t reserved[0x28];
		};

		extern CollisionMeshInstance g_collisionMeshInstances[300];
		extern CollisionQueryResult g_collisionQueryResults[2];
		extern int16_t g_groundCollisionMeshIndex;
		extern MathScratchVector g_mathScratch[64];

		void BuildCollisionWorld(int32_t level, uint8_t** buffer, int32_t terrainNum);
		void MarkPlatformAsMoving(int32_t platformIndex);
		bool IsMeshMoving();
		int32_t IsSafeFooting(int32_t queryIndex, const uint8_t* contactState);
		int32_t GetGroundContactIdx();

		STATIC_ASSERT(sizeof(CollisionMeshInstance) == 0x34);
		STATIC_ASSERT(sizeof(CollisionQueryResult) == 0x30);
		STATIC_ASSERT(sizeof(MathScratchVector) == 0x10);
	}

	namespace Platform
	{
		struct CollisionFace
		{
			uint8_t faceData[0x20];
			Vector3I16 normal;
			Vector3I16 secondaryNormal;
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

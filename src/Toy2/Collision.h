#pragma once

#include "Common.h"
#include "Numerics.h"

#include <stddef.h>

namespace Toy2
{
	namespace Shadow
	{
		void ResetShadowCount();
		void QueueStretched(int32_t x, int32_t groundY, int32_t z, int32_t size, int32_t sourceY);
		void QueueStretchedForBuzz(int32_t x, int32_t groundY, int32_t z, int32_t size);
	}

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
			uint8_t reserved[0x20];
			int16_t platformIndex;
			uint8_t reserved2[4];
			uint16_t surfaceType;
		};

		extern CollisionMeshInstance g_collisionMeshInstances[300];
		extern CollisionQueryResult g_collisionQueryResults[2];
		extern int16_t g_groundCollisionMeshIndex;
		extern Vector3I16 g_groundNormal;
		extern Vector3I16 g_buzzGroundNormal;
		extern int32_t g_groundPlatformIndex;
		extern int32_t g_collisionTriangleCount;
		extern int32_t g_collisionEdgeVertexCount;
		extern Vector3I g_collisionEdgeVertices[32];
		extern MathScratchVector g_mathScratch[64];

		void BuildCollisionWorld(int32_t level, uint8_t** buffer, int32_t terrainNum);
		void MarkPlatformAsMoving(int32_t platformIndex);
		bool IsMeshMoving();
		int32_t IsSafeFooting(int32_t queryIndex, const uint8_t* contactState);
		int32_t GetGroundContactIdx();
		int32_t SweepAndSlide(Vector3I* position, Vector3I* movement, int32_t collisionThreshold, int16_t* collisionAngles, int32_t radius);
		void GatherTrianglesAtXZ(const Vector3I* position);
		void ResolveGroundCeiling(PosAndAngles* position, int32_t radius);

		STATIC_ASSERT(sizeof(CollisionMeshInstance) == 0x34);
		STATIC_ASSERT(sizeof(CollisionQueryResult) == 0x30);
		STATIC_ASSERT(offsetof(CollisionQueryResult, platformIndex) == 0x28);
		STATIC_ASSERT(offsetof(CollisionQueryResult, surfaceType) == 0x2E);
		STATIC_ASSERT(sizeof(MathScratchVector) == 0x10);
	}

	namespace Platform
	{
		enum PlatformFlags
		{
			PLATFORM_FLAG_BUZZ_CONTACT = 0x2,
			PLATFORM_FLAG_ROTATED = 0x8,
			PLATFORM_FLAG_BUZZ_GROUNDED = 0x10,
		};

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

namespace Nu3D
{
	namespace Collision
	{
		int16_t IsPointInTriangle(int32_t pointX,
			int32_t pointY,
			int32_t pointZ,
			int32_t edge1X,
			int32_t edge1Y,
			int32_t edge1Z,
			int32_t edge2X,
			int32_t edge2Y,
			int32_t edge2Z,
			const Vector3I16* normal);
		int32_t RaycastAgainstEdges(int32_t* nearestFraction,
			int32_t* startDistance,
			int32_t* endDistance,
			Vector3I16* hitNormal,
			uint16_t* reversed,
			const Vector3I* start,
			const Vector3I* movement);
	}
}

namespace Nu3D
{
	namespace Collision
	{
		int32_t IsFloorWalkable();
		int32_t GetSurfaceQuality(int32_t queryIndex);
		int32_t GetGroundHeight(const PosAndAngles* position, int32_t shadowSize);
		int32_t GetGroundHeightEx(const PosAndAngles* position, int32_t shadowSize, int32_t probeRadius);
	}
}

#pragma once

#include "Toy2/Collision.h"

namespace Toy2
{
	namespace Collision
	{
		struct SurfaceCollisionResult
		{
			uint32_t contactFlags;
			Platform::CollisionFace* face;
			Vector3I16 normal;
			int16_t contactState;
			Vector3I16 wallNormal;
			int16_t reserved;
			Vector3I16 movement;
			Vector3I16 surfaceVelocity;
			int32_t contactTimer;
			int16_t platformIndex;
			uint8_t reserved2[2];
			int16_t collisionDistance;
			uint16_t surfaceType;
		};

		struct PackedCollisionFace
		{
			int16_t boundsMinX;
			int16_t boundsExtentX;
			int8_t boundsMinYBlock;
			int8_t boundsExtentYBlock;
			int8_t boundsMinZBlock;
			int8_t boundsExtentZBlock;
			Vector3I16 vertex0;
			Vector3I16 vertex1Offset;
			Vector3I16 vertex2Offset;
			Vector3I16 vertex3Offset;
			Vector3I16 firstPlaneNormal;
			Vector3I16 secondPlaneNormal;
		};

		struct CollisionTreeGroup
		{
			int16_t marker;
			int16_t faceCount;
			int16_t boundsMinX;
			int16_t boundsExtentX;
			int16_t boundsMinZ;
			int16_t boundsExtentZ;
		};

		struct CollisionGridCell
		{
			int16_t meshListStart;
			int16_t meshCount;
			int32_t boundsMinX;
			int32_t boundsMinZ;
			int32_t boundsExtentX;
			int32_t boundsExtentZ;
		};

		struct CollisionStepMotion
		{
			Vector3I16 movement;
			int16_t reserved;
			Vector3I16 platformMovement;
		};

		extern int32_t g_activeCollisionGridCellCount;
		extern CollisionGridCell g_collisionGrid[257];
		extern int16_t g_collisionGridMeshIndices[600];
		extern uint16_t g_collisionPassFlags;
		extern PackedCollisionFace* g_collisionTriangles[240];
		extern int16_t g_collisionTriangleMeshIndices[240];
		extern PackedCollisionFace* g_rotatedCollisionTriangles[32];
		extern int16_t g_rotatedCollisionTriangleMeshIndices[32];
		extern int32_t g_rotatedCollisionTriangleCount;
		extern int32_t g_hadGroundResponse;
		extern int32_t g_collisionWorldMaxX;

		int32_t ResolveSubstep(CollisionStepMotion* motion,
			Vector3I* position,
			Vector3I* velocity,
			SurfaceCollisionResult* result,
			const int16_t* meshIndices,
			PackedCollisionFace** faces,
			int32_t faceCount,
			int32_t movePlatforms);
		int32_t ResolvePlatformFooting(CollisionStepMotion* motion, Vector3I* position, Vector3I* velocity, SurfaceCollisionResult* result);

		STATIC_ASSERT(sizeof(SurfaceCollisionResult) == sizeof(CollisionQueryResult));
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, normal) == 0x08);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, contactState) == 0x0E);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, wallNormal) == 0x10);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, movement) == 0x18);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, surfaceVelocity) == 0x1E);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, contactTimer) == 0x24);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, collisionDistance) == 0x2C);
		STATIC_ASSERT(sizeof(PackedCollisionFace) == 0x2C);
		STATIC_ASSERT(offsetof(PackedCollisionFace, firstPlaneNormal) == 0x20);
		STATIC_ASSERT(offsetof(PackedCollisionFace, secondPlaneNormal) == 0x26);
		STATIC_ASSERT(sizeof(CollisionTreeGroup) == 0x0C);
		STATIC_ASSERT(sizeof(CollisionGridCell) == 0x14);
		STATIC_ASSERT(sizeof(CollisionStepMotion) == 0x0E);
	}
}

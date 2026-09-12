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

		struct CollisionNormal
		{
			Vector3I16 direction;
			int16_t reserved;
		};
		STATIC_ASSERT(sizeof(CollisionNormal) == 0x08);
		STATIC_ASSERT(offsetof(CollisionNormal, reserved) == 0x06);

		// One motion tested against the collision mesh. The sweep solver fills the
		// nearest hit and the mesh query reads it back.
		struct CollisionSweep
		{
			Vector3I start;
			int32_t reservedStart;
			Vector3I end;
			int32_t reservedEnd;
			int32_t nearestFraction;
			int32_t startDistance;
			int32_t endDistance;
			uint32_t contactFlags;
			PackedCollisionFace* face;
			CollisionNormal hitNormal;
			MathScratchVector movement; // unit direction (value) and length (scalar) of the sweep
		};

		// The fraction a sweep reports when nothing was hit.
		const uint32_t COLLISION_NO_CONTACT = 0xFFFFFFFF;

		// A platform that moving bodies may stand on.
		const int16_t PLATFORM_FLAG_COLLISION_SUPPORT = 0x1;

		// The sweep solver and the mesh query both round toward zero this way.
		static __forceinline int32_t ShiftTowardZero(int32_t value, int32_t bits) { return (value + ((value >> 31) & ((1 << bits) - 1))) >> bits; }

		// Entry points the two units call across the split.
		int32_t SweepWallSegments(CollisionSweep* sweep, const Vector3I* start, const Vector3I* movement, int32_t radius);
		// One mesh of the collision world, as the query reads it.
		struct CollisionMeshRecord
		{
			Vector3I origin;
			int32_t platformIdx;
			CollisionTreeGroup* collisionTree;
			Vector3I boundsMin;
			Vector3I boundsExt;
			int16_t typeFlags;
			uint16_t flags;
			int32_t boundingSqRadius;
		};

		const int16_t COLLISION_MESH_STATIC_A = 6;
		const int16_t COLLISION_MESH_STATIC_B = 7;
		const uint16_t COLLISION_MESH_EXCLUDE_FROM_GRID = 0x400;
		const uint16_t COLLISION_MESH_EXCLUDE_FROM_QUERY = 0x100;

	}

	// The workspace that the collision query fills: nine slots, each caching the
	// meshes and the faces near one query position. ShadowGround.cpp reads the
	// slots that Collision.cpp fills, so both units need the layout.
	namespace Terrain
	{
		struct CollisionCachePosition
		{
			int32_t cachedPositionX;
			uint8_t reserved[4];
			int32_t cachedPositionZ;
			uint8_t reserved2[12];
		};

		struct CollisionQueryBounds
		{
			Vector3I maximum;
			Vector3I minimum;
		};

		struct CollisionWorkspaceSlot
		{
			union
			{
				CollisionCachePosition cachePosition;
				CollisionQueryBounds queryBounds;
			};
			int16_t activeFrames;
			uint8_t reserved3[2];
			int32_t entryCount;
			int16_t meshIndices[240];
			Collision::PackedCollisionFace* faces[240];
		};

		struct CollisionWorkspace
		{
			CollisionWorkspaceSlot slots[9];
		};

		STATIC_ASSERT(sizeof(CollisionCachePosition) == 0x18);
		STATIC_ASSERT(sizeof(CollisionQueryBounds) == 0x18);
		STATIC_ASSERT(sizeof(CollisionWorkspaceSlot) == 0x5C0);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, activeFrames) == 0x18);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, entryCount) == 0x1C);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, meshIndices) == 0x20);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, faces) == 0x200);
		STATIC_ASSERT(sizeof(CollisionWorkspace) == 0x33C0);

		extern CollisionWorkspace* g_collisionWorkspace;
	}

	namespace Platform
	{
		void AdvancePartialMotion(int32_t frameScale, int32_t collisionScale, int32_t skipMotion);
	}
}

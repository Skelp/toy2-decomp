#include "Toy2/Collision.h"
#include "Toy2/CollisionInternal.h"
#include "FileUtils.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Nu3D
{
	namespace Collision
	{
		int32_t SweepAgainstCandidates(const Vector4I* movement, Vector4I* position, int32_t radius);
	}
}

namespace Toy2
{
	extern int32_t g_ziplineState;
	extern int32_t g_ledgeClimbTimer;
	extern int32_t g_poleClimbState;
	extern int32_t g_levelFileIndex;

	namespace Platform
	{
		void AdvancePartialMotion(int32_t frameScale, int32_t collisionScale, int32_t skipMotion);
	}

	namespace Levels
	{
		extern void* g_cachedAllBuffer;
		extern int32_t g_hasZoneData;

		void BuildLevelPath(int32_t level, char* output, const char* suffix);
	}

	namespace Terrain
	{
		struct TerrainFileHeader
		{
			int32_t descriptorTableOffsetInWords;
		};

		struct TerrainMeshDescriptor
		{
			int32_t reserved;
			int32_t collisionTreeWordCount;
			Vector3I origin;
			int32_t typeOrRelocationIndex;
			uint8_t reserved2[6];
			int16_t platformId;
			uint8_t reserved3[12];
			int16_t flags;
			uint8_t reserved4[26];
			int16_t sourceMeshId;
			int16_t isClone;
		};

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

		union TerrainRelocationLink
		{
			int32_t marker;
			uint8_t* next;
		};

		STATIC_ASSERT(sizeof(TerrainFileHeader) == 0x04);
		STATIC_ASSERT(sizeof(TerrainMeshDescriptor) == 0x4C);
		STATIC_ASSERT(offsetof(TerrainMeshDescriptor, collisionTreeWordCount) == 0x04);
		STATIC_ASSERT(offsetof(TerrainMeshDescriptor, platformId) == 0x1E);
		STATIC_ASSERT(offsetof(TerrainMeshDescriptor, flags) == 0x2C);
		STATIC_ASSERT(offsetof(TerrainMeshDescriptor, sourceMeshId) == 0x48);
		STATIC_ASSERT(sizeof(CollisionCachePosition) == 0x18);
		STATIC_ASSERT(sizeof(CollisionQueryBounds) == 0x18);
		STATIC_ASSERT(sizeof(CollisionWorkspaceSlot) == 0x5C0);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, activeFrames) == 0x18);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, entryCount) == 0x1C);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, meshIndices) == 0x20);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, faces) == 0x200);
		STATIC_ASSERT(sizeof(CollisionWorkspace) == 0x33C0);
		// Relocation chain 1 holds wall edges. The chain head points after the link word.
		// Each block of 16 vertices keeps its X and Z bounds in the Y words of its first four vertices.
		struct TerrainEdgeList
		{
			uint16_t edgeCount;
			uint16_t reserved;
			Vector3I vertices[1];
		};

		STATIC_ASSERT(sizeof(TerrainRelocationLink) == 0x04);
		STATIC_ASSERT(offsetof(TerrainEdgeList, vertices) == 0x04);
		STATIC_ASSERT(sizeof(TerrainEdgeList) == 0x10);

		// GLOBAL: TOY2 0x007290F0
		uint8_t* g_terrainRelocationHeads[8];

		// GLOBAL: TOY2 0x0072871C
		CollisionWorkspace* g_collisionWorkspace;

		// FUNCTION: TOY2 0x00489980 [PROVISIONAL]
		int32_t LoadAll(char* filename, int32_t baseMeshIndex, uint8_t** buffer)
		{
			int32_t loadedMeshCount;
			int32_t descriptorIndex;
			char allFilename[100];
			int16_t sourceMeshIndices[100];

			if (Levels::g_cachedAllBuffer == NULL)
			{
				strcpy(allFilename, filename);
				strcat(allFilename, ".all");
				FileUtils::LoadFile(allFilename, *buffer);
			}
			else
			{
				*buffer = static_cast<uint8_t*>(Levels::g_cachedAllBuffer);
			}

			TerrainFileHeader* fileHeader = reinterpret_cast<TerrainFileHeader*>(*buffer);
			int32_t* descriptorTable = reinterpret_cast<int32_t*>(*buffer + fileHeader->descriptorTableOffsetInWords * 2);
			*buffer += sizeof(TerrainFileHeader);
			loadedMeshCount = 0;
			descriptorIndex = 0;

			memset(sourceMeshIndices, -1, sizeof(sourceMeshIndices));
			memset(g_terrainRelocationHeads, 0, sizeof(g_terrainRelocationHeads));

			if (*descriptorTable > 0)
			{
				TerrainMeshDescriptor* descriptor = reinterpret_cast<TerrainMeshDescriptor*>(descriptorTable);
				Collision::CollisionMeshInstance* mesh = &Collision::g_collisionMeshInstances[baseMeshIndex];

				do
				{
					if (descriptor->typeOrRelocationIndex < 256)
					{
						loadedMeshCount++;

						if (descriptor->isClone == 1)
						{
							int16_t sourceMeshIndex = sourceMeshIndices[descriptor->sourceMeshId];
							if (sourceMeshIndex >= 0)
							{
								*mesh = Collision::g_collisionMeshInstances[sourceMeshIndex];
								mesh->origin = descriptor->origin;
								mesh->platformIdx = descriptor->typeOrRelocationIndex;
								mesh->origin.x <<= 5;
								mesh->origin.y <<= 5;
								mesh->origin.z <<= 5;
							}
							else
							{
								mesh->typeFlags = 0;
							}
						}
						else
						{
							mesh->typeFlags = static_cast<int16_t>(descriptor->typeOrRelocationIndex);
							mesh->unk = descriptor->flags;
							mesh->origin.x = descriptor->origin.x << 5;
							mesh->origin.y = descriptor->origin.y << 5;
							mesh->origin.z = descriptor->origin.z << 5;
							mesh->collisionTree = reinterpret_cast<int32_t>(*buffer);

							if (descriptor->platformId != 0)
							{
								Platform::g_platformStates[descriptor->platformId - 1].collisionMeshIndex = static_cast<int16_t>(baseMeshIndex);
								Platform::g_platformStates[descriptor->platformId - 1].origin.x = mesh->origin.x;
								Platform::g_platformStates[descriptor->platformId - 1].origin.y = mesh->origin.y;
								Platform::g_platformStates[descriptor->platformId - 1].origin.z = mesh->origin.z;
								mesh->platformIdx = descriptor->platformId - 1;
							}

							if (descriptor->sourceMeshId >= 0)
							{
								sourceMeshIndices[descriptor->sourceMeshId] = static_cast<int16_t>(baseMeshIndex);
							}
						}

						baseMeshIndex++;
						mesh++;
					}
					else
					{
						TerrainRelocationLink* relocation = reinterpret_cast<TerrainRelocationLink*>(*buffer);
						if (relocation->marker == 0x12345678)
						{
							int32_t relocationIndex = descriptor->typeOrRelocationIndex - 256;
							relocation->next = g_terrainRelocationHeads[relocationIndex];
							g_terrainRelocationHeads[relocationIndex] = *buffer + sizeof(TerrainRelocationLink);
						}
					}

					*buffer += descriptor->collisionTreeWordCount * 2;
					descriptorIndex++;
					descriptor++;
				} while (descriptorIndex < *descriptorTable);
			}

			g_collisionWorkspace = reinterpret_cast<CollisionWorkspace*>(*buffer);
			*buffer += sizeof(CollisionWorkspace);

			for (int32_t slotIndex = 0; slotIndex < 9; slotIndex++)
			{
				g_collisionWorkspace->slots[slotIndex].activeFrames = 0;
				g_collisionWorkspace->slots[slotIndex].entryCount = 0;
			}

			return loadedMeshCount;
		}
	}

	namespace Collision
	{
		struct PlatformMotionCollisionResult
		{
			uint32_t contactFlags;
			Platform::CollisionFace* face;
			uint8_t reserved[0x10];
			Vector3I16 movement;
			Vector3I16 platformMovement;
			uint8_t reserved2[4];
			int16_t platformIndex;
			int16_t platformRotationState;
			int16_t reserved3;
			uint16_t surfaceType;
		};

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

		struct CollisionNormal
		{
			Vector3I16 direction;
			int16_t reserved;
		};

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
			Vector3I movementDirection;
			int32_t movementLength;
		};

		const int16_t COLLISION_MESH_STATIC_A = 6;
		const int16_t COLLISION_MESH_STATIC_B = 7;
		const uint16_t COLLISION_MESH_EXCLUDE_FROM_GRID = 0x400;
		const uint16_t COLLISION_MESH_EXCLUDE_FROM_QUERY = 0x100;
		const uint32_t COLLISION_NO_CONTACT = 0xFFFFFFFF;
		const int16_t PLATFORM_FLAG_COLLISION_SUPPORT = 0x1;

		static __forceinline int32_t ShiftTowardZero(int32_t value, int32_t bits) { return (value + ((value >> 31) & ((1 << bits) - 1))) >> bits; }

		STATIC_ASSERT(sizeof(CollisionNormal) == 0x08);
		STATIC_ASSERT(offsetof(CollisionNormal, reserved) == 0x06);
		STATIC_ASSERT(sizeof(PlatformMotionCollisionResult) == sizeof(CollisionQueryResult));
		STATIC_ASSERT(offsetof(PlatformMotionCollisionResult, movement) == 0x18);
		STATIC_ASSERT(offsetof(PlatformMotionCollisionResult, platformMovement) == 0x1E);
		STATIC_ASSERT(offsetof(PlatformMotionCollisionResult, platformRotationState) == 0x2A);
		STATIC_ASSERT(sizeof(CollisionMeshRecord) == sizeof(CollisionMeshInstance));
		STATIC_ASSERT(sizeof(CollisionSweep) == 0x4C);
		STATIC_ASSERT(offsetof(CollisionSweep, nearestFraction) == 0x20);
		STATIC_ASSERT(offsetof(CollisionSweep, hitNormal) == 0x34);
		STATIC_ASSERT(offsetof(CollisionSweep, movementDirection) == 0x3C);
		STATIC_ASSERT(offsetof(CollisionSweep, movementLength) == 0x48);

		// GLOBAL: TOY2 0x00729178
		CollisionQueryResult g_collisionQueryResults[2];

		// GLOBAL: TOY2 0x007295A0
		int32_t g_surfaceVelocityBlend;

		// GLOBAL: TOY2 0x007295A8
		CollisionMeshInstance g_collisionMeshInstances[300];

		// GLOBAL: TOY2 0x007270A0
		CollisionGridCell g_collisionGrid[257];

		// GLOBAL: TOY2 0x0072D2B0
		int16_t g_collisionGridMeshIndices[600];

		// GLOBAL: TOY2 0x007290E0
		int32_t g_boundedCollisionMeshCount;

		// GLOBAL: TOY2 0x007290EC
		int32_t g_collisionMeshCount;

		// GLOBAL: TOY2 0x00729120
		int32_t g_activeCollisionGridCellCount;

		// GLOBAL: TOY2 0x00729124
		int32_t g_collisionWorldMaxX;

		// GLOBAL: TOY2 0x00729110
		int32_t g_activePlatformIndex;

		// GLOBAL: TOY2 0x00728718
		int32_t g_previousPlatformIndex;

		// GLOBAL: TOY2 0x0072D2A4
		int16_t g_motionScriptEntryCount;

		// GLOBAL: TOY2 0x00729130
		int16_t g_groundCollisionMeshIndex;

		// GLOBAL: TOY2 0x00728698
		Vector3I16 g_groundNormal;

		// GLOBAL: TOY2 0x00729118
		Vector3I16 g_buzzGroundNormal;

		// GLOBAL: TOY2 0x0072912C
		int32_t g_groundPlatformIndex;

		// GLOBAL: TOY2 0x0072D2A0
		int32_t g_collisionTriangleCount;

		// GLOBAL: TOY2 0x007291D8
		uint16_t g_collisionPassFlags;

		// GLOBAL: TOY2 0x007291DC
		PackedCollisionFace* g_collisionTriangles[240];

		// GLOBAL: TOY2 0x007284B4
		int16_t g_collisionTriangleMeshIndices[240];

		// GLOBAL: TOY2 0x007286A0
		PackedCollisionFace* g_rotatedCollisionTriangles[32];

		// GLOBAL: TOY2 0x00729134
		int16_t g_rotatedCollisionTriangleMeshIndices[32];

		// GLOBAL: TOY2 0x0072D2A8
		int32_t g_rotatedCollisionTriangleCount;

		// GLOBAL: TOY2 0x0072D2AC
		int32_t g_collisionEdgeVertexCount;

		// GLOBAL: TOY2 0x00728DA0
		Vector3I g_collisionEdgeVertices[32];

		// GLOBAL: TOY2 0x00554FA0
		MathScratchVector g_mathScratch[64];

		// FUNCTION: TOY2 0x00489C30 [PROVISIONAL]
		void BuildCollisionWorld(int32_t level, uint8_t** buffer, int32_t terrainNum)
		{
			const int32_t maxMeshCount = 300;
			const int32_t gridWidth = 5;
			const int32_t gridCellCount = gridWidth * gridWidth;
			const int32_t alwaysCheckedCell = 255;
			const int32_t movingMeshCell = 256;
			const int32_t coordinateScale = 32;
			const int32_t blockScale = 64;
			const int32_t infinity = 0x7FFFFFFF;
			const int32_t negativeInfinity = (int32_t)0x80000000;
			int32_t cellIndex;
			int32_t meshIndex;

			g_surfaceVelocityBlend = 0x1000;
			g_activePlatformIndex = -1;
			g_previousPlatformIndex = -1;
			g_motionScriptEntryCount = 0;
			g_boundedCollisionMeshCount = 0;
			g_groundPlatformIndex = -1;

			for (cellIndex = 0; cellIndex < 257; cellIndex++)
				g_collisionGrid[cellIndex].meshCount = 0;

			for (int32_t platformIndex = 0; platformIndex < 32; platformIndex++)
			{
				Platform::PlatformState& platform = Platform::g_platformStates[platformIndex];
				platform.collisionMeshIndex = -1;
				platform.rotationAnglesFixed.x = 0;
				platform.rotationAnglesFixed.y = 0;
				platform.rotationAnglesFixed.z = 0;
				platform.angularVelocity.x = 0;
				platform.angularVelocity.y = 0;
				platform.angularVelocity.z = 0;
				platform.remainingRotation.x = 0;
				platform.remainingRotation.y = 0;
				platform.remainingRotation.z = 0;
				platform.velocity.x = 0;
				platform.velocity.y = 0;
				platform.velocity.z = 0;
				platform.remainingTranslation.x = 0;
				platform.remainingTranslation.y = 0;
				platform.remainingTranslation.z = 0;
				platform.flags = 0;
				platform.motionMode = 0;
			}

			const char* terrainSuffix;
			if (terrainNum == 1)
				terrainSuffix = "terr1";
			else if (terrainNum == 2)
				terrainSuffix = "terr2";
			else if (terrainNum == 3)
				terrainSuffix = "terr3";
			else
				terrainSuffix = "terrain";

			char filename[64];
			Levels::BuildLevelPath(level, filename, terrainSuffix);
			g_collisionMeshCount = Terrain::LoadAll(filename, 0, buffer);
			g_collisionWorldMaxX = negativeInfinity;

			for (meshIndex = 0; meshIndex < maxMeshCount; meshIndex++)
			{
				CollisionMeshRecord& mesh = reinterpret_cast<CollisionMeshRecord*>(g_collisionMeshInstances)[meshIndex];
				if (mesh.typeFlags != COLLISION_MESH_STATIC_A && mesh.typeFlags != COLLISION_MESH_STATIC_B && mesh.typeFlags != COLLISION_MESH_MOVING)
					continue;

				int32_t minX = infinity;
				int32_t maxX = negativeInfinity;
				int32_t minY = infinity;
				int32_t maxY = negativeInfinity;
				int32_t minZ = infinity;
				int32_t maxZ = negativeInfinity;
				int32_t boundingSqRadius = 0;
				CollisionTreeGroup* group = mesh.collisionTree;

				while (group->marker >= 0)
				{
					PackedCollisionFace* face = reinterpret_cast<PackedCollisionFace*>(group + 1);
					for (int32_t faceIndex = 0; faceIndex < group->faceCount; faceIndex++, face++)
					{
						int32_t faceMinX = face->boundsMinX;
						int32_t faceMaxX = faceMinX + face->boundsExtentX;
						int32_t faceMinY = face->boundsMinYBlock * blockScale + face->vertex0.y;
						int32_t faceMaxY = (face->boundsExtentYBlock * blockScale + face->boundsMinYBlock) * blockScale + face->vertex0.y;
						int32_t faceMinZ = face->boundsMinZBlock * blockScale + face->vertex0.z;
						int32_t faceMaxZ = (face->boundsExtentZBlock * blockScale + face->boundsMinZBlock) * blockScale + face->vertex0.z;

						if (faceMinX < minX)
							minX = faceMinX;
						if (faceMaxX > maxX)
							maxX = faceMaxX;
						if (faceMinY < minY)
							minY = faceMinY;
						if (faceMaxY > maxY)
							maxY = faceMaxY;
						if (faceMinZ < minZ)
							minZ = faceMinZ;
						if (faceMaxZ > maxZ)
							maxZ = faceMaxZ;

						int32_t vertexX = face->vertex0.y;
						int32_t worldX = mesh.origin.x + vertexX * coordinateScale;
						if (worldX > g_collisionWorldMaxX)
							g_collisionWorldMaxX = worldX;
						int32_t distance = face->vertex0.x * face->vertex0.x + vertexX * vertexX + face->vertex0.z * face->vertex0.z;
						if (distance > boundingSqRadius)
							boundingSqRadius = distance;

						int32_t vertexY = face->vertex0.x + face->vertex1Offset.x;
						vertexX = face->vertex0.y + face->vertex1Offset.y;
						int32_t vertexZ = face->vertex0.z + face->vertex1Offset.z;
						worldX = mesh.origin.x + vertexX * coordinateScale;
						if (worldX > g_collisionWorldMaxX)
							g_collisionWorldMaxX = worldX;
						distance = vertexY * vertexY + vertexX * vertexX + vertexZ * vertexZ;
						if (distance > boundingSqRadius)
							boundingSqRadius = distance;

						vertexY = face->vertex0.x + face->vertex2Offset.x;
						vertexX = face->vertex0.y + face->vertex2Offset.y;
						vertexZ = face->vertex0.z + face->vertex2Offset.z;
						worldX = mesh.origin.x + vertexX * coordinateScale;
						if (worldX > g_collisionWorldMaxX)
							g_collisionWorldMaxX = worldX;
						distance = vertexY * vertexY + vertexX * vertexX + vertexZ * vertexZ;
						if (distance > boundingSqRadius)
							boundingSqRadius = distance;

						vertexY = face->vertex0.x + face->vertex3Offset.x;
						vertexX = face->vertex0.y + face->vertex3Offset.y;
						vertexZ = face->vertex0.z + face->vertex3Offset.z;
						worldX = mesh.origin.x + vertexX * coordinateScale;
						if (worldX > g_collisionWorldMaxX)
							g_collisionWorldMaxX = worldX;
						distance = vertexY * vertexY + vertexX * vertexX + vertexZ * vertexZ;
						if (distance > boundingSqRadius)
							boundingSqRadius = distance;
					}
					group = reinterpret_cast<CollisionTreeGroup*>(face);
				}

				mesh.boundingSqRadius = boundingSqRadius;
				mesh.boundsMin.x = mesh.origin.x + minX * coordinateScale;
				mesh.boundsMin.y = mesh.origin.y + minY * coordinateScale;
				mesh.boundsMin.z = mesh.origin.z + minZ * coordinateScale;
				mesh.boundsExt.x = mesh.origin.x + maxX * coordinateScale - mesh.boundsMin.x;
				mesh.boundsExt.y = mesh.origin.y + maxY * coordinateScale - mesh.boundsMin.y;
				mesh.boundsExt.z = mesh.origin.z + maxZ * coordinateScale - mesh.boundsMin.z;
				g_boundedCollisionMeshCount++;
			}

			int32_t worldMinX = infinity;
			int32_t worldMaxX = negativeInfinity;
			int32_t worldMinZ = infinity;
			int32_t worldMaxZ = negativeInfinity;
			for (meshIndex = 0; meshIndex < maxMeshCount; meshIndex++)
			{
				CollisionMeshRecord& mesh = reinterpret_cast<CollisionMeshRecord*>(g_collisionMeshInstances)[meshIndex];
				if ((mesh.typeFlags != COLLISION_MESH_STATIC_A && mesh.typeFlags != COLLISION_MESH_STATIC_B)
					|| (mesh.flags & COLLISION_MESH_EXCLUDE_FROM_GRID) != 0)
					continue;

				for (CollisionTreeGroup* group = mesh.collisionTree; group->marker >= 0;)
				{
					int32_t minX = mesh.origin.x + group->boundsMinX * coordinateScale;
					int32_t maxX = mesh.origin.x + (group->boundsMinX + group->boundsExtentX) * coordinateScale;
					int32_t minZ = mesh.origin.z + group->boundsMinZ * coordinateScale;
					int32_t maxZ = mesh.origin.z + (group->boundsMinZ + group->boundsExtentZ) * coordinateScale;
					if (minX < worldMinX)
						worldMinX = minX;
					if (maxX > worldMaxX)
						worldMaxX = maxX;
					if (minZ < worldMinZ)
						worldMinZ = minZ;
					if (maxZ > worldMaxZ)
						worldMaxZ = maxZ;
					PackedCollisionFace* faces = reinterpret_cast<PackedCollisionFace*>(group + 1);
					group = reinterpret_cast<CollisionTreeGroup*>(faces + group->faceCount);
				}
			}

			int16_t meshGridCells[2048];
			int32_t meshMinX[2048];
			int32_t meshMaxX[2048];
			int32_t meshMinZ[2048];
			int32_t meshMaxZ[2048];
			int32_t movingMeshCount = 0;
			int32_t meshListCount = 0;
			g_collisionGrid[movingMeshCell].meshListStart = 0;

			for (meshIndex = 0; meshIndex < maxMeshCount; meshIndex++)
			{
				CollisionMeshRecord& mesh = reinterpret_cast<CollisionMeshRecord*>(g_collisionMeshInstances)[meshIndex];
				if ((mesh.typeFlags == COLLISION_MESH_STATIC_A || mesh.typeFlags == COLLISION_MESH_STATIC_B)
					&& (mesh.flags & COLLISION_MESH_EXCLUDE_FROM_GRID) == 0)
				{
					meshMinX[meshIndex] = infinity;
					meshMaxX[meshIndex] = negativeInfinity;
					meshMinZ[meshIndex] = infinity;
					meshMaxZ[meshIndex] = negativeInfinity;

					for (CollisionTreeGroup* group = mesh.collisionTree; group->marker >= 0;)
					{
						int32_t minX = mesh.origin.x + group->boundsMinX * coordinateScale;
						int32_t maxX = mesh.origin.x + (group->boundsMinX + group->boundsExtentX) * coordinateScale;
						int32_t minZ = mesh.origin.z + group->boundsMinZ * coordinateScale;
						int32_t maxZ = mesh.origin.z + (group->boundsMinZ + group->boundsExtentZ) * coordinateScale;
						if (minX < meshMinX[meshIndex])
							meshMinX[meshIndex] = minX;
						if (maxX > meshMaxX[meshIndex])
							meshMaxX[meshIndex] = maxX;
						if (minZ < meshMinZ[meshIndex])
							meshMinZ[meshIndex] = minZ;
						if (maxZ > meshMaxZ[meshIndex])
							meshMaxZ[meshIndex] = maxZ;
						PackedCollisionFace* faces = reinterpret_cast<PackedCollisionFace*>(group + 1);
						group = reinterpret_cast<CollisionTreeGroup*>(faces + group->faceCount);
					}

					int32_t gridX = (((meshMinX[meshIndex] + meshMaxX[meshIndex]) / 2 - worldMinX) * gridWidth) / (worldMaxX - worldMinX);
					if (gridX < 0)
						gridX = 0;
					else if (gridX > gridWidth - 1)
						gridX = gridWidth - 1;

					int32_t gridZ = (((meshMinZ[meshIndex] + meshMaxZ[meshIndex]) / 2 - worldMinZ) * gridWidth) / (worldMaxZ - worldMinZ);
					if (gridZ < 0)
						meshGridCells[meshIndex] = (int16_t)gridX;
					else
					{
						if (gridZ > gridWidth - 1)
							gridZ = gridWidth - 1;
						meshGridCells[meshIndex] = (int16_t)(gridX + gridZ * gridWidth);
					}
				}
				else if (mesh.typeFlags == COLLISION_MESH_MOVING && (mesh.flags & COLLISION_MESH_EXCLUDE_FROM_GRID) == 0)
				{
					g_collisionGridMeshIndices[meshListCount++] = (int16_t)meshIndex;
					g_collisionGrid[movingMeshCell].meshCount++;
					movingMeshCount++;
				}
			}

			g_collisionGrid[alwaysCheckedCell].meshListStart = (int16_t)movingMeshCount;
			for (meshIndex = 0; meshIndex < maxMeshCount; meshIndex++)
			{
				if ((reinterpret_cast<CollisionMeshRecord*>(g_collisionMeshInstances)[meshIndex].flags & COLLISION_MESH_EXCLUDE_FROM_GRID) != 0)
				{
					g_collisionGridMeshIndices[meshListCount++] = (int16_t)meshIndex;
					g_collisionGrid[alwaysCheckedCell].meshCount++;
				}
			}

			g_activeCollisionGridCellCount = 0;
			for (cellIndex = 0; cellIndex < gridCellCount; cellIndex++)
			{
				CollisionGridCell& cell = g_collisionGrid[cellIndex];
				cell.meshListStart = (int16_t)meshListCount;
				int32_t minX = infinity;
				int32_t maxX = negativeInfinity;
				int32_t minZ = infinity;
				int32_t maxZ = negativeInfinity;

				for (meshIndex = 0; meshIndex < maxMeshCount; meshIndex++)
				{
					CollisionMeshRecord& mesh = reinterpret_cast<CollisionMeshRecord*>(g_collisionMeshInstances)[meshIndex];
					if (meshGridCells[meshIndex] == cellIndex && (mesh.typeFlags == COLLISION_MESH_STATIC_A || mesh.typeFlags == COLLISION_MESH_STATIC_B)
						&& (mesh.flags & COLLISION_MESH_EXCLUDE_FROM_GRID) == 0)
					{
						if (meshMinX[meshIndex] < minX)
							minX = meshMinX[meshIndex];
						if (meshMaxX[meshIndex] > maxX)
							maxX = meshMaxX[meshIndex];
						if (meshMinZ[meshIndex] < minZ)
							minZ = meshMinZ[meshIndex];
						if (meshMaxZ[meshIndex] > maxZ)
							maxZ = meshMaxZ[meshIndex];
						cell.meshCount++;
						g_collisionGridMeshIndices[meshListCount++] = (int16_t)meshIndex;
					}
				}

				if (cell.meshCount != 0)
				{
					cell.boundsMinX = minX;
					cell.boundsMinZ = minZ;
					cell.boundsExtentX = maxX - minX;
					cell.boundsExtentZ = maxZ - minZ;
					g_activeCollisionGridCellCount++;
				}
			}

			for (int32_t queryIndex = 0; queryIndex < 2; queryIndex++)
			{
				g_collisionQueryResults[queryIndex].face = 0;
				g_collisionQueryResults[queryIndex].contactFlags = 0;
				reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].contactTimer = 0;
				reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].collisionDistance = 0xFA0;
			}
		}

		// FUNCTION: TOY2 0x004878A0 [MATCHED]
		void MarkPlatformAsMoving(int32_t platformIndex)
		{
			int16_t meshIndex = Platform::g_platformStates[platformIndex].collisionMeshIndex;
			if (meshIndex != 0)
			{
				g_collisionMeshInstances[meshIndex].typeFlags = COLLISION_MESH_MOVING;
			}
		}

		// FUNCTION: TOY2 0x0048E1B0 [MATCHED]
		bool IsMeshMoving()
		{
			int32_t meshIndex = g_collisionQueryResults[0].contactFlags & COLLISION_CONTACT_MESH_INDEX_MASK;
			return g_collisionMeshInstances[meshIndex].typeFlags == COLLISION_MESH_MOVING;
		}

		// FUNCTION: TOY2 0x00487AD0 [MATCHED]
		int32_t IsSafeFooting(int32_t queryIndex, const uint8_t* contactState)
		{
			if (*contactState != 1)
				return false;

			if ((g_collisionQueryResults[queryIndex].contactFlags & COLLISION_CONTACT_SECONDARY_FACE) == 0)
				return g_collisionQueryResults[queryIndex].face->normal.y < -0xF3C;
			return g_collisionQueryResults[queryIndex].face->secondaryNormal.y < -0xF3C;
		}

		// FUNCTION: TOY2 0x0048E1D0 [MATCHED]
		int32_t GetGroundContactIdx()
		{
			if (g_groundCollisionMeshIndex != -1 && g_collisionMeshInstances[g_groundCollisionMeshIndex].typeFlags == COLLISION_MESH_MOVING)
			{
				return g_collisionMeshInstances[g_groundCollisionMeshIndex].platformIdx;
			}
			return -1;
		}

		// FUNCTION: TOY2 0x0048C860 [PROVISIONAL]
		int32_t SweepAndSlide(Vector3I* position, Vector3I* movement, int32_t collisionThreshold, int16_t* collisionAngles, int32_t radius)
		{
			const int32_t maxTriangles = 240;
			const int32_t maxEdgeVertices = 32;
			const int32_t edgeBlockSize = 16;
			const int32_t noEdgeBounds = (int32_t)0x80000000;
			int32_t localX = 0;
			int32_t localY = 0;

			g_collisionEdgeVertexCount = 0;
			g_collisionPassFlags = 0;
			Vector4I delta;
			delta.x = movement->x;
			delta.y = movement->y;
			delta.z = movement->z;
			int32_t localZ = 0;
			int32_t meshInRange = 0;

			int32_t queryMaxX;
			uint32_t queryExtentX;
			if (delta.x < 0)
			{
				queryMaxX = position->x + 0x800;
				queryExtentX = 0x1000 - delta.x;
			}
			else
			{
				queryMaxX = position->x + 0x800 + delta.x;
				queryExtentX = delta.x + 0x1000;
			}

			int32_t queryMaxY;
			uint32_t queryExtentY;
			if (delta.y < 0)
			{
				queryMaxY = position->y + 0x800;
				queryExtentY = 0x1000 - delta.y;
			}
			else
			{
				queryMaxY = position->y + 0x800 + delta.y;
				queryExtentY = delta.y + 0x1000;
			}

			int32_t queryMaxZ;
			uint32_t queryExtentZ;
			if (delta.z < 0)
			{
				queryMaxZ = position->z + 0x800;
				queryExtentZ = 0x1000 - delta.z;
			}
			else
			{
				queryMaxZ = position->z + 0x800 + delta.z;
				queryExtentZ = delta.z + 0x1000;
			}

			g_collisionTriangleCount = 0;
			queryMaxX += radius * 0x1644 / 0x1000;
			queryMaxY += radius * 0x1644 / 0x1000;
			queryMaxZ += radius * 0x1644 / 0x1000;
			queryExtentX += radius * 0x1644 / 0x800;
			queryExtentY += radius * 0x1644 / 0x800;
			queryExtentZ += radius * 0x1644 / 0x800;
			uint32_t toleranceX = (queryExtentX + 31) / 32;
			uint32_t toleranceY = (queryExtentY + 31) / 32;
			uint32_t toleranceZ = (queryExtentZ + 31) / 32;
			int32_t lastMeshIndex = -1;
			uint32_t excludeMask = collisionAngles != 0 ? 0x200 : COLLISION_MESH_EXCLUDE_FROM_QUERY;

			if (collisionAngles == 0 || queryMaxX >= Terrain::g_collisionWorkspace->slots[8].queryBounds.maximum.x
				|| queryMaxY >= Terrain::g_collisionWorkspace->slots[8].queryBounds.maximum.y
				|| queryMaxZ >= Terrain::g_collisionWorkspace->slots[8].queryBounds.maximum.z
				|| queryMaxX - queryExtentX <= (uint32_t)Terrain::g_collisionWorkspace->slots[8].queryBounds.minimum.x
				|| queryMaxY - queryExtentY <= (uint32_t)Terrain::g_collisionWorkspace->slots[8].queryBounds.minimum.y
				|| queryMaxZ - queryExtentZ <= (uint32_t)Terrain::g_collisionWorkspace->slots[8].queryBounds.minimum.z)
			{
				int16_t* candidateMeshes = reinterpret_cast<int16_t*>(g_mathScratch);
				int32_t candidateCount = 0;
				int16_t* candidate = candidateMeshes;
				int32_t cellCount = g_activeCollisionGridCellCount;
				for (int32_t cellIndex = 0; cellIndex < cellCount; cellIndex++)
				{
					CollisionGridCell& cell = g_collisionGrid[cellIndex];
					if ((uint32_t)(queryMaxX - cell.boundsMinX) < cell.boundsExtentX + queryExtentX
						&& (uint32_t)(queryMaxZ - cell.boundsMinZ) < cell.boundsExtentZ + queryExtentZ)
					{
						int16_t* meshList = &g_collisionGridMeshIndices[cell.meshListStart];
						int32_t meshCount = cell.meshCount;
						for (int32_t index = 0; index < meshCount; index++)
							*candidate++ = meshList[index];
						candidateCount += meshCount;
					}
				}

				for (int32_t candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++)
				{
					int16_t meshIndex = candidateMeshes[candidateIndex];
					CollisionMeshInstance& mesh = g_collisionMeshInstances[meshIndex];
					if ((uint32_t)(queryMaxX - mesh.boundsMin.x) < mesh.boundsExt.x + queryExtentX
						&& (uint32_t)(queryMaxY - mesh.boundsMin.y) < mesh.boundsExt.y + queryExtentY
						&& (uint32_t)(queryMaxZ - mesh.boundsMin.z) < mesh.boundsExt.z + queryExtentZ && mesh.typeFlags != 0 && (excludeMask & mesh.unk) == 0)
					{
						localX = (queryMaxX - mesh.origin.x) / 32;
						localY = (queryMaxY - mesh.origin.y) / 32;
						localZ = (queryMaxZ - mesh.origin.z) / 32;
						CollisionTreeGroup* group = reinterpret_cast<CollisionTreeGroup*>(mesh.collisionTree);
						while (group->marker >= 0)
						{
							int32_t faceCount = group->faceCount;
							PackedCollisionFace* face = reinterpret_cast<PackedCollisionFace*>(group + 1);
							if ((uint32_t)(localX - group->boundsMinX) < group->boundsExtentX + toleranceX
								&& (uint32_t)(localZ - group->boundsMinZ) < group->boundsExtentZ + toleranceZ)
							{
								for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
								{
									if ((uint32_t)(localX - face->boundsMinX) < face->boundsExtentX + toleranceX
										&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < face->boundsExtentZBlock * 64 + toleranceZ
										&& (uint32_t)(localY - face->boundsMinYBlock * 64 - face->vertex0.y) < face->boundsExtentYBlock * 64 + toleranceY
										&& face->firstPlaneNormal.y < collisionThreshold && g_collisionTriangleCount < maxTriangles)
									{
										g_collisionTriangles[g_collisionTriangleCount] = face;
										g_collisionTriangleMeshIndices[g_collisionTriangleCount] = meshIndex;
										g_collisionTriangleCount++;
									}
								}
							}
							else
								face += faceCount;
							group = reinterpret_cast<CollisionTreeGroup*>(face);
						}
					}
				}
			}
			else
			{
				Terrain::CollisionWorkspaceSlot& cache = Terrain::g_collisionWorkspace->slots[8];
				for (int32_t entry = cache.entryCount - 1; entry >= 0; entry--)
				{
					int32_t meshIndex = cache.meshIndices[entry];
					if (meshIndex != lastMeshIndex)
					{
						meshInRange = 0;
						lastMeshIndex = meshIndex;
						CollisionMeshInstance& mesh = g_collisionMeshInstances[meshIndex];
						if ((uint32_t)(queryMaxX - mesh.boundsMin.x) < mesh.boundsExt.x + queryExtentX
							&& (uint32_t)(queryMaxY - mesh.boundsMin.y) < mesh.boundsExt.y + queryExtentY
							&& (uint32_t)(queryMaxZ - mesh.boundsMin.z) < mesh.boundsExt.z + queryExtentZ && mesh.typeFlags != 0
							&& (excludeMask & mesh.unk) == 0)
						{
							meshInRange = 1;
							localX = (queryMaxX - mesh.origin.x) / 32;
							localY = (queryMaxY - mesh.origin.y) / 32;
							localZ = (queryMaxZ - mesh.origin.z) / 32;
						}
					}

					if (meshInRange != 0)
					{
						PackedCollisionFace* face = cache.faces[entry];
						if ((uint32_t)(localX - face->boundsMinX) < face->boundsExtentX + toleranceX
							&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < face->boundsExtentZBlock * 64 + toleranceZ
							&& (uint32_t)(localY - face->boundsMinYBlock * 64 - face->vertex0.y) < face->boundsExtentYBlock * 64 + toleranceY
							&& g_collisionTriangleCount < maxTriangles)
						{
							g_collisionTriangles[g_collisionTriangleCount] = face;
							g_collisionTriangleMeshIndices[g_collisionTriangleCount] = (int16_t)lastMeshIndex;
							g_collisionTriangleCount++;
						}
					}
				}
			}

			int16_t* movingMeshes = &g_collisionGridMeshIndices[g_collisionGrid[256].meshListStart];
			int32_t moveX = delta.x / 16;
			int32_t moveY = delta.y / 16;
			int32_t moveZ = delta.z / 16;
			int32_t moveLengthSq = moveX * moveX + moveY * moveY + moveZ * moveZ;
			int32_t movementReach = (int32_t)sqrt((double)moveLengthSq) * 16;
			int32_t movingCount = g_collisionGrid[256].meshCount;
			for (int32_t movingIndex = 0; movingIndex < movingCount; movingIndex++)
			{
				CollisionMeshInstance& mesh = g_collisionMeshInstances[movingMeshes[movingIndex]];
				int32_t x = position->x;
				int32_t y = position->y;
				int32_t z = position->z;
				int32_t offsetX = (mesh.origin.x - x) >> 6;
				int32_t offsetY = (mesh.origin.y - y) >> 6;
				int32_t offsetZ = (mesh.origin.z - z) >> 6;
				if (offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ > (mesh.boundingSqRadius >> 1) + moveLengthSq)
					continue;

				Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
				int32_t platformRadius = abs(platform.velocity.x) + abs(platform.velocity.y) + movementReach + abs(platform.velocity.z);
				int32_t platformDiameter = platformRadius * 2;
				int32_t tolerance = (platformRadius + 15) / 16;
				int32_t queryX;
				int32_t queryY;
				int32_t queryZ;
				if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
				{
					Vector3I16 angles;
					angles.x = platform.rotationAnglesFixed.x >> 2;
					angles.y = platform.rotationAnglesFixed.y >> 2;
					angles.z = platform.rotationAnglesFixed.z >> 2;
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
					const Matrix3x3I16& rotation = Animation::g_keyframeRotation.matrix;
					int32_t relativeX = position->x - mesh.origin.x;
					int32_t relativeY = position->y - mesh.origin.y;
					int32_t relativeZ = position->z - mesh.origin.z;
					queryX = (rotation.m00 * relativeX + rotation.m10 * relativeY + rotation.m20 * relativeZ) / 0x1000 + mesh.origin.x + platformRadius;
					queryY = (rotation.m01 * relativeX + rotation.m11 * relativeY + rotation.m21 * relativeZ) / 0x1000 + mesh.origin.y + platformRadius;
					queryZ = (rotation.m02 * relativeX + rotation.m12 * relativeY + rotation.m22 * relativeZ) / 0x1000 + mesh.origin.z + platformRadius;
				}
				else
				{
					queryX = x + platformRadius;
					queryY = y + platformRadius;
					queryZ = z + platformRadius;
				}

				if (queryX - mesh.boundsMin.x < mesh.boundsExt.x + platformDiameter && queryZ - mesh.boundsMin.z < mesh.boundsExt.z + platformDiameter
					&& mesh.typeFlags != 0 && (excludeMask & mesh.unk) == 0)
				{
					int32_t platformLocalX = (queryX - mesh.origin.x) / 32;
					int32_t platformLocalY = (queryY - mesh.origin.y) / 32;
					int32_t platformLocalZ = (queryZ - mesh.origin.z) / 32;
					CollisionTreeGroup* group = reinterpret_cast<CollisionTreeGroup*>(mesh.collisionTree);
					while (group->marker >= 0)
					{
						int32_t faceCount = group->faceCount;
						PackedCollisionFace* face = reinterpret_cast<PackedCollisionFace*>(group + 1);
						if ((uint32_t)(platformLocalX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + tolerance)
							&& (uint32_t)(platformLocalZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + tolerance))
						{
							for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
							{
								if ((uint32_t)(platformLocalX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + tolerance)
									&& (uint32_t)(platformLocalZ - face->boundsMinZBlock * 64 - face->vertex0.z)
										< (uint32_t)(face->boundsExtentZBlock * 64 + tolerance)
									&& (uint32_t)(platformLocalY - face->boundsMinYBlock * 64 - face->vertex0.y)
										< (uint32_t)(face->boundsExtentYBlock * 64 + tolerance)
									&& g_collisionTriangleCount < maxTriangles)
								{
									g_collisionTriangles[g_collisionTriangleCount] = face;
									g_collisionTriangleMeshIndices[g_collisionTriangleCount] = movingMeshes[movingIndex];
									g_collisionTriangleCount++;
								}
							}
						}
						else
							face += faceCount;
						group = reinterpret_cast<CollisionTreeGroup*>(face);
					}
				}
			}

			int32_t minimumX = (int32_t)(queryMaxX - queryExtentX) / 32;
			int32_t minimumZ = (int32_t)(queryMaxZ - queryExtentZ) / 32;
			int32_t maximumX = queryMaxX / 32;
			int32_t maximumZ = queryMaxZ / 32;
			for (uint8_t* head = Terrain::g_terrainRelocationHeads[1]; head != 0;
				head = reinterpret_cast<Terrain::TerrainRelocationLink*>(head - sizeof(Terrain::TerrainRelocationLink))->next)
			{
				Terrain::TerrainEdgeList* edges = reinterpret_cast<Terrain::TerrainEdgeList*>(head);
				for (int32_t edgeIndex = 0; edgeIndex < edges->edgeCount; edgeIndex += edgeBlockSize)
				{
					Vector3I* block = &edges->vertices[edgeIndex];
					int32_t blockEnd;
					if (block[0].y != noEdgeBounds)
					{
						if ((uint32_t)(maximumX - block[0].y) < block[1].y + toleranceX && (uint32_t)(maximumZ - block[2].y) < block[3].y + toleranceZ)
							blockEnd = edgeIndex + edgeBlockSize;
						else
							blockEnd = 0;
					}
					else
						blockEnd = edges->edgeCount;

					for (int32_t index = edgeIndex; index < blockEnd; index++)
					{
						Vector3I& start = edges->vertices[index];
						Vector3I& end = edges->vertices[index + 1];
						if (((start.x >= minimumX && end.x <= maximumX) || (end.x >= minimumX && start.x <= maximumX))
							&& ((start.z >= minimumZ && end.z <= maximumZ) || (end.z >= minimumZ && start.z <= maximumZ))
							&& g_collisionEdgeVertexCount < maxEdgeVertices)
						{
							g_collisionEdgeVertices[g_collisionEdgeVertexCount++] = start;
							g_collisionEdgeVertices[g_collisionEdgeVertexCount++] = end;
						}
					}
				}
			}

			int32_t hit = 0;
			if (g_collisionTriangleCount > 0 || g_collisionEdgeVertexCount > 0)
			{
				// Callers pass the position field of a larger record; the sweep takes the following word as well.
				Vector4I* positionRecord = reinterpret_cast<Vector4I*>(position);
				Vector4I start = *positionRecord;
				hit = Nu3D::Collision::SweepAgainstCandidates(&delta, &start, radius);
				movement->x = start.x - position->x;
				movement->y = start.y - position->y;
				movement->z = start.z - position->z;
			}
			return hit;
		}

		// FUNCTION: TOY2 0x00480660 [PROVISIONAL]
		int32_t SweepWallSegments(CollisionSweep* sweep, const Vector3I* start, const Vector3I* movement, int32_t radius)
		{
			if (g_collisionEdgeVertexCount == 0)
				return 0;

			int32_t foundHit = 0;
			Vector3I16 normal;
			normal.y = 0;
			int32_t expandedRadius = ShiftTowardZero(radius + 0x2000, 5);

			for (int32_t vertexIndex = 0; vertexIndex < g_collisionEdgeVertexCount; vertexIndex += 2)
			{
				const Vector3I& edgeStart = g_collisionEdgeVertices[vertexIndex];
				const Vector3I& edgeEnd = g_collisionEdgeVertices[vertexIndex + 1];

				normal.x = (int16_t)edgeStart.z - (int16_t)edgeEnd.z;
				normal.z = (int16_t)edgeEnd.x - (int16_t)edgeStart.x;
				Nu3D::Math::NormalizeToFixedPoint16(&normal, &normal);

				int32_t startDeltaX = start->x - edgeStart.x * 0x20;
				int32_t startDeltaZ = start->z - edgeStart.z * 0x20;
				int32_t startDistance = ((startDeltaZ * normal.z + startDeltaX * normal.x) >> 12) - radius;
				if (startDistance >= 0)
				{
					int32_t endDistance =
						(((startDeltaZ + movement->z) * normal.z + (startDeltaX + movement->x) * normal.x) >> 12) - radius;
					if (endDistance < 0)
					{
						int16_t edgeDeltaX = (int16_t)edgeEnd.x - (int16_t)edgeStart.x;
						int16_t edgeDeltaZ = (int16_t)edgeEnd.z - (int16_t)edgeStart.z;
						int32_t distanceRange = startDistance - endDistance;
						int32_t hitX = start->x + movement->x * startDistance / distanceRange - (normal.x * radius >> 12);
						int32_t hitZ = start->z + movement->z * startDistance / distanceRange - (normal.z * radius >> 12);
						int32_t fromStart = (hitZ - edgeStart.z * 0x20) * edgeDeltaZ + (hitX - edgeStart.x * 0x20) * edgeDeltaX;
						int32_t fromEnd = (hitZ - edgeEnd.z * 0x20) * edgeDeltaZ + (hitX - edgeEnd.x * 0x20) * edgeDeltaX;

						if (fromStart >= -0x2000 && fromEnd <= 0x2000)
						{
							int32_t fraction = (startDistance << 14) / distanceRange;
							if (fraction < sweep->nearestFraction)
							{
								sweep->startDistance = startDistance;
								sweep->endDistance = endDistance;
								sweep->hitNormal.direction.x = normal.x * 4;
								sweep->hitNormal.direction.y = 0;
								sweep->hitNormal.direction.z = normal.z * 4;
								sweep->nearestFraction = fraction;
								foundHit = 1;
							}
						}
					}
				}

				int32_t edgeOffsetX = edgeStart.x - ShiftTowardZero(start->x, 5);
				int32_t edgeOffsetZ = edgeStart.z - ShiftTowardZero(start->z, 5);
				int32_t movementX = ShiftTowardZero(movement->x, 5);
				int32_t movementZ = ShiftTowardZero(movement->z, 5);
				if (edgeOffsetX * edgeOffsetX + edgeOffsetZ * edgeOffsetZ
					< movementX * movementX + movementZ * movementZ + expandedRadius * expandedRadius)
				{
					Vector3I16 direction = { (int16_t)movement->x, 0, (int16_t)movement->z };
					Nu3D::Math::NormalizeToFixedPoint16(&direction, &direction);

					int32_t edgeStartX = edgeStart.x * 0x20;
					int32_t edgeStartZ = edgeStart.z * 0x20;
					int32_t projectedDistance =
						((edgeStartZ - start->z) * direction.z + (edgeStartX - start->x) * direction.x) >> 12;
					if (projectedDistance >= 0)
					{
						int32_t closestX = edgeStartX - (start->x + (direction.x * projectedDistance >> 12));
						int32_t closestZ = edgeStartZ - (start->z + (direction.z * projectedDistance >> 12));
						int32_t closestDistanceSquared = closestX * closestX + closestZ * closestZ;
						if (closestDistanceSquared <= (radius + 0x20) * (radius + 0x20))
						{
							int32_t hitDistance = projectedDistance
								- (int32_t)sqrt((double)((radius + 0x40) * (radius + 0x40) - closestDistanceSquared));
							int32_t movementLengthSquared = movement->x * movement->x + movement->z * movement->z;
							if (hitDistance >= -0x20 && hitDistance * hitDistance <= movementLengthSquared)
							{
								if (hitDistance < 0)
									hitDistance = 0;

								int32_t movementLength = (int32_t)sqrt((double)movementLengthSquared);
								int32_t fraction = (hitDistance << 14) / movementLength;
								if (fraction < sweep->nearestFraction)
								{
									normal.x = (int16_t)(direction.x * hitDistance >> 12) - (int16_t)edgeStart.x * 0x20
										+ (int16_t)start->x;
									normal.z = (int16_t)(direction.z * hitDistance >> 12) - (int16_t)edgeStart.z * 0x20
										+ (int16_t)start->z;
									Nu3D::Math::NormalizeToFixedPoint16(&normal, &normal);

									sweep->startDistance = hitDistance;
									sweep->endDistance = hitDistance - movementLength;
									sweep->hitNormal.direction.x = normal.x * 4;
									sweep->hitNormal.direction.y = 0;
									sweep->hitNormal.direction.z = normal.z * 4;
									sweep->nearestFraction = fraction;
									foundHit = 1;
								}
							}
						}
					}
				}
			}
			return foundHit;
		}

		// FUNCTION: TOY2 0x004813B0 [PROVISIONAL]
		int32_t SweepEdge(const Vector3I* start,
			const Vector3I* end,
			int32_t edgeX,
			int32_t edgeY,
			int32_t edgeZ,
			CollisionSweep* sweep,
			int32_t radius,
			const Vector3I16* adjacentNormals)
		{
			Vector3I& movementDirection = g_mathScratch[1].value;
			Vector3I& edgeDirection = g_mathScratch[2].value;
			Vector3I& contactPoint = g_mathScratch[3].value;
			Vector3I& perpendicular = g_mathScratch[4].value;
			Vector3I& edgeCross = g_mathScratch[5].value;
			Vector3I& planeNormal = g_mathScratch[6].value;
			Vector3I& crossOffset = g_mathScratch[7].value;
			Vector3I& hitNormal = g_mathScratch[8].value;

			movementDirection = sweep->movementDirection;
			g_mathScratch[1].scalar = sweep->movementLength;
			edgeDirection.x = edgeX;
			edgeDirection.y = edgeY;
			edgeDirection.z = edgeZ;
			Nu3D::Math::NormalizeToFixedPoint(&edgeDirection, &edgeDirection);
			Nu3D::Math::CrossProduct3D(&movementDirection, &edgeDirection, &edgeCross);

			if (abs(edgeCross.x) < 0x80000 && abs(edgeCross.y) < 0x80000 && abs(edgeCross.z) < 0x80000)
			{
				if (edgeCross.x == 0 && edgeCross.y == 0 && edgeCross.z == 0)
					return 0;

				planeNormal.x = (start->x + end->x) >> 1;
				planeNormal.y = (start->y + end->y) >> 1;
				planeNormal.z = (start->z + end->z) >> 1;
				int32_t edgeLengthSquared = edgeX * edgeX + edgeY * edgeY + edgeZ * edgeZ;
				int32_t scaledEdgeLengthSquared = edgeLengthSquared;
				int32_t edgeProjection = planeNormal.x * edgeX + planeNormal.y * edgeY + planeNormal.z * edgeZ;
				while (edgeProjection > 0x40000)
				{
					edgeProjection >>= 1;
					scaledEdgeLengthSquared >>= 1;
				}
				if (scaledEdgeLengthSquared == 0)
					return 0;

				planeNormal.x -= edgeProjection * edgeX / scaledEdgeLengthSquared;
				planeNormal.y -= edgeProjection * edgeY / scaledEdgeLengthSquared;
				planeNormal.z -= edgeProjection * edgeZ / scaledEdgeLengthSquared;
				while (abs(planeNormal.x) > 0x4000 || abs(planeNormal.y) > 0x4000 || abs(planeNormal.z) > 0x4000)
				{
					planeNormal.x >>= 2;
					planeNormal.y >>= 2;
					planeNormal.z >>= 2;
				}
				Nu3D::Math::NormalizeToFixedPoint(&planeNormal, &planeNormal);

				int32_t startDistance = ((start->x * planeNormal.x + start->y * planeNormal.y + start->z * planeNormal.z) >> 12) - radius;
				int32_t endDistance = ((end->x * planeNormal.x + end->y * planeNormal.y + end->z * planeNormal.z) >> 12) - radius;
				if (startDistance < 0 || endDistance >= 0)
					return 0;

				int32_t distanceRange = startDistance - endDistance;
				int32_t fraction = startDistance * 0x4000 / distanceRange;
				if (fraction >= sweep->nearestFraction)
					return 0;

				contactPoint.x = start->x + (end->x - start->x) * startDistance / distanceRange;
				contactPoint.y = start->y + (end->y - start->y) * startDistance / distanceRange;
				contactPoint.z = start->z + (end->z - start->z) * startDistance / distanceRange;
				int32_t alongEdge = contactPoint.x * edgeX + contactPoint.y * edgeY + contactPoint.z * edgeZ;
				if (alongEdge < 0 || (alongEdge >> 5) > edgeLengthSquared)
					return 0;

				scaledEdgeLengthSquared = edgeLengthSquared;
				while (alongEdge > 0x40000)
				{
					alongEdge >>= 1;
					scaledEdgeLengthSquared >>= 1;
				}
				contactPoint.x -= alongEdge * edgeX / scaledEdgeLengthSquared;
				contactPoint.y -= alongEdge * edgeY / scaledEdgeLengthSquared;
				contactPoint.z -= alongEdge * edgeZ / scaledEdgeLengthSquared;
				Nu3D::Math::NormalizeToFixedPoint(&contactPoint, &contactPoint);
				contactPoint.x *= 4;
				contactPoint.y *= 4;
				contactPoint.z *= 4;

				bool accepted = adjacentNormals[0].y != 0x7FFF
					&& adjacentNormals[0].x * hitNormal.x + adjacentNormals[0].y * hitNormal.y + adjacentNormals[0].z * hitNormal.z > -0x80000;
				if (! accepted)
				{
					accepted = adjacentNormals[1].y != 0x7FFF
						&& adjacentNormals[1].x * hitNormal.x + adjacentNormals[1].y * hitNormal.y + adjacentNormals[1].z * hitNormal.z > -0x80000;
				}
				if (! accepted)
					return 0;

				sweep->startDistance = startDistance;
				sweep->endDistance = endDistance;
				sweep->nearestFraction = fraction;
				sweep->hitNormal.direction.x = (int16_t)hitNormal.x;
				sweep->hitNormal.direction.y = (int16_t)hitNormal.y;
				sweep->hitNormal.direction.z = (int16_t)hitNormal.z;
				return 1;
			}

			edgeCross.x >>= 10;
			edgeCross.y >>= 10;
			edgeCross.z >>= 10;
			if (edgeCross.x == 0 && edgeCross.y == 0 && edgeCross.z == 0)
				return 0;

			planeNormal = edgeCross;
			while (abs(planeNormal.x) > 0x4000 || abs(planeNormal.y) > 0x4000 || abs(planeNormal.z) > 0x4000)
			{
				planeNormal.x >>= 1;
				planeNormal.y >>= 1;
				planeNormal.z >>= 1;
			}
			Nu3D::Math::NormalizeToFixedPoint(&planeNormal, &planeNormal);

			int32_t planeDistance = (start->x * planeNormal.x + start->y * planeNormal.y + start->z * planeNormal.z) >> 10;
			if (abs(planeDistance) > radius * 4 + 0xC0)
				return 0;
			int32_t directionDot = (planeNormal.x * edgeCross.x + planeNormal.y * edgeCross.y + planeNormal.z * edgeCross.z) >> 12;
			if (directionDot == 0)
				return 0;

			crossOffset.x = start->x >> 2;
			crossOffset.y = start->y >> 2;
			crossOffset.z = start->z >> 2;
			Nu3D::Math::CrossProduct3D(&crossOffset, &edgeDirection, &crossOffset);
			crossOffset.x >>= 10;
			crossOffset.y >>= 10;
			crossOffset.z >>= 10;
			int32_t hitDistance = -4 * ((crossOffset.x * planeNormal.x + crossOffset.y * planeNormal.y + crossOffset.z * planeNormal.z) / directionDot);

			Nu3D::Math::CrossProduct3D(&planeNormal, &edgeDirection, &perpendicular);
			while (abs(perpendicular.x) > 0x10000 || abs(perpendicular.y) > 0x10000 || abs(perpendicular.z) > 0x10000)
			{
				perpendicular.x >>= 3;
				perpendicular.y >>= 3;
				perpendicular.z >>= 3;
			}
			while (abs(perpendicular.x) > 0x4000 || abs(perpendicular.y) > 0x4000 || abs(perpendicular.z) > 0x4000)
			{
				perpendicular.x >>= 1;
				perpendicular.y >>= 1;
				perpendicular.z >>= 1;
			}
			Nu3D::Math::NormalizeToFixedPoint(&perpendicular, &perpendicular);

			int32_t approachRate = (movementDirection.x * perpendicular.x + movementDirection.y * perpendicular.y + movementDirection.z * perpendicular.z) >> 8;
			if (approachRate == 0)
				return 0;
			int32_t radiusOffset = (int32_t)sqrt((double)abs(radius * radius * 16 - planeDistance * planeDistance));
			radiusOffset = abs(radiusOffset * 0x4000 / approachRate);
			hitDistance -= radiusOffset;
			if (hitDistance < 0 || hitDistance >= sweep->movementLength)
				return 0;

			int32_t fraction = hitDistance * 0x4000 / sweep->movementLength;
			if (fraction >= sweep->nearestFraction)
				return 0;

			contactPoint.x = start->x + (end->x - start->x) * hitDistance / sweep->movementLength;
			contactPoint.y = start->y + (end->y - start->y) * hitDistance / sweep->movementLength;
			contactPoint.z = start->z + (end->z - start->z) * hitDistance / sweep->movementLength;
			int32_t alongEdge = contactPoint.x * edgeX + contactPoint.y * edgeY + contactPoint.z * edgeZ;
			int32_t edgeLengthSquared = edgeX * edgeX + edgeY * edgeY + edgeZ * edgeZ;
			if (alongEdge < 0 || (alongEdge >> 5) > edgeLengthSquared)
				return 0;

			int32_t scaledEdgeLengthSquared = edgeLengthSquared;
			while (alongEdge > 0x40000)
			{
				alongEdge >>= 1;
				scaledEdgeLengthSquared >>= 1;
			}
			hitNormal.x = contactPoint.x - alongEdge * edgeX / scaledEdgeLengthSquared;
			hitNormal.y = contactPoint.y - alongEdge * edgeY / scaledEdgeLengthSquared;
			hitNormal.z = contactPoint.z - alongEdge * edgeZ / scaledEdgeLengthSquared;
			Nu3D::Math::NormalizeToFixedPoint(&hitNormal, &hitNormal);
			hitNormal.x *= 4;
			hitNormal.y *= 4;
			hitNormal.z *= 4;

			bool accepted = adjacentNormals[0].y != 0x7FFF
				&& adjacentNormals[0].x * hitNormal.x + adjacentNormals[0].y * hitNormal.y + adjacentNormals[0].z * hitNormal.z > -0x80000;
			if (! accepted)
			{
				accepted = adjacentNormals[1].y != 0x7FFF
					&& adjacentNormals[1].x * hitNormal.x + adjacentNormals[1].y * hitNormal.y + adjacentNormals[1].z * hitNormal.z > -0x80000;
			}
			if (! accepted)
				return 0;

			sweep->startDistance = hitDistance;
			sweep->endDistance = hitDistance - sweep->movementLength;
			sweep->nearestFraction = fraction;
			sweep->hitNormal.direction.x = (int16_t)hitNormal.x;
			sweep->hitNormal.direction.y = (int16_t)hitNormal.y;
			sweep->hitNormal.direction.z = (int16_t)hitNormal.z;
			return 1;
		}

		// FUNCTION: TOY2 0x00481D00 [PROVISIONAL]
		int32_t SweepVertex(Vector3I* start, Vector3I* end, CollisionSweep* sweep, int32_t radius, const Vector3I16* adjacentNormals)
		{
			int32_t accepted;
			Vector3I hitNormal;
			Vector3I direction;

			start->x >>= 2;
			start->y >>= 2;
			start->z >>= 2;
			end->x >>= 2;
			end->y >>= 2;
			end->z >>= 2;

			direction.x = end->x - start->x;
			direction.y = end->y - start->y;
			direction.z = end->z - start->z;
			Nu3D::Math::NormalizeToFixedPoint(&direction, &direction);

			Vector3I vertex = *start;
			radius >>= 2;
			int32_t projectedDistance = -(vertex.x * direction.x + vertex.y * direction.y + vertex.z * direction.z) >> 12;
			int32_t sweepLength;
			int32_t closestX;
			int32_t closestY;
			int32_t closestZ;
			if (-radius <= projectedDistance
				&& (sweepLength =
						((((end->x - vertex.x) * direction.x + (end->y - vertex.y) * direction.y + (end->z - vertex.z) * direction.z) >> 8) + 0x80) >> 4,
					projectedDistance <= sweepLength + radius)
				&& (closestZ = vertex.z + (projectedDistance * direction.z >> 12),
					closestY = vertex.y + (projectedDistance * direction.y >> 12),
					closestX = vertex.x + (projectedDistance * direction.x >> 12),
					closestX * closestX + closestY * closestY + closestZ * closestZ <= radius * radius))
			{
				int32_t hitDistance =
					projectedDistance - (int32_t)sqrt((double)(radius * radius - (closestX * closestX + closestY * closestY + closestZ * closestZ)));
				if (hitDistance >= 0 && hitDistance < sweepLength)
				{
					int32_t fraction = (hitDistance << 14) / sweepLength;
					if (fraction < 0)
					{
						for (;;) {}
					}
					if (fraction < sweep->nearestFraction)
					{
						hitNormal.x = vertex.x + (hitDistance * direction.x >> 12);
						hitNormal.y = vertex.y + (hitDistance * direction.y >> 12);
						hitNormal.z = vertex.z + (hitDistance * direction.z >> 12);
						accepted = false;
						Nu3D::Math::NormalizeToFixedPoint(&hitNormal, &hitNormal);
						hitNormal.x *= 4;
						hitNormal.y *= 4;
						hitNormal.z *= 4;
						if (adjacentNormals[0].y != 0x7FFF
							&& adjacentNormals[0].x * hitNormal.x + adjacentNormals[0].y * hitNormal.y + adjacentNormals[0].z * hitNormal.z > -0x80000)
						{
							accepted = true;
						}
						if ((adjacentNormals[1].y != 0x7FFF
								&& adjacentNormals[1].x * hitNormal.x + adjacentNormals[1].y * hitNormal.y + adjacentNormals[1].z * hitNormal.z > -0x80000)
							|| accepted)
						{
							sweep->startDistance = hitDistance;
							sweep->hitNormal.direction.x = (int16_t)hitNormal.x;
							sweep->nearestFraction = fraction;
							sweep->endDistance = hitDistance - sweepLength;
							sweep->hitNormal.direction.y = (int16_t)hitNormal.y;
							sweep->hitNormal.direction.z = (int16_t)hitNormal.z;
							return 1;
						}
					}
				}
			}
			return 0;
		}

		// FUNCTION: TOY2 0x00481FB0 [PROVISIONAL]
		int32_t SweepTriangle(PackedCollisionFace* face, CollisionSweep* sweep, int32_t radius, uint32_t contactFlags)
		{
			int32_t foundHit = 0;
			const int32_t coordinateScale = 0x20;
			const int32_t fractionScale = 0x4000;
			const int32_t faceTolerance = 0x100;
			const int32_t edgeContactFlag = 0x2000;
			Vector3I localStart;
			Vector3I localEnd;
			Vector3I16 adjacentNormals[2];

			int32_t firstStartDistance =
				((sweep->start.x - face->vertex0.x * coordinateScale) * face->firstPlaneNormal.x
					+ (sweep->start.y - face->vertex0.y * coordinateScale) * face->firstPlaneNormal.y
					+ (sweep->start.z - face->vertex0.z * coordinateScale) * face->firstPlaneNormal.z)
				>> 14;
			firstStartDistance -= radius;
			int32_t firstEndDistance =
				((sweep->end.x - face->vertex0.x * coordinateScale) * face->firstPlaneNormal.x
					+ (sweep->end.y - face->vertex0.y * coordinateScale) * face->firstPlaneNormal.y
					+ (sweep->end.z - face->vertex0.z * coordinateScale) * face->firstPlaneNormal.z)
				>> 14;
			firstEndDistance -= radius;

			if (firstStartDistance >= 0 && firstEndDistance < 0)
			{
				int32_t distanceRange = firstStartDistance - firstEndDistance;
				Vector3I contactPoint;
				contactPoint.x =
					sweep->start.x + (sweep->end.x - sweep->start.x) * firstStartDistance / distanceRange - (face->firstPlaneNormal.x * radius >> 14);
				contactPoint.y =
					sweep->start.y + (sweep->end.y - sweep->start.y) * firstStartDistance / distanceRange - (face->firstPlaneNormal.y * radius >> 14);
				contactPoint.z =
					sweep->start.z + (sweep->end.z - sweep->start.z) * firstStartDistance / distanceRange - (face->firstPlaneNormal.z * radius >> 14);
				if (Nu3D::Collision::IsPointInTriangle((contactPoint.x >> 5) - face->vertex0.x,
						(contactPoint.y >> 5) - face->vertex0.y,
						(contactPoint.z >> 5) - face->vertex0.z,
						face->vertex1Offset.x,
						face->vertex1Offset.y,
						face->vertex1Offset.z,
						face->vertex2Offset.x,
						face->vertex2Offset.y,
						face->vertex2Offset.z,
						&face->firstPlaneNormal)
					!= 0)
				{
					int32_t fraction = firstStartDistance * fractionScale / distanceRange;
					if (fraction < sweep->nearestFraction)
					{
						sweep->startDistance = firstStartDistance;
						sweep->endDistance = firstEndDistance;
						sweep->nearestFraction = fraction;
						sweep->hitNormal.direction = face->firstPlaneNormal;
						sweep->face = face;
						sweep->contactFlags = contactFlags;
						foundHit = 1;
					}
				}
			}

			bool hasSecondFace = face->secondPlaneNormal.y != 0x7FFF;
			int32_t secondStartDistance = 0;
			int32_t secondEndDistance = 0;
			bool checkSecondEdges = false;
			if (hasSecondFace)
			{
				int32_t secondOriginX = face->vertex0.x + face->vertex3Offset.x;
				int32_t secondOriginY = face->vertex0.y + face->vertex3Offset.y;
				int32_t secondOriginZ = face->vertex0.z + face->vertex3Offset.z;
				secondStartDistance =
					((sweep->start.x - secondOriginX * coordinateScale) * face->secondPlaneNormal.x
						+ (sweep->start.y - secondOriginY * coordinateScale) * face->secondPlaneNormal.y
						+ (sweep->start.z - secondOriginZ * coordinateScale) * face->secondPlaneNormal.z)
					>> 14;
				secondStartDistance -= radius;
				secondEndDistance =
					((sweep->end.x - secondOriginX * coordinateScale) * face->secondPlaneNormal.x
						+ (sweep->end.y - secondOriginY * coordinateScale) * face->secondPlaneNormal.y
						+ (sweep->end.z - secondOriginZ * coordinateScale) * face->secondPlaneNormal.z)
					>> 14;
				secondEndDistance -= radius;

				if (secondStartDistance >= 0 && secondEndDistance < 0)
				{
					int32_t distanceRange = secondStartDistance - secondEndDistance;
					Vector3I contactPoint;
					contactPoint.x =
						sweep->start.x + (sweep->end.x - sweep->start.x) * secondStartDistance / distanceRange - (face->secondPlaneNormal.x * radius >> 14);
					contactPoint.y =
						sweep->start.y + (sweep->end.y - sweep->start.y) * secondStartDistance / distanceRange - (face->secondPlaneNormal.y * radius >> 14);
					contactPoint.z =
						sweep->start.z + (sweep->end.z - sweep->start.z) * secondStartDistance / distanceRange - (face->secondPlaneNormal.z * radius >> 14);
					if (Nu3D::Collision::IsPointInTriangle((contactPoint.x >> 5) - secondOriginX,
							(contactPoint.y >> 5) - secondOriginY,
							(contactPoint.z >> 5) - secondOriginZ,
							face->vertex2Offset.x - face->vertex3Offset.x,
							face->vertex2Offset.y - face->vertex3Offset.y,
							face->vertex2Offset.z - face->vertex3Offset.z,
							face->vertex1Offset.x - face->vertex3Offset.x,
							face->vertex1Offset.y - face->vertex3Offset.y,
							face->vertex1Offset.z - face->vertex3Offset.z,
							&face->secondPlaneNormal)
						!= 0)
					{
						int32_t fraction = secondStartDistance * fractionScale / distanceRange;
						if (fraction < sweep->nearestFraction)
						{
							sweep->startDistance = secondStartDistance;
							sweep->endDistance = secondEndDistance;
							sweep->nearestFraction = fraction;
							sweep->hitNormal.direction = face->secondPlaneNormal;
							sweep->face = face;
							sweep->contactFlags = contactFlags | COLLISION_CONTACT_SECONDARY_FACE;
							foundHit = 1;
						}
					}
				}

				int32_t minimumDistance = -faceTolerance - (radius >> 1);
				checkSecondEdges = (secondStartDistance < faceTolerance + 1 || secondEndDistance < faceTolerance + 1)
					&& (secondStartDistance >= minimumDistance || secondEndDistance >= minimumDistance);
			}

			int32_t minimumDistance = -faceTolerance - (radius >> 1);
			bool checkFirstEdges = (firstStartDistance < faceTolerance + 1 || firstEndDistance < faceTolerance + 1)
				&& (firstStartDistance >= minimumDistance || firstEndDistance >= minimumDistance);
			if (! checkFirstEdges && ! checkSecondEdges)
				return foundHit;

			if (checkFirstEdges)
				adjacentNormals[0] = face->firstPlaneNormal;
			else
				adjacentNormals[0].y = 0x7FFF;
			if (checkSecondEdges)
				adjacentNormals[1] = face->secondPlaneNormal;
			else
				adjacentNormals[1].y = 0x7FFF;

			int32_t vertexRadius = radius + 0x40;
			if (hasSecondFace && checkSecondEdges)
			{
				localStart.x = sweep->start.x - (face->vertex0.x + face->vertex3Offset.x) * coordinateScale;
				localStart.y = sweep->start.y - (face->vertex0.y + face->vertex3Offset.y) * coordinateScale;
				localStart.z = sweep->start.z - (face->vertex0.z + face->vertex3Offset.z) * coordinateScale;
				localEnd.x = sweep->end.x - (face->vertex0.x + face->vertex3Offset.x) * coordinateScale;
				localEnd.y = sweep->end.y - (face->vertex0.y + face->vertex3Offset.y) * coordinateScale;
				localEnd.z = sweep->end.z - (face->vertex0.z + face->vertex3Offset.z) * coordinateScale;
				if (SweepVertex(&localStart, &localEnd, sweep, vertexRadius, adjacentNormals) != 0)
				{
					sweep->face = face;
					sweep->contactFlags = contactFlags | edgeContactFlag;
					foundHit = 1;
				}
			}

			localStart.x = sweep->start.x - (face->vertex0.x + face->vertex2Offset.x) * coordinateScale;
			localStart.y = sweep->start.y - (face->vertex0.y + face->vertex2Offset.y) * coordinateScale;
			localStart.z = sweep->start.z - (face->vertex0.z + face->vertex2Offset.z) * coordinateScale;
			localEnd.x = sweep->end.x - (face->vertex0.x + face->vertex2Offset.x) * coordinateScale;
			localEnd.y = sweep->end.y - (face->vertex0.y + face->vertex2Offset.y) * coordinateScale;
			localEnd.z = sweep->end.z - (face->vertex0.z + face->vertex2Offset.z) * coordinateScale;
			if (SweepVertex(&localStart, &localEnd, sweep, vertexRadius, adjacentNormals) != 0)
			{
				sweep->face = face;
				sweep->contactFlags = contactFlags | edgeContactFlag;
				foundHit = 1;
			}

			localStart.x = sweep->start.x - (face->vertex0.x + face->vertex1Offset.x) * coordinateScale;
			localStart.y = sweep->start.y - (face->vertex0.y + face->vertex1Offset.y) * coordinateScale;
			localStart.z = sweep->start.z - (face->vertex0.z + face->vertex1Offset.z) * coordinateScale;
			localEnd.x = sweep->end.x - (face->vertex0.x + face->vertex1Offset.x) * coordinateScale;
			localEnd.y = sweep->end.y - (face->vertex0.y + face->vertex1Offset.y) * coordinateScale;
			localEnd.z = sweep->end.z - (face->vertex0.z + face->vertex1Offset.z) * coordinateScale;
			if (SweepVertex(&localStart, &localEnd, sweep, vertexRadius, adjacentNormals) != 0)
			{
				sweep->face = face;
				sweep->contactFlags = contactFlags | edgeContactFlag;
				foundHit = 1;
			}

			int32_t edgeRadius = radius + 0x20;
			if (checkFirstEdges)
			{
				localStart.x = sweep->start.x - face->vertex0.x * coordinateScale;
				localStart.y = sweep->start.y - face->vertex0.y * coordinateScale;
				localStart.z = sweep->start.z - face->vertex0.z * coordinateScale;
				localEnd.x = sweep->end.x - face->vertex0.x * coordinateScale;
				localEnd.y = sweep->end.y - face->vertex0.y * coordinateScale;
				localEnd.z = sweep->end.z - face->vertex0.z * coordinateScale;
				if (SweepVertex(&localStart, &localEnd, sweep, vertexRadius, adjacentNormals) != 0)
				{
					sweep->face = face;
					sweep->contactFlags = contactFlags | edgeContactFlag;
					foundHit = 1;
				}
				if (SweepEdge(&localStart, &localEnd, face->vertex1Offset.x, face->vertex1Offset.y, face->vertex1Offset.z, sweep, edgeRadius, adjacentNormals)
					!= 0)
				{
					sweep->face = face;
					sweep->contactFlags = contactFlags | edgeContactFlag;
					foundHit = 1;
				}
				if (SweepEdge(&localStart, &localEnd, face->vertex2Offset.x, face->vertex2Offset.y, face->vertex2Offset.z, sweep, edgeRadius, adjacentNormals)
					!= 0)
				{
					sweep->face = face;
					sweep->contactFlags = contactFlags | edgeContactFlag;
					foundHit = 1;
				}
			}

			localStart.x = sweep->start.x - (face->vertex0.x + face->vertex1Offset.x) * coordinateScale;
			localStart.y = sweep->start.y - (face->vertex0.y + face->vertex1Offset.y) * coordinateScale;
			localStart.z = sweep->start.z - (face->vertex0.z + face->vertex1Offset.z) * coordinateScale;
			localEnd.x = sweep->end.x - (face->vertex0.x + face->vertex1Offset.x) * coordinateScale;
			localEnd.y = sweep->end.y - (face->vertex0.y + face->vertex1Offset.y) * coordinateScale;
			localEnd.z = sweep->end.z - (face->vertex0.z + face->vertex1Offset.z) * coordinateScale;
			if (SweepEdge(&localStart,
					&localEnd,
					face->vertex2Offset.x - face->vertex1Offset.x,
					face->vertex2Offset.y - face->vertex1Offset.y,
					face->vertex2Offset.z - face->vertex1Offset.z,
					sweep,
					edgeRadius,
					adjacentNormals)
				!= 0)
			{
				sweep->face = face;
				sweep->contactFlags = contactFlags | edgeContactFlag;
				foundHit = 1;
			}

			if (hasSecondFace && checkSecondEdges)
			{
				localStart.x = sweep->start.x - (face->vertex0.x + face->vertex3Offset.x) * coordinateScale;
				localStart.y = sweep->start.y - (face->vertex0.y + face->vertex3Offset.y) * coordinateScale;
				localStart.z = sweep->start.z - (face->vertex0.z + face->vertex3Offset.z) * coordinateScale;
				localEnd.x = sweep->end.x - (face->vertex0.x + face->vertex3Offset.x) * coordinateScale;
				localEnd.y = sweep->end.y - (face->vertex0.y + face->vertex3Offset.y) * coordinateScale;
				localEnd.z = sweep->end.z - (face->vertex0.z + face->vertex3Offset.z) * coordinateScale;
				if (SweepEdge(&localStart,
						&localEnd,
						face->vertex2Offset.x - face->vertex3Offset.x,
						face->vertex2Offset.y - face->vertex3Offset.y,
						face->vertex2Offset.z - face->vertex3Offset.z,
						sweep,
						edgeRadius,
						adjacentNormals)
					!= 0)
				{
					sweep->face = face;
					sweep->contactFlags = contactFlags | edgeContactFlag;
					foundHit = 1;
				}
				if (SweepEdge(&localStart,
						&localEnd,
						face->vertex1Offset.x - face->vertex3Offset.x,
						face->vertex1Offset.y - face->vertex3Offset.y,
						face->vertex1Offset.z - face->vertex3Offset.z,
						sweep,
						edgeRadius,
						adjacentNormals)
					!= 0)
				{
					sweep->face = face;
					sweep->contactFlags = contactFlags | edgeContactFlag;
					foundHit = 1;
				}
			}

			return foundHit;
		}

		// GLOBAL: TOY2 0x00729170
		int32_t g_lastCollisionEndDistance;

		// GLOBAL: TOY2 0x0072959C
		int32_t g_lastCollisionStartDistance;

		// GLOBAL: TOY2 0x0072D298
		int32_t g_hadGroundResponse;

		static __forceinline int32_t RotateTransposeX(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{
			return ShiftTowardZero(matrix.m00 * x + matrix.m10 * y + matrix.m20 * z, 12);
		}

		static __forceinline int32_t RotateTransposeY(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{
			return ShiftTowardZero(matrix.m01 * x + matrix.m11 * y + matrix.m21 * z, 12);
		}

		static __forceinline int32_t RotateTransposeZ(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{
			return ShiftTowardZero(matrix.m02 * x + matrix.m12 * y + matrix.m22 * z, 12);
		}

		static __forceinline int32_t RotateX(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{
			return ShiftTowardZero(matrix.m00 * x + matrix.m01 * y + matrix.m02 * z, 12);
		}

		static __forceinline int32_t RotateY(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{
			return ShiftTowardZero(matrix.m10 * x + matrix.m11 * y + matrix.m12 * z, 12);
		}

		static __forceinline int32_t RotateZ(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{
			return ShiftTowardZero(matrix.m20 * x + matrix.m21 * y + matrix.m22 * z, 12);
		}

		static __forceinline void RemoveNormalComponent(Vector3I16* movement, const Vector3I16& normal, int32_t shift)
		{
			int32_t dot = movement->x * normal.x + movement->y * normal.y + movement->z * normal.z;
			int32_t scale = -ShiftTowardZero(dot, shift);
			movement->x += (int16_t)ShiftTowardZero(normal.x * scale, 17);
			movement->y += (int16_t)ShiftTowardZero(normal.y * scale, 17);
			movement->z += (int16_t)ShiftTowardZero(normal.z * scale, 17);
		}

		static __forceinline void RemoveNormalComponent(Vector3I* movement, const Vector3I16& normal, int32_t shift)
		{
			int32_t dot = movement->x * normal.x + movement->y * normal.y + movement->z * normal.z;
			int32_t scale = -ShiftTowardZero(dot, shift);
			movement->x += (int16_t)ShiftTowardZero(normal.x * scale, 17);
			movement->y += (int16_t)ShiftTowardZero(normal.y * scale, 17);
			movement->z += (int16_t)ShiftTowardZero(normal.z * scale, 17);
		}

		// FUNCTION: TOY2 0x00482A00 [PROVISIONAL]
		int32_t ResolveSubstep(CollisionStepMotion* motion,
			Vector3I* position,
			Vector3I* velocity,
			SurfaceCollisionResult* result,
			const int16_t* meshIndices,
			PackedCollisionFace** faces,
			int32_t faceCount,
			int32_t movePlatforms)
		{
			Vector3I totalMovement;
			totalMovement.x = motion->movement.x + motion->platformMovement.x;
			totalMovement.y = motion->movement.y + motion->platformMovement.y;
			totalMovement.z = motion->movement.z + motion->platformMovement.z;

			CollisionSweep sweep;
			sweep.contactFlags = COLLISION_NO_CONTACT;
			sweep.nearestFraction = 0x7FFFFFFF;
			bool triangleHit = false;
			int16_t preparedMeshIndex = -1;

			for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++)
			{
				PackedCollisionFace* face = faces[faceIndex];
				int16_t meshIndex = meshIndices[faceIndex];
				if (meshIndex != preparedMeshIndex)
				{
					CollisionMeshInstance& mesh = g_collisionMeshInstances[meshIndex];
					sweep.start.x = position->x - mesh.origin.x;
					sweep.start.y = position->y - mesh.origin.y;
					sweep.start.z = position->z - mesh.origin.z;

					if (mesh.typeFlags == COLLISION_MESH_MOVING)
					{
						Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
						if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
						{
							Vector3I16 angles;
							angles.x = platform.rotationAnglesFixed.x >> 2;
							angles.y = platform.rotationAnglesFixed.y >> 2;
							angles.z = platform.rotationAnglesFixed.z >> 2;
							Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
							int32_t relativeX = sweep.start.x;
							int32_t relativeY = sweep.start.y;
							int32_t relativeZ = sweep.start.z;
							sweep.start.x = RotateTransposeX(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
							sweep.start.y = RotateTransposeY(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
							sweep.start.z = RotateTransposeZ(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);

							angles.x = (platform.rotationAnglesFixed.x + platform.remainingRotation.x) >> 2;
							angles.y = (platform.rotationAnglesFixed.y + platform.remainingRotation.y) >> 2;
							angles.z = (platform.rotationAnglesFixed.z + platform.remainingRotation.z) >> 2;
							Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
							relativeX = position->x - mesh.origin.x + totalMovement.x;
							relativeY = position->y - mesh.origin.y + totalMovement.y;
							relativeZ = position->z - mesh.origin.z + totalMovement.z;
							if (movePlatforms != 0)
							{
								relativeX -= platform.remainingTranslation.x;
								relativeY -= platform.remainingTranslation.y;
								relativeZ -= platform.remainingTranslation.z;
							}
							sweep.end.x = RotateTransposeX(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
							sweep.end.y = RotateTransposeY(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
							sweep.end.z = RotateTransposeZ(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
						}
						else
						{
							sweep.end.x = sweep.start.x + totalMovement.x;
							sweep.end.y = sweep.start.y + totalMovement.y;
							sweep.end.z = sweep.start.z + totalMovement.z;
							if (movePlatforms != 0)
							{
								sweep.end.x -= platform.remainingTranslation.x;
								sweep.end.y -= platform.remainingTranslation.y;
								sweep.end.z -= platform.remainingTranslation.z;
							}
						}
					}
					else
					{
						sweep.end.x = sweep.start.x + totalMovement.x;
						sweep.end.y = sweep.start.y + totalMovement.y;
						sweep.end.z = sweep.start.z + totalMovement.z;
					}

					sweep.movementDirection.x = sweep.end.x - sweep.start.x;
					sweep.movementDirection.y = sweep.end.y - sweep.start.y;
					sweep.movementDirection.z = sweep.end.z - sweep.start.z;
					while (abs(sweep.movementDirection.x) > 0x4000 || abs(sweep.movementDirection.y) > 0x4000
						|| abs(sweep.movementDirection.z) > 0x4000)
					{
						sweep.movementDirection.x >>= 1;
						sweep.movementDirection.y >>= 1;
						sweep.movementDirection.z >>= 1;
					}
					Nu3D::Math::NormalizeToFixedPoint(&sweep.movementDirection, &sweep.movementDirection);
					sweep.movementLength = (((sweep.end.x - sweep.start.x) * sweep.movementDirection.x
						+ (sweep.end.y - sweep.start.y) * sweep.movementDirection.y
						+ (sweep.end.z - sweep.start.z) * sweep.movementDirection.z)
						>> 12)
						+ 0x100;
					preparedMeshIndex = meshIndex;
				}

				if (SweepTriangle(face, &sweep, result->collisionDistance, (uint16_t)meshIndex) != 0)
					triangleHit = true;
			}

			int32_t wallResult = SweepWallSegments(&sweep, position, &totalMovement, result->collisionDistance);
			if (wallResult == 1)
				sweep.contactFlags = COLLISION_NO_CONTACT;
			else if (! triangleHit)
			{
				if (movePlatforms != 0)
					Platform::AdvancePartialMotion(100, 0, 0);
				position->x += totalMovement.x;
				position->y += totalMovement.y;
				position->z += totalMovement.z;
				motion->movement.x = motion->movement.y = motion->movement.z = 0;
				motion->platformMovement.x = motion->platformMovement.y = motion->platformMovement.z = 0;
				return 0;
			}

			bool movingContact = sweep.contactFlags != COLLISION_NO_CONTACT
				&& g_collisionMeshInstances[sweep.contactFlags & COLLISION_CONTACT_MESH_INDEX_MASK].typeFlags == COLLISION_MESH_MOVING;
			if (movingContact
				&& (Platform::g_platformStates[g_collisionMeshInstances[sweep.contactFlags & COLLISION_CONTACT_MESH_INDEX_MASK].platformIdx].flags
					& Platform::PLATFORM_FLAG_ROTATED)
					!= 0)
				sweep.endDistance -= 0x80;
			else
				sweep.endDistance -= 0x60;
			sweep.startDistance = (sweep.startDistance - 0x20) >> 3;
			sweep.endDistance >>= 3;
			int32_t distanceRange = sweep.startDistance - sweep.endDistance;
			g_lastCollisionEndDistance = sweep.endDistance;
			g_lastCollisionStartDistance = sweep.startDistance;
			position->x += sweep.startDistance * totalMovement.x / distanceRange;
			position->y += sweep.startDistance * totalMovement.y / distanceRange;
			position->z += sweep.startDistance * totalMovement.z / distanceRange;
			if (movePlatforms != 0)
				Platform::AdvancePartialMotion(sweep.startDistance, sweep.endDistance, 0);

			Vector3I16 normal = sweep.hitNormal.direction;
			int32_t platformIndex = -1;
			if (movingContact)
			{
				platformIndex = g_collisionMeshInstances[sweep.contactFlags & COLLISION_CONTACT_MESH_INDEX_MASK].platformIdx;
				result->platformIndex = (int16_t)platformIndex;
				Platform::PlatformState& platform = Platform::g_platformStates[platformIndex];
				if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
				{
					Vector3I16 angles;
					angles.x = platform.rotationAnglesFixed.x >> 2;
					angles.y = platform.rotationAnglesFixed.y >> 2;
					angles.z = platform.rotationAnglesFixed.z >> 2;
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
					normal.x = (int16_t)RotateX(Animation::g_keyframeRotation.matrix,
						sweep.hitNormal.direction.x,
						sweep.hitNormal.direction.y,
						sweep.hitNormal.direction.z);
					normal.y = (int16_t)RotateY(Animation::g_keyframeRotation.matrix,
						sweep.hitNormal.direction.x,
						sweep.hitNormal.direction.y,
						sweep.hitNormal.direction.z);
					normal.z = (int16_t)RotateZ(Animation::g_keyframeRotation.matrix,
						sweep.hitNormal.direction.x,
						sweep.hitNormal.direction.y,
						sweep.hitNormal.direction.z);
				}
			}

			motion->movement.x -= (int16_t)(motion->movement.x * sweep.startDistance / distanceRange);
			motion->movement.y -= (int16_t)(motion->movement.y * sweep.startDistance / distanceRange);
			motion->movement.z -= (int16_t)(motion->movement.z * sweep.startDistance / distanceRange);
			if (! movingContact)
			{
				motion->platformMovement.x -= (int16_t)(motion->platformMovement.x * sweep.startDistance / distanceRange);
				motion->platformMovement.y -= (int16_t)(motion->platformMovement.y * sweep.startDistance / distanceRange);
				motion->platformMovement.z -= (int16_t)(motion->platformMovement.z * sweep.startDistance / distanceRange);
			}

			if (normal.y < -0x2000)
			{
				RemoveNormalComponent(&motion->movement, normal, 11);
				if (! movingContact)
					RemoveNormalComponent(&motion->platformMovement, normal, 11);
				else
				{
					Platform::PlatformState& platform = Platform::g_platformStates[platformIndex];
					motion->platformMovement.x = platform.remainingTranslation.x + (normal.x >> 8);
					motion->platformMovement.y = platform.remainingTranslation.y + (normal.y >> 8);
					motion->platformMovement.z = platform.remainingTranslation.z + (normal.z >> 8);
					if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
					{
						Vector3I16 angles;
						angles.x = platform.rotationAnglesFixed.x >> 2;
						angles.y = platform.rotationAnglesFixed.y >> 2;
						angles.z = platform.rotationAnglesFixed.z >> 2;
						Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
						angles.x = (platform.rotationAnglesFixed.x + platform.remainingRotation.x) >> 2;
						angles.y = (platform.rotationAnglesFixed.y + platform.remainingRotation.y) >> 2;
						angles.z = (platform.rotationAnglesFixed.z + platform.remainingRotation.z) >> 2;
						Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_nextKeyframeRotation.matrix);
						CollisionMeshInstance& mesh = g_collisionMeshInstances[platform.collisionMeshIndex];
						int32_t relativeX = position->x - mesh.origin.x;
						int32_t relativeY = position->y - mesh.origin.y;
						int32_t relativeZ = position->z - mesh.origin.z;
						int32_t localX = RotateTransposeX(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
						int32_t localY = RotateTransposeY(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
						int32_t localZ = RotateTransposeZ(Animation::g_keyframeRotation.matrix, relativeX, relativeY, relativeZ);
						int32_t movedX = RotateX(Animation::g_nextKeyframeRotation.matrix, localX, localY, localZ) + mesh.origin.x;
						int32_t movedY = RotateY(Animation::g_nextKeyframeRotation.matrix, localX, localY, localZ) + mesh.origin.y;
						int32_t movedZ = RotateZ(Animation::g_nextKeyframeRotation.matrix, localX, localY, localZ) + mesh.origin.z;
						motion->platformMovement.x += (int16_t)movedX - (int16_t)position->x;
						int16_t oldMovementY = motion->platformMovement.y;
						motion->platformMovement.y += (int16_t)movedY - (int16_t)position->y;
						motion->platformMovement.z += (int16_t)movedZ - (int16_t)position->z;
						if (motion->platformMovement.y < oldMovementY)
							motion->platformMovement.y += (motion->platformMovement.y - oldMovementY) / 8 + 0x20;
					}
					platform.flags |= Platform::PLATFORM_FLAG_BUZZ_CONTACT;
					platform.contactFace = reinterpret_cast<Platform::CollisionFace*>(sweep.face);
				}
				RemoveNormalComponent(velocity, normal, 11);
			}
			else
			{
				if (Renderer::g_frameDelta >= 3)
				{
					velocity->x >>= 1;
					velocity->z >>= 1;
				}
				else
				{
					velocity->x = ShiftTowardZero(velocity->x * 15, 4);
					velocity->z = ShiftTowardZero(velocity->z * 15, 4);
				}

				int32_t normalShift = g_hadGroundResponse == 0 ? 11 : 10;
				if (normal.y < 1 || motion->movement.y < 1)
					RemoveNormalComponent(&motion->movement, normal, normalShift);
				if (! movingContact)
				{
					if (normal.y < 1 || motion->platformMovement.y < 1)
						RemoveNormalComponent(&motion->platformMovement, normal, normalShift);
				}
				else
				{
					Platform::PlatformState& platform = Platform::g_platformStates[platformIndex];
					velocity->y += normal.y >> 6;
					motion->movement.y += normal.y >> 6;
					motion->platformMovement.x = platform.remainingTranslation.x + (normal.x >> 8);
					motion->platformMovement.z = platform.remainingTranslation.z + (normal.z >> 8);
					if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
					{
						motion->movement.x += normal.x >> 7;
						motion->movement.z += normal.z >> 7;
						motion->platformMovement.x += normal.x >> 7;
						motion->platformMovement.z += normal.z >> 7;
					}
					platform.flags |= PLATFORM_FLAG_COLLISION_SUPPORT;
					platform.contactFace = reinterpret_cast<Platform::CollisionFace*>(sweep.face);
				}

				result->normal = normal;
				result->contactState = movingContact ? 2 : 1;
				if (normal.y < 1 || velocity->y < 1)
					RemoveNormalComponent(velocity, normal, normalShift);
				motion->movement.x += normal.x / 0x60;
				motion->movement.y += normal.y / 0x60;
				motion->movement.z += normal.z / 0x60;
				velocity->x += normal.x / 0x60;
				velocity->z += normal.z / 0x60;
				g_hadGroundResponse = 1;
			}

			if (sweep.contactFlags != COLLISION_NO_CONTACT)
			{
				uint8_t surfaceType = (uint8_t)g_collisionMeshInstances[sweep.contactFlags & COLLISION_CONTACT_MESH_INDEX_MASK].unk;
				if (surfaceType != 0xFF)
					result->surfaceType = (result->surfaceType & 0xFF00) | surfaceType;
			}
			if (normal.y >= -0x2000)
				return 2;
			result->contactFlags = sweep.contactFlags;
			result->face = reinterpret_cast<Platform::CollisionFace*>(sweep.face);
			result->wallNormal = normal;
			return 1;
		}

		// FUNCTION: TOY2 0x00483EF0 [PROVISIONAL]
		int32_t ResolvePlatformFooting(CollisionStepMotion* motion,
			Vector3I* position,
			Vector3I*,
			SurfaceCollisionResult* result)
		{
			Platform::CollisionFace* contactFace = result->face;
			int16_t meshIndex = (int16_t)(result->contactFlags & COLLISION_CONTACT_MESH_INDEX_MASK);
			CollisionMeshInstance& mesh = g_collisionMeshInstances[meshIndex];
			uint8_t surfaceType = (uint8_t)mesh.unk;
			if (surfaceType != 0xFF)
				result->surfaceType ^= (uint8_t)result->surfaceType ^ surfaceType;

			Vector3I16 primaryNormal;
			Vector3I16 secondaryNormal;
			if (mesh.typeFlags == COLLISION_MESH_MOVING
				&& (Platform::g_platformStates[mesh.platformIdx].flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
			{
				Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
				Vector3I16 angles;
				angles.x = platform.rotationAnglesFixed.x >> 2;
				angles.y = platform.rotationAnglesFixed.y >> 2;
				angles.z = platform.rotationAnglesFixed.z >> 2;
				Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);

				primaryNormal.x = (int16_t)RotateX(
					Animation::g_keyframeRotation.matrix, contactFace->normal.x, contactFace->normal.y, contactFace->normal.z);
				primaryNormal.y = (int16_t)RotateY(
					Animation::g_keyframeRotation.matrix, contactFace->normal.x, contactFace->normal.y, contactFace->normal.z);
				primaryNormal.z = (int16_t)RotateZ(
					Animation::g_keyframeRotation.matrix, contactFace->normal.x, contactFace->normal.y, contactFace->normal.z);

				if (contactFace->secondaryNormal.y != 0x7FFF)
				{
					secondaryNormal.x = (int16_t)RotateX(Animation::g_keyframeRotation.matrix,
						contactFace->secondaryNormal.x,
						contactFace->secondaryNormal.y,
						contactFace->secondaryNormal.z);
					secondaryNormal.y = (int16_t)RotateY(Animation::g_keyframeRotation.matrix,
						contactFace->secondaryNormal.x,
						contactFace->secondaryNormal.y,
						contactFace->secondaryNormal.z);
					secondaryNormal.z = (int16_t)RotateZ(Animation::g_keyframeRotation.matrix,
						contactFace->secondaryNormal.x,
						contactFace->secondaryNormal.y,
						contactFace->secondaryNormal.z);
				}
				else
					secondaryNormal = contactFace->secondaryNormal;
			}
			else
			{
				primaryNormal = contactFace->normal;
				secondaryNormal = contactFace->secondaryNormal;
			}

			Vector3I combinedMovement;
			combinedMovement.x = motion->movement.x + motion->platformMovement.x;
			combinedMovement.y = motion->movement.y + motion->platformMovement.y;
			combinedMovement.z = motion->movement.z + motion->platformMovement.z;

			Vector3I stepPosition;
			stepPosition.x = position->x + combinedMovement.x;
			stepPosition.y = position->y + combinedMovement.y;
			stepPosition.z = position->z + combinedMovement.z;

			const Vector3I16& contactNormal =
				(result->contactFlags & COLLISION_CONTACT_SECONDARY_FACE) != 0 ? secondaryNormal : primaryNormal;
			stepPosition.y += contactNormal.y >> 8;

			CollisionStepMotion stepMotion;
			stepMotion.movement.x = 0;
			stepMotion.movement.y = (int16_t)(-contactNormal.y >> 6);
			stepMotion.movement.z = 0;
			stepMotion.platformMovement.x = 0;
			stepMotion.platformMovement.y = 0;
			stepMotion.platformMovement.z = 0;

			SurfaceCollisionResult stepResult = *result;
			PackedCollisionFace* face = reinterpret_cast<PackedCollisionFace*>(contactFace);
			if (ResolveSubstep(&stepMotion, &stepPosition, &combinedMovement, &stepResult, &meshIndex, &face, 1, 0) == 1)
			{
				const Vector3I16& resolvedNormal = (stepResult.contactFlags & COLLISION_CONTACT_SECONDARY_FACE) != 0
					? secondaryNormal
					: primaryNormal;
				motion->movement.x =
					(int16_t)((resolvedNormal.x >> 9) - (int16_t)position->x - motion->platformMovement.x + (int16_t)stepPosition.x);
				motion->movement.y =
					(int16_t)((resolvedNormal.y >> 8) - (int16_t)position->y - motion->platformMovement.y + (int16_t)stepPosition.y);
				motion->movement.z =
					(int16_t)((resolvedNormal.z >> 9) - (int16_t)position->z - motion->platformMovement.z + (int16_t)stepPosition.z);
				return 1;
			}

			if (secondaryNormal.y != 0x7FFF)
			{
				motion->movement.x += (int16_t)((secondaryNormal.x + primaryNormal.x) >> 9);
				motion->movement.y += (int16_t)((secondaryNormal.y + primaryNormal.y) >> 9);
				motion->movement.z += (int16_t)((secondaryNormal.z + primaryNormal.z) >> 9);
			}
			else
			{
				motion->movement.x += primaryNormal.x >> 9;
				motion->movement.y += primaryNormal.y >> 9;
				motion->movement.z += primaryNormal.z >> 9;
			}
			return 0;
		}

		// FUNCTION: TOY2 0x00485940 [PROVISIONAL]
		void GatherTrianglesAtXZ(const Vector3I* position)
		{
			const int32_t cacheSlotCount = 8;
			const int32_t maxTriangleCount = 240;
			const int32_t cachedQueryOffset = 0x1000;
			const int32_t cachedQueryDiameter = 0x2000;
			const int32_t cachePositionRadius = 0x2000;
			const int32_t cachePositionDiameter = 0x4000;
			const int32_t cacheBuildOffset = 0x5000;
			const int32_t cacheBuildDiameter = 0xA000;
			const int32_t cachedFaceTolerance = 0x100;
			const int32_t cacheBuildFaceTolerance = 0x500;
			Vector3I queryPosition = *position;
			CollisionMeshRecord* collisionMeshes = reinterpret_cast<CollisionMeshRecord*>(g_collisionMeshInstances);
			int16_t* candidateMeshIndices = reinterpret_cast<int16_t*>(g_mathScratch);
			int32_t cacheSlotIndex = -1;
			int32_t replacementSlotIndex = -1;
			int32_t lowestActivity = 0x7FFFFFFF;

			for (int32_t slotIndex = 0; slotIndex < cacheSlotCount; slotIndex++)
			{
				Terrain::CollisionWorkspaceSlot& slot = Terrain::g_collisionWorkspace->slots[slotIndex];
				if ((uint32_t)(queryPosition.x - slot.cachePosition.cachedPositionX + cachePositionRadius) < cachePositionDiameter
					&& (uint32_t)(queryPosition.z - slot.cachePosition.cachedPositionZ + cachePositionRadius) < cachePositionDiameter && slot.activeFrames > 0)
				{
					cacheSlotIndex = slotIndex;
					break;
				}

				if (slot.activeFrames < lowestActivity)
				{
					lowestActivity = slot.activeFrames;
					replacementSlotIndex = slotIndex;
				}
			}

			g_collisionTriangleCount = 0;

			if (cacheSlotIndex >= 0)
			{
				Terrain::CollisionWorkspaceSlot& slot = Terrain::g_collisionWorkspace->slots[cacheSlotIndex];
				slot.activeFrames = 8;
				int32_t previousMeshIndex = -1;
				bool meshIsInRange = false;
				int32_t localX = 0;
				int32_t localZ = 0;

				for (int32_t entryIndex = slot.entryCount - 1; entryIndex >= 0; entryIndex--)
				{
					int32_t meshIndex = slot.meshIndices[entryIndex];
					if (meshIndex != previousMeshIndex)
					{
						previousMeshIndex = meshIndex;
						meshIsInRange = false;
						CollisionMeshRecord& mesh = collisionMeshes[meshIndex];
						if (queryPosition.x + cachedQueryOffset - mesh.boundsMin.x < mesh.boundsExt.x + cachedQueryDiameter
							&& queryPosition.z + cachedQueryOffset - mesh.boundsMin.z < mesh.boundsExt.z + cachedQueryDiameter && mesh.typeFlags != 0
							&& (mesh.flags & COLLISION_MESH_EXCLUDE_FROM_QUERY) == 0)
						{
							localX = ShiftTowardZero(queryPosition.x + cachedQueryOffset - mesh.origin.x, 5);
							localZ = ShiftTowardZero(queryPosition.z + cachedQueryOffset - mesh.origin.z, 5);
							meshIsInRange = true;
						}
					}

					if (meshIsInRange)
					{
						PackedCollisionFace* face = slot.faces[entryIndex];
						if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + cachedFaceTolerance)
							&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < (uint32_t)((face->boundsExtentZBlock + 4) * 64)
							&& g_collisionTriangleCount < maxTriangleCount)
						{
							g_collisionTriangles[g_collisionTriangleCount] = face;
							g_collisionTriangleMeshIndices[g_collisionTriangleCount] = (int16_t)meshIndex;
							g_collisionTriangleCount++;
						}
					}
				}
			}
			else
			{
				int32_t candidateMeshCount = 0;
				for (int32_t cellIndex = 0; cellIndex < g_activeCollisionGridCellCount; cellIndex++)
				{
					CollisionGridCell& cell = g_collisionGrid[cellIndex];
					if ((uint32_t)(queryPosition.x + cacheBuildOffset - cell.boundsMinX) < (uint32_t)(cell.boundsExtentX + cacheBuildDiameter)
						&& (uint32_t)(queryPosition.z + cacheBuildOffset - cell.boundsMinZ) < (uint32_t)(cell.boundsExtentZ + cacheBuildDiameter))
					{
						for (int32_t meshListIndex = 0; meshListIndex < cell.meshCount; meshListIndex++)
						{
							candidateMeshIndices[candidateMeshCount++] = g_collisionGridMeshIndices[cell.meshListStart + meshListIndex];
						}
					}
				}

				cacheSlotIndex = replacementSlotIndex;
				Terrain::CollisionWorkspaceSlot& slot = Terrain::g_collisionWorkspace->slots[cacheSlotIndex];
				slot.entryCount = 0;

				for (int32_t candidateIndex = 0; candidateIndex < candidateMeshCount; candidateIndex++)
				{
					int32_t meshIndex = candidateMeshIndices[candidateIndex];
					CollisionMeshRecord& mesh = collisionMeshes[meshIndex];
					if (queryPosition.x + cacheBuildOffset - mesh.boundsMin.x < mesh.boundsExt.x + cacheBuildDiameter
						&& queryPosition.z + cacheBuildOffset - mesh.boundsMin.z < mesh.boundsExt.z + cacheBuildDiameter && mesh.typeFlags != 0
						&& (mesh.flags & COLLISION_MESH_EXCLUDE_FROM_QUERY) == 0)
					{
						int32_t localX = ShiftTowardZero(queryPosition.x + cacheBuildOffset - mesh.origin.x, 5);
						int32_t localZ = ShiftTowardZero(queryPosition.z + cacheBuildOffset - mesh.origin.z, 5);
						CollisionTreeGroup* group = mesh.collisionTree;

						while (group->marker >= 0)
						{
							int32_t faceCount = group->faceCount;
							PackedCollisionFace* face = reinterpret_cast<PackedCollisionFace*>(group + 1);
							if ((uint32_t)(localX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + cacheBuildFaceTolerance)
								&& (uint32_t)(localZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + cacheBuildFaceTolerance))
							{
								for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
								{
									if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + cacheBuildFaceTolerance)
										&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < (uint32_t)((face->boundsExtentZBlock + 20) * 64)
										&& slot.entryCount < maxTriangleCount)
									{
										slot.faces[slot.entryCount] = face;
										slot.meshIndices[slot.entryCount] = (int16_t)meshIndex;
										slot.entryCount++;
									}
								}
							}
							else
							{
								face += faceCount;
							}
							group = reinterpret_cast<CollisionTreeGroup*>(face);
						}
					}
				}

				slot.activeFrames = 8;
				slot.cachePosition.cachedPositionX = queryPosition.x;
				slot.cachePosition.cachedPositionZ = queryPosition.z;
				int32_t previousMeshIndex = -1;
				bool meshIsInRange = false;
				int32_t localX = 0;
				int32_t localZ = 0;

				for (int32_t entryIndex = slot.entryCount - 1; entryIndex >= 0; entryIndex--)
				{
					int32_t meshIndex = slot.meshIndices[entryIndex];
					if (meshIndex != previousMeshIndex)
					{
						previousMeshIndex = meshIndex;
						meshIsInRange = false;
						CollisionMeshRecord& mesh = collisionMeshes[meshIndex];
						if (queryPosition.x + cachedQueryOffset - mesh.boundsMin.x < mesh.boundsExt.x + cachedQueryDiameter
							&& queryPosition.z + cachedQueryOffset - mesh.boundsMin.z < mesh.boundsExt.z + cachedQueryDiameter && mesh.typeFlags != 0
							&& (mesh.flags & COLLISION_MESH_EXCLUDE_FROM_QUERY) == 0)
						{
							localX = ShiftTowardZero(queryPosition.x + cachedQueryOffset - mesh.origin.x, 5);
							localZ = ShiftTowardZero(queryPosition.z + cachedQueryOffset - mesh.origin.z, 5);
							meshIsInRange = true;
						}
					}

					if (meshIsInRange)
					{
						PackedCollisionFace* face = slot.faces[entryIndex];
						if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + cachedFaceTolerance)
							&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < (uint32_t)((face->boundsExtentZBlock + 4) * 64)
							&& g_collisionTriangleCount < maxTriangleCount)
						{
							g_collisionTriangles[g_collisionTriangleCount] = face;
							g_collisionTriangleMeshIndices[g_collisionTriangleCount] = (int16_t)meshIndex;
							g_collisionTriangleCount++;
						}
					}
				}
			}

			CollisionGridCell& movingCell = g_collisionGrid[256];
			int32_t movingListIndex;
			for (movingListIndex = 0; movingListIndex < movingCell.meshCount; movingListIndex++)
			{
				candidateMeshIndices[movingListIndex] = g_collisionGridMeshIndices[movingCell.meshListStart + movingListIndex];
			}

			for (movingListIndex = 0; movingListIndex < movingCell.meshCount; movingListIndex++)
			{
				int32_t meshIndex = candidateMeshIndices[movingListIndex];
				CollisionMeshRecord& mesh = collisionMeshes[meshIndex];
				Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
				int32_t queryX;
				int32_t queryZ;

				if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) == 0)
				{
					queryX = queryPosition.x + cachedQueryOffset;
					queryZ = queryPosition.z + cachedQueryOffset;
				}
				else
				{
					Animation::g_nextKeyframeRotation.angles.x = platform.rotationAnglesFixed.x >> 2;
					Animation::g_nextKeyframeRotation.angles.y = platform.rotationAnglesFixed.y >> 2;
					Animation::g_nextKeyframeRotation.angles.z = platform.rotationAnglesFixed.z >> 2;
					Nu3D::Math::SetRotationXYZ(&Animation::g_nextKeyframeRotation.angles, &Animation::g_keyframeRotation.matrix);

					int32_t deltaX = queryPosition.x - mesh.origin.x;
					int32_t deltaY = queryPosition.y - mesh.origin.y;
					int32_t deltaZ = queryPosition.z - mesh.origin.z;
					queryX =
						ShiftTowardZero(Animation::g_keyframeRotation.matrix.m00 * deltaX + Animation::g_keyframeRotation.matrix.m10 * deltaY
								+ Animation::g_keyframeRotation.matrix.m20 * deltaZ,
							12)
						+ cachedQueryOffset + mesh.origin.x;
					queryZ =
						ShiftTowardZero(Animation::g_keyframeRotation.matrix.m02 * deltaX + Animation::g_keyframeRotation.matrix.m12 * deltaY
								+ Animation::g_keyframeRotation.matrix.m22 * deltaZ,
							12)
						+ cachedQueryOffset + mesh.origin.z;
				}

				if (queryX - mesh.boundsMin.x < mesh.boundsExt.x + cachedQueryDiameter && queryZ - mesh.boundsMin.z < mesh.boundsExt.z + cachedQueryDiameter
					&& mesh.typeFlags != 0)
				{
					int32_t localX = ShiftTowardZero(queryX - mesh.origin.x, 5);
					int32_t localZ = ShiftTowardZero(queryZ - mesh.origin.z, 5);
					CollisionTreeGroup* group = mesh.collisionTree;

					while (group->marker >= 0)
					{
						int32_t faceCount = group->faceCount;
						PackedCollisionFace* face = reinterpret_cast<PackedCollisionFace*>(group + 1);
						if ((uint32_t)(localX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + cachedFaceTolerance)
							&& (uint32_t)(localZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + cachedFaceTolerance))
						{
							for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
							{
								if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + cachedFaceTolerance)
									&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < (uint32_t)((face->boundsExtentZBlock + 4) * 64)
									&& g_collisionTriangleCount < maxTriangleCount)
								{
									g_collisionTriangles[g_collisionTriangleCount] = face;
									g_collisionTriangleMeshIndices[g_collisionTriangleCount] = (int16_t)meshIndex;
									g_collisionTriangleCount++;
								}
							}
						}
						else
						{
							face += faceCount;
						}
						group = reinterpret_cast<CollisionTreeGroup*>(face);
					}
				}
			}
		}

		// STUB: TOY2 0x00486520
		void ResolveGroundCeiling(PosAndAngles* position, int32_t radius) {}

		// FUNCTION: TOY2 0x0048B660 [MATCHED]
		void ApplySurfaceVelocity(int32_t queryIndex, int32_t x, int32_t y, int32_t z)
		{
			if (g_ledgeClimbTimer != 0 || g_poleClimbState != 0 || g_ziplineState != 0)
				return;

			if (reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].contactState != 0
				&& reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].normal.x * x
						+ reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].normal.y * y
						+ reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].normal.z * z
					< 0)
			{
				g_surfaceVelocityBlend = 0;
				return;
			}

			int32_t blend = g_surfaceVelocityBlend;
			if (blend < 0x1000)
			{
				blend += 0x100;
				g_surfaceVelocityBlend = blend;
			}

			reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].surfaceVelocity.x += (int16_t)(blend * x / 0x1000);
			reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].surfaceVelocity.y += (int16_t)(blend * y / 0x1000);
			reinterpret_cast<SurfaceCollisionResult*>(g_collisionQueryResults)[queryIndex].surfaceVelocity.z += (int16_t)(blend * z / 0x1000);
		}
	}

	namespace Platform
	{
		// FUNCTION: TOY2 0x0049EC00 [PROVISIONAL]
		int32_t StepTiltPhysics(int32_t platformIndex,
			int32_t linkId,
			int32_t angularVelocity,
			int32_t motionMode,
			int32_t minimumAngle,
			int32_t maximumAngle,
			int32_t angularDivisor)
		{
			if (g_ledgeClimbPlatformIndex == platformIndex)
				return 0;

			PosAndAngles platformTransform;
			if (g_buzzActor.collisionFlags != 0 && (GetFlags(platformIndex) & 3) == PLATFORM_FLAG_BUZZ_CONTACT)
			{
				GetRotationAngles(platformIndex, &platformTransform.pos);
				int32_t tiltSin = Numerics::g_sinCosLUT[(platformTransform.pos.y - 0x800) & 0xFFF] >> 2;
				int32_t tiltCos = Numerics::g_sinCosLUT[(platformTransform.pos.y + 0x400) & 0xFFF] >> 2;
				Nu3D::Link::GetCurrentPosFixed(linkId, &platformTransform.pos);

				int32_t tiltSide =
					(((g_buzzActor.posAngles.pos.x - platformTransform.pos.x) >> 5) * tiltCos
						+ ((g_buzzActor.posAngles.pos.z - platformTransform.pos.z) >> 5) * tiltSin)
					/ 0x1000;
				int32_t verticalDistance = (g_buzzActor.posAngles.pos.y - platformTransform.pos.y) >> 5;
				int32_t distanceSquared = verticalDistance * verticalDistance + tiltSide * tiltSide;
				int32_t distance = (int32_t)sqrt((double)distanceSquared) >> 5;
				if (tiltSide > 0)
					angularVelocity += ((g_previousVerticalVelocity * distance >> 9) + distance) * Renderer::g_frameDelta / 2;
				else
					angularVelocity -= ((g_previousVerticalVelocity * distance >> 9) + distance) * Renderer::g_frameDelta / 2;
			}
			else if (motionMode == 1)
			{
				angularVelocity += Renderer::g_frameDelta * 4;
			}
			else if (motionMode == 2)
			{
				GetRotation(platformIndex, &platformTransform.pos);
				if (abs(platformTransform.pos.z) > 8)
				{
					if (platformTransform.pos.z < 0)
						angularVelocity += Renderer::g_frameDelta * 3;
					else
						angularVelocity -= Renderer::g_frameDelta * 3;
				}
			}

			if (angularVelocity > 0)
			{
				angularVelocity -= Renderer::g_frameDelta * 2;
				if (angularVelocity < 0)
					angularVelocity = 0;
			}
			else if (angularVelocity < 0)
			{
				angularVelocity += Renderer::g_frameDelta * 2;
				if (angularVelocity > 0)
					angularVelocity = 0;
			}

			GetRotation(platformIndex, &platformTransform.pos);
			int32_t previousRotation = platformTransform.pos.z;
			platformTransform.pos.z += angularVelocity / angularDivisor;
			if (platformTransform.pos.z < 0)
			{
				if (platformTransform.pos.z < minimumAngle * 4)
				{
					platformTransform.pos.z = minimumAngle * 4;
					angularVelocity = abs(angularVelocity * 3 / 4);
				}
			}
			else if (platformTransform.pos.z > maximumAngle * 4)
			{
				platformTransform.pos.z = maximumAngle * 4;
				angularVelocity = -abs(angularVelocity * 3 / 4);
			}

			SetAngularVelocity(platformIndex, 0, 0, (int16_t)platformTransform.pos.z - (int16_t)previousRotation);
			CommitRotationToLink(platformIndex, linkId);
			return angularVelocity;
		}

		const int32_t PLATFORM_FLAG_TRANSLATING = 0x80;
		const int32_t PLATFORM_FLAG_CARRIES_BUZZ = 0x100;

		// GLOBAL: TOY2 0x00728720
		PlatformState g_platformStates[32];

		// GLOBAL: TOY2 0x007290A0
		int16_t g_contactPlatformIndices[32];

		// GLOBAL: TOY2 0x007290E4
		int32_t g_contactPlatformCount;

		// FUNCTION: TOY2 0x00488580 [MATCHED]
		int32_t GetFlags(int32_t platformIndex) { return g_platformStates[platformIndex].flags; }

		// FUNCTION: TOY2 0x004885A0 [MATCHED]
		Vector3I16* GetContactFaceNormal(int32_t platformIndex) { return &g_platformStates[platformIndex].contactFace->normal; }

		// FUNCTION: TOY2 0x00488510 [MATCHED]
		void SetOrigin(int32_t platformIndex, int32_t x, int32_t y, int32_t z)
		{
			int16_t meshIndex = g_platformStates[platformIndex].collisionMeshIndex;
			Collision::g_collisionMeshInstances[meshIndex].boundsMin.x += x - Collision::g_collisionMeshInstances[meshIndex].origin.x;
			Collision::g_collisionMeshInstances[meshIndex].boundsMin.z += z - Collision::g_collisionMeshInstances[meshIndex].origin.z;
			Collision::g_collisionMeshInstances[meshIndex].origin.x = x;
			Collision::g_collisionMeshInstances[meshIndex].origin.y = y;
			Collision::g_collisionMeshInstances[meshIndex].origin.z = z;
		}

		// FUNCTION: TOY2 0x00488A60 [MATCHED]
		void AddFlags(int32_t platformIndex, uint16_t flags) { g_platformStates[platformIndex].flags |= flags; }

		// FUNCTION: TOY2 0x00488A80 [MATCHED]
		void ClearFlags(int32_t platformIndex, int32_t flags) { g_platformStates[platformIndex].flags &= ~flags; }

		// FUNCTION: TOY2 0x004878D0 [MATCHED]
		void DisableCollision(int32_t platformIndex)
		{
			int16_t meshIndex = g_platformStates[platformIndex].collisionMeshIndex;
			if (meshIndex != 0)
			{
				Collision::g_collisionMeshInstances[meshIndex].typeFlags = 0;
			}
		}

		// FUNCTION: TOY2 0x00487900 [MATCHED]
		void SetVelocity(int32_t platformIndex, int32_t x, int32_t y, int32_t z)
		{
			if (g_platformStates[platformIndex].collisionMeshIndex != 0)
			{
				if (x == 0 && y == 0 && z == 0)
				{
					g_platformStates[platformIndex].flags &= ~PLATFORM_FLAG_TRANSLATING;
					g_platformStates[platformIndex].velocity.x = 0;
					g_platformStates[platformIndex].velocity.y = 0;
					g_platformStates[platformIndex].velocity.z = 0;
					return;
				}
				g_platformStates[platformIndex].flags |= PLATFORM_FLAG_TRANSLATING;
				g_platformStates[platformIndex].velocity.x = x;
				g_platformStates[platformIndex].velocity.y = y;
				g_platformStates[platformIndex].velocity.z = z;
			}
		}

		// FUNCTION: TOY2 0x00487970 [MATCHED]
		void SetAngularVelocity(int32_t platformIndex, int16_t x, int16_t y, int16_t z)
		{
			if (g_platformStates[platformIndex].collisionMeshIndex != 0)
			{
				g_platformStates[platformIndex].flags |= 8;
				g_platformStates[platformIndex].angularVelocity.x = x;
				g_platformStates[platformIndex].angularVelocity.y = y;
				g_platformStates[platformIndex].angularVelocity.z = z;
			}
		}

		// FUNCTION: TOY2 0x0048E200 [MATCHED]
		void CopyVelocity(int32_t sourcePlatformIndex, int32_t destinationPlatformIndex)
		{
			if (g_platformStates[sourcePlatformIndex].collisionMeshIndex != 0 && g_platformStates[destinationPlatformIndex].collisionMeshIndex != 0)
			{
				g_platformStates[destinationPlatformIndex].flags |= PLATFORM_FLAG_TRANSLATING;
				g_platformStates[destinationPlatformIndex].velocity.x = g_platformStates[sourcePlatformIndex].velocity.x;
				g_platformStates[destinationPlatformIndex].velocity.y = g_platformStates[sourcePlatformIndex].velocity.y;
				g_platformStates[destinationPlatformIndex].velocity.z = g_platformStates[sourcePlatformIndex].velocity.z;
			}
		}

		// FUNCTION: TOY2 0x004879C0 [MATCHED]
		void GetOrigin(int32_t platformIndex, Vector3I* origin)
		{
			if (g_platformStates[platformIndex].collisionMeshIndex != 0)
			{
				origin->x = Collision::g_collisionMeshInstances[g_platformStates[platformIndex].collisionMeshIndex].origin.x;
				origin->y = Collision::g_collisionMeshInstances[g_platformStates[platformIndex].collisionMeshIndex].origin.y;
				origin->z = Collision::g_collisionMeshInstances[g_platformStates[platformIndex].collisionMeshIndex].origin.z;
			}
		}

		// FUNCTION: TOY2 0x00487A20 [MATCHED]
		void GetRotation(int32_t platformIndex, Vector3I* rotation)
		{
			if (g_platformStates[platformIndex].collisionMeshIndex != 0)
			{
				rotation->x = g_platformStates[platformIndex].rotationAnglesFixed.x;
				rotation->y = g_platformStates[platformIndex].rotationAnglesFixed.y;
				rotation->z = g_platformStates[platformIndex].rotationAnglesFixed.z;
			}
		}

		// FUNCTION: TOY2 0x00488AA0 [MATCHED]
		void SetRotationAngles(int32_t platformIndex, int16_t x, int16_t y, int16_t z)
		{
			g_platformStates[platformIndex].rotationAnglesFixed.x = x * 4;
			g_platformStates[platformIndex].rotationAnglesFixed.y = y * 4;
			g_platformStates[platformIndex].rotationAnglesFixed.z = z * 4;
			g_platformStates[platformIndex].flags |= 8;
		}

		// FUNCTION: TOY2 0x00488AF0 [MATCHED]
		void GetRotationAngles(int32_t platformIndex, Vector3I* angles)
		{
			angles->x = g_platformStates[platformIndex].rotationAnglesFixed.x >> 2;
			angles->y = g_platformStates[platformIndex].rotationAnglesFixed.y >> 2;
			angles->z = g_platformStates[platformIndex].rotationAnglesFixed.z >> 2;
		}

		// FUNCTION: TOY2 0x00488BD0 [MATCHED]
		void CommitRotationToLink(int32_t platformIndex, int32_t linkId)
		{
			Nu3D::Link::SetRotationAbsolute8bit(linkId,
				g_platformStates[platformIndex].rotationAnglesFixed.x >> 2,
				g_platformStates[platformIndex].rotationAnglesFixed.y >> 2,
				g_platformStates[platformIndex].rotationAnglesFixed.z >> 2);
		}

		// FUNCTION: TOY2 0x00488C10 [MATCHED]
		void AdvancePartialMotion(int32_t frameScale, int32_t collisionScale, int32_t skipMotion)
		{
			if (g_contactPlatformCount <= 0)
			{
				return;
			}

			int16_t* platformIndex = g_contactPlatformIndices;
			int32_t platformCount = g_contactPlatformCount;
			do
			{
				int32_t currentPlatformIndex = *platformIndex;
				if (skipMotion == 0)
				{
					if ((g_platformStates[currentPlatformIndex].flags & PLATFORM_FLAG_TRANSLATING) != 0)
					{
						int16_t translationX = g_platformStates[currentPlatformIndex].remainingTranslation.x;
						int32_t remainingScale = frameScale - collisionScale;
						int32_t movementX = translationX * frameScale / remainingScale;
						int16_t collisionMeshIndex = g_platformStates[currentPlatformIndex].collisionMeshIndex;
						int16_t translationY = g_platformStates[currentPlatformIndex].remainingTranslation.y;
						Collision::g_collisionMeshInstances[collisionMeshIndex].origin.x += movementX;
						int16_t translationZ = g_platformStates[currentPlatformIndex].remainingTranslation.z;
						Collision::g_collisionMeshInstances[collisionMeshIndex].origin.y += translationY * frameScale / remainingScale;
						int32_t movementZ = translationZ * frameScale / remainingScale;
						Collision::g_collisionMeshInstances[collisionMeshIndex].origin.z += movementZ;
						Collision::g_collisionMeshInstances[collisionMeshIndex].boundsMin.x += movementX;
						Collision::g_collisionMeshInstances[collisionMeshIndex].boundsMin.z += movementZ;
						g_platformStates[currentPlatformIndex].remainingTranslation.x = translationX - movementX;
						translationY = g_platformStates[currentPlatformIndex].remainingTranslation.y;
						g_platformStates[currentPlatformIndex].remainingTranslation.y = translationY - translationY * frameScale / remainingScale;
						translationZ = g_platformStates[currentPlatformIndex].remainingTranslation.z;
						g_platformStates[currentPlatformIndex].remainingTranslation.z = translationZ - translationZ * frameScale / remainingScale;
					}

					if ((g_platformStates[currentPlatformIndex].flags & PLATFORM_FLAG_ROTATED) != 0)
					{
						int32_t remainingScale = frameScale - collisionScale;
						g_platformStates[currentPlatformIndex].rotationAnglesFixed.x +=
							g_platformStates[currentPlatformIndex].remainingRotation.x * frameScale / remainingScale;
						g_platformStates[currentPlatformIndex].rotationAnglesFixed.y +=
							g_platformStates[currentPlatformIndex].remainingRotation.y * frameScale / remainingScale;
						g_platformStates[currentPlatformIndex].rotationAnglesFixed.z +=
							g_platformStates[currentPlatformIndex].remainingRotation.z * frameScale / remainingScale;

						int16_t rotationX = g_platformStates[currentPlatformIndex].remainingRotation.x;
						g_platformStates[currentPlatformIndex].remainingRotation.x = rotationX - rotationX * frameScale / remainingScale;
						int16_t rotationY = g_platformStates[currentPlatformIndex].remainingRotation.y;
						g_platformStates[currentPlatformIndex].remainingRotation.y = rotationY - rotationY * frameScale / remainingScale;
						int16_t rotationZ = g_platformStates[currentPlatformIndex].remainingRotation.z;
						g_platformStates[currentPlatformIndex].remainingRotation.z = rotationZ - rotationZ * frameScale / remainingScale;
					}
				}

				platformIndex++;
				platformCount--;
			} while (platformCount != 0);
		}

		// FUNCTION: TOY2 0x00488E10 [PROVISIONAL]
		void UpdateBuzzPlatformMotion(int32_t queryIndex, int32_t collisionPass)
		{
			if (collisionPass == 2)
			{
				return;
			}

			Collision::PlatformMotionCollisionResult* collisionResults =
				reinterpret_cast<Collision::PlatformMotionCollisionResult*>(Collision::g_collisionQueryResults);
			int32_t movedPlatformIndex = -1;
			bool carryFromAllPlatforms = g_ledgeClimbPlatformIndex >= 0 && g_levelFileIndex == 10;
			int32_t activePlatformIndex = -1;
			if (Collision::g_activePlatformIndex >= 0 && g_buzzActor.airborneMode == 0
				&& (g_platformStates[Collision::g_activePlatformIndex].flags & PLATFORM_FLAG_BUZZ_GROUNDED) != 0)
			{
				activePlatformIndex = Collision::g_activePlatformIndex;
			}

			for (int32_t workspaceIndex = 0; workspaceIndex < 8; workspaceIndex++)
			{
				if (Terrain::g_collisionWorkspace->slots[workspaceIndex].activeFrames > 0)
				{
					Terrain::g_collisionWorkspace->slots[workspaceIndex].activeFrames--;
				}
			}

			int32_t buzzZ = g_buzzActor.posAngles.pos.z;
			for (int32_t platformIndex = 0; platformIndex < 32; platformIndex++)
			{
				PlatformState& platform = g_platformStates[platformIndex];
				uint16_t flags = platform.flags;
				if ((flags & PLATFORM_FLAG_CARRIES_BUZZ) != 0
					&& (flags & (PLATFORM_FLAG_BUZZ_CONTACT | PLATFORM_FLAG_BUZZ_GROUNDED)) != 0 && g_buzzActor.airborneMode == 0)
				{
					activePlatformIndex = platformIndex;
				}

				platform.flags = flags & ~(1 | PLATFORM_FLAG_BUZZ_CONTACT);
				if ((flags & PLATFORM_FLAG_TRANSLATING) != 0)
				{
					platform.remainingTranslation = platform.velocity;
				}
				if ((platform.flags & PLATFORM_FLAG_ROTATED) != 0)
				{
					platform.remainingRotation = platform.angularVelocity;
				}

				if (platformIndex == g_ledgeClimbPlatformIndex || platformIndex == activePlatformIndex || carryFromAllPlatforms)
				{
					platform.remainingTranslation.x = 0;
					platform.remainingTranslation.y = 0;
					platform.remainingTranslation.z = 0;
					platform.remainingRotation.x = 0;
					platform.remainingRotation.y = 0;
					platform.remainingRotation.z = 0;

					if ((platform.flags & PLATFORM_FLAG_CARRIES_BUZZ) != 0)
					{
						Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[platform.collisionMeshIndex];
						int32_t movementX = platform.velocity.x;
						int32_t movementY = platform.velocity.y;
						int32_t movementZ = platform.velocity.z;
						mesh.origin.x += movementX;
						mesh.origin.y += movementY;
						mesh.origin.z += movementZ;
						mesh.boundsMin.x += movementX;
						mesh.boundsMin.z += movementZ;

						if (platformIndex == g_ledgeClimbPlatformIndex || g_ledgeClimbPlatformIndex == -1)
						{
							buzzZ += movementZ;
							movedPlatformIndex = platformIndex;
							g_buzzActor.posAngles.pos.x += movementX;
							g_buzzActor.posAngles.pos.y += movementY;
							g_buzzActor.posAngles.pos.z = buzzZ;

							if ((platform.flags & PLATFORM_FLAG_ROTATED) != 0)
							{
								Vector3I16 angles;
								angles.x = platform.rotationAnglesFixed.x >> 2;
								angles.y = platform.rotationAnglesFixed.y >> 2;
								angles.z = platform.rotationAnglesFixed.z >> 2;
								Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
								angles.x = (platform.rotationAnglesFixed.x + platform.angularVelocity.x) >> 2;
								angles.y = (platform.rotationAnglesFixed.y + platform.angularVelocity.y) >> 2;
								angles.z = (platform.rotationAnglesFixed.z + platform.angularVelocity.z) >> 2;
								Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_nextKeyframeRotation.matrix);

								int32_t relativeX = g_buzzActor.posAngles.pos.x - mesh.origin.x;
								int32_t relativeY = g_buzzActor.posAngles.pos.y - mesh.origin.y;
								int32_t relativeZ = g_buzzActor.posAngles.pos.z - mesh.origin.z;
								int32_t localRawX = Animation::g_keyframeRotation.matrix.m00 * relativeX
									+ Animation::g_keyframeRotation.matrix.m10 * relativeY
									+ Animation::g_keyframeRotation.matrix.m20 * relativeZ;
								int32_t localRawY = Animation::g_keyframeRotation.matrix.m01 * relativeX
									+ Animation::g_keyframeRotation.matrix.m11 * relativeY
									+ Animation::g_keyframeRotation.matrix.m21 * relativeZ;
								int32_t localRawZ = Animation::g_keyframeRotation.matrix.m02 * relativeX
									+ Animation::g_keyframeRotation.matrix.m12 * relativeY
									+ Animation::g_keyframeRotation.matrix.m22 * relativeZ;
								int32_t localX = localRawX / 4096;
								int32_t localY = localRawY / 4096;
								int32_t localZ = localRawZ / 4096;
								int32_t fractionX = localRawX & 0xFFF;
								int32_t fractionY = localRawY & 0xFFF;
								int32_t fractionZ = localRawZ & 0xFFF;

								int32_t rotatedX = Animation::g_nextKeyframeRotation.matrix.m00 * localX
									+ Animation::g_nextKeyframeRotation.matrix.m01 * localY
									+ Animation::g_nextKeyframeRotation.matrix.m02 * localZ
									+ Animation::g_nextKeyframeRotation.matrix.m00 * fractionX / 4096
									+ Animation::g_nextKeyframeRotation.matrix.m01 * fractionY / 4096
									+ Animation::g_nextKeyframeRotation.matrix.m02 * fractionZ / 4096;
								int32_t rotatedY = Animation::g_nextKeyframeRotation.matrix.m10 * localX
									+ Animation::g_nextKeyframeRotation.matrix.m11 * localY
									+ Animation::g_nextKeyframeRotation.matrix.m12 * localZ
									+ Animation::g_nextKeyframeRotation.matrix.m10 * fractionX / 4096
									+ Animation::g_nextKeyframeRotation.matrix.m11 * fractionY / 4096
									+ Animation::g_nextKeyframeRotation.matrix.m12 * fractionZ / 4096;
								int32_t rotatedZ = Animation::g_nextKeyframeRotation.matrix.m20 * localX
									+ Animation::g_nextKeyframeRotation.matrix.m21 * localY
									+ Animation::g_nextKeyframeRotation.matrix.m22 * localZ
									+ Animation::g_nextKeyframeRotation.matrix.m20 * fractionX / 4096
									+ Animation::g_nextKeyframeRotation.matrix.m21 * fractionY / 4096
									+ Animation::g_nextKeyframeRotation.matrix.m22 * fractionZ / 4096;
								g_buzzActor.posAngles.pos.x = rotatedX / 4096 + mesh.origin.x;
								g_buzzActor.posAngles.pos.y = rotatedY / 4096 + mesh.origin.y;
								buzzZ = rotatedZ / 4096 + mesh.origin.z;
								platform.rotationAnglesFixed.x += platform.angularVelocity.x;
								platform.rotationAnglesFixed.y += platform.angularVelocity.y;
								platform.rotationAnglesFixed.z += platform.angularVelocity.z;
								g_buzzActor.posAngles.pos.z = buzzZ;
							}
						}

						if (g_buzzActor.velocity.lateral != 0 || g_buzzActor.velocity.forward != 0)
						{
							collisionResults[0].platformMovement.y += 800;
						}
					}
				}
				else if ((platform.flags & (PLATFORM_FLAG_BUZZ_CONTACT | PLATFORM_FLAG_BUZZ_GROUNDED)) != 0)
				{
					Collision::PlatformMotionCollisionResult& result = collisionResults[0];
					result.platformMovement.x += platform.velocity.x;
					result.platformMovement.y += platform.velocity.y;
					result.platformMovement.z += platform.velocity.z;
					if (platform.angularVelocity.x != 0 || platform.angularVelocity.y != 0 || platform.angularVelocity.z != 0)
					{
						result.platformRotationState = 0;
					}

					Vector3I16 angles;
					angles.x = platform.rotationAnglesFixed.x >> 2;
					angles.y = platform.rotationAnglesFixed.y >> 2;
					angles.z = platform.rotationAnglesFixed.z >> 2;
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
					angles.x = (platform.rotationAnglesFixed.x + platform.remainingRotation.x) >> 2;
					angles.y = (platform.rotationAnglesFixed.y + platform.remainingRotation.y) >> 2;
					angles.z = (platform.rotationAnglesFixed.z + platform.remainingRotation.z) >> 2;
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_nextKeyframeRotation.matrix);

					Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[platform.collisionMeshIndex];
					int32_t relativeX = g_buzzActor.posAngles.pos.x - mesh.origin.x;
					int32_t relativeY = g_buzzActor.posAngles.pos.y - mesh.origin.y;
					int32_t relativeZ = g_buzzActor.posAngles.pos.z - mesh.origin.z;
					int32_t localX = (Animation::g_keyframeRotation.matrix.m00 * relativeX
						+ Animation::g_keyframeRotation.matrix.m10 * relativeY
						+ Animation::g_keyframeRotation.matrix.m20 * relativeZ)
						/ 4096;
					int32_t localY = (Animation::g_keyframeRotation.matrix.m01 * relativeX
						+ Animation::g_keyframeRotation.matrix.m11 * relativeY
						+ Animation::g_keyframeRotation.matrix.m21 * relativeZ)
						/ 4096;
					int32_t localZ = (Animation::g_keyframeRotation.matrix.m02 * relativeX
						+ Animation::g_keyframeRotation.matrix.m12 * relativeY
						+ Animation::g_keyframeRotation.matrix.m22 * relativeZ)
						/ 4096;
					int32_t rotatedX = (Animation::g_nextKeyframeRotation.matrix.m00 * localX
						+ Animation::g_nextKeyframeRotation.matrix.m01 * localY
						+ Animation::g_nextKeyframeRotation.matrix.m02 * localZ)
						/ 4096;
					int32_t rotatedY = (Animation::g_nextKeyframeRotation.matrix.m10 * localX
						+ Animation::g_nextKeyframeRotation.matrix.m11 * localY
						+ Animation::g_nextKeyframeRotation.matrix.m12 * localZ)
						/ 4096;
					int32_t rotatedZ = (Animation::g_nextKeyframeRotation.matrix.m20 * localX
						+ Animation::g_nextKeyframeRotation.matrix.m21 * localY
						+ Animation::g_nextKeyframeRotation.matrix.m22 * localZ)
						/ 4096;
					result.platformMovement.x += (int16_t)rotatedX - (int16_t)g_buzzActor.posAngles.pos.x + (int16_t)mesh.origin.x;
					result.platformMovement.y += (int16_t)rotatedY - (int16_t)g_buzzActor.posAngles.pos.y + (int16_t)mesh.origin.y;
					result.platformMovement.z += (int16_t)rotatedZ - (int16_t)g_buzzActor.posAngles.pos.z + (int16_t)mesh.origin.z;
					buzzZ = g_buzzActor.posAngles.pos.z;
				}

				platform.flags &= ~PLATFORM_FLAG_BUZZ_GROUNDED;
			}

			collisionResults[queryIndex].movement.x += collisionResults[queryIndex].platformMovement.x;
			collisionResults[queryIndex].movement.y += collisionResults[queryIndex].platformMovement.y;
			collisionResults[queryIndex].movement.z += collisionResults[queryIndex].platformMovement.z;
			Collision::g_activePlatformIndex = activePlatformIndex;

			if (Collision::g_previousPlatformIndex != -1 && movedPlatformIndex == -1 && g_buzzActor.airborneMode == 0)
			{
				PlatformState& platform = g_platformStates[Collision::g_previousPlatformIndex];
				int32_t movementY = platform.velocity.y / 2;
				g_buzzActor.posAngles.pos.y += movementY;
				int32_t movementZ = platform.velocity.z / 2;
				g_buzzActor.posAngles.pos.z = buzzZ + movementZ;
				g_buzzActor.velocity.forward += movementZ;
				int32_t movementX = platform.velocity.x / 2;
				g_buzzActor.posAngles.pos.x += movementX;
				g_buzzActor.velocity.lateral += movementX;
				g_buzzActor.velocity.vertical += movementY;

				if ((platform.flags & PLATFORM_FLAG_ROTATED) != 0)
				{
					Vector3I16 angles;
					angles.x = platform.rotationAnglesFixed.x >> 2;
					angles.y = platform.rotationAnglesFixed.y >> 2;
					angles.z = platform.rotationAnglesFixed.z >> 2;
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
					angles.x = (platform.rotationAnglesFixed.x + platform.angularVelocity.x) >> 2;
					angles.y = (platform.rotationAnglesFixed.y + platform.angularVelocity.y) >> 2;
					angles.z = (platform.rotationAnglesFixed.z + platform.angularVelocity.z) >> 2;
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_nextKeyframeRotation.matrix);

					Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[platform.collisionMeshIndex];
					int32_t relativeX = g_buzzActor.posAngles.pos.x - mesh.origin.x;
					int32_t relativeY = g_buzzActor.posAngles.pos.y - mesh.origin.y;
					int32_t relativeZ = g_buzzActor.posAngles.pos.z - mesh.origin.z;
					int32_t localX = (Animation::g_keyframeRotation.matrix.m00 * relativeX
						+ Animation::g_keyframeRotation.matrix.m10 * relativeY
						+ Animation::g_keyframeRotation.matrix.m20 * relativeZ)
						/ 4096;
					int32_t localY = (Animation::g_keyframeRotation.matrix.m01 * relativeX
						+ Animation::g_keyframeRotation.matrix.m11 * relativeY
						+ Animation::g_keyframeRotation.matrix.m21 * relativeZ)
						/ 4096;
					int32_t localZ = (Animation::g_keyframeRotation.matrix.m02 * relativeX
						+ Animation::g_keyframeRotation.matrix.m12 * relativeY
						+ Animation::g_keyframeRotation.matrix.m22 * relativeZ)
						/ 4096;
					int32_t targetX = (Animation::g_nextKeyframeRotation.matrix.m00 * localX
						+ Animation::g_nextKeyframeRotation.matrix.m01 * localY
						+ Animation::g_nextKeyframeRotation.matrix.m02 * localZ)
						/ 4096
						+ mesh.origin.x;
					int32_t targetY = (Animation::g_nextKeyframeRotation.matrix.m10 * localX
						+ Animation::g_nextKeyframeRotation.matrix.m11 * localY
						+ Animation::g_nextKeyframeRotation.matrix.m12 * localZ)
						/ 4096
						+ mesh.origin.y;
					int32_t targetZ = (Animation::g_nextKeyframeRotation.matrix.m20 * localX
						+ Animation::g_nextKeyframeRotation.matrix.m21 * localY
						+ Animation::g_nextKeyframeRotation.matrix.m22 * localZ)
						/ 4096
						+ mesh.origin.z;
					g_buzzActor.posAngles.pos.x += (targetX - g_buzzActor.posAngles.pos.x) / 2;
					g_buzzActor.posAngles.pos.y += (targetY - g_buzzActor.posAngles.pos.y) / 2;
					g_buzzActor.posAngles.pos.z += (targetZ - g_buzzActor.posAngles.pos.z) / 2;
					g_buzzActor.velocity.lateral += (targetX - g_buzzActor.posAngles.pos.x) / 2;
					g_buzzActor.velocity.vertical += (targetY - g_buzzActor.posAngles.pos.y) / 2;
					g_buzzActor.velocity.forward += (targetZ - g_buzzActor.posAngles.pos.z) / 2;
				}
			}

			Collision::g_previousPlatformIndex = movedPlatformIndex;
		}

		// FUNCTION: TOY2 0x0048ACC0 [PROVISIONAL]
		void StepMotionScript(int32_t platformIndex,
			int32_t linkId,
			int16_t** scriptPosition,
			int32_t* waitTimer,
			int32_t* speedScale)
		{
			int16_t* command = *scriptPosition;
			Vector3I targetPosition;
			Vector3I platformPosition;

			switch (command[0])
			{
			case 0:
				*scriptPosition -= command[1];
				*waitTimer = 0;
				return;

			case 1:
			{
				Nu3D::Link::GetTargetPosFixed(linkId, &targetPosition);
				int16_t collisionMeshIndex = g_platformStates[platformIndex].collisionMeshIndex;
				if (collisionMeshIndex != 0)
				{
					platformPosition.x = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.x;
					platformPosition.y = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.y;
					platformPosition.z = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.z;
				}
				targetPosition.x = (targetPosition.x >> 5) + command[1];
				targetPosition.y = (targetPosition.y >> 5) + command[2];
				targetPosition.z = (targetPosition.z >> 5) + command[3];
				platformPosition.x >>= 5;
				platformPosition.y >>= 5;
				platformPosition.z >>= 5;
				Nu3D::Link::SetPositionRawAndCommit(
					linkId, platformPosition.x, platformPosition.y, platformPosition.z);

				int32_t distanceX = abs(platformPosition.x - targetPosition.x);
				int32_t distanceY = abs(platformPosition.y - targetPosition.y);
				int32_t distanceZ = abs(platformPosition.z - targetPosition.z);
				int32_t speed = command[4];
				int32_t slowDistance = speed * 4 + 0x30;
				if (distanceX < slowDistance && distanceY < slowDistance && distanceZ < slowDistance && *speedScale > 0)
					*speedScale = -0x40;

				if (*speedScale < 0)
				{
					*speedScale += Renderer::g_frameDelta * 2;
					if (*speedScale > 0)
						*speedScale = 0;
				}
				else if (*speedScale < 0x40)
				{
					*speedScale += Renderer::g_frameDelta * 2;
				}

				if (*speedScale != 0)
				{
					if (distanceX >= speed + 8 || distanceY >= speed + 8 || distanceZ >= speed + 8)
					{
						if (*speedScale < 0)
							speed = -(*speedScale * speed / 0x40);
						else if (*speedScale < 0x40)
							speed = *speedScale * speed / 0x40;
						if (speed < 1)
							speed = 1;

						targetPosition.x -= platformPosition.x;
						targetPosition.y -= platformPosition.y;
						targetPosition.z -= platformPosition.z;
						Nu3D::Math::NormalizeToFixedPoint(&targetPosition, &targetPosition);
						int32_t frameSpeed = Renderer::g_frameDelta * speed;
						int32_t movementX = frameSpeed * targetPosition.x;
						int32_t movementY = frameSpeed * targetPosition.y;
						int32_t movementZ = frameSpeed * targetPosition.z;
						if (g_platformStates[platformIndex].collisionMeshIndex != 0)
						{
							if ((movementX >> 7) == 0 && (movementY >> 7) == 0 && (movementZ >> 7) == 0)
							{
								g_platformStates[platformIndex].flags &= ~PLATFORM_FLAG_TRANSLATING;
								g_platformStates[platformIndex].velocity.x = 0;
								g_platformStates[platformIndex].velocity.y = 0;
								g_platformStates[platformIndex].velocity.z = 0;
								return;
							}
							g_platformStates[platformIndex].flags |= PLATFORM_FLAG_TRANSLATING;
							g_platformStates[platformIndex].velocity.x = movementX >> 7;
							g_platformStates[platformIndex].velocity.y = movementY >> 7;
							g_platformStates[platformIndex].velocity.z = movementZ >> 7;
						}
						return;
					}
				}

				*speedScale = 0;
				*waitTimer = 0;
				*scriptPosition += 5;
				if (g_platformStates[platformIndex].collisionMeshIndex != 0)
				{
					g_platformStates[platformIndex].flags &= ~PLATFORM_FLAG_TRANSLATING;
					g_platformStates[platformIndex].velocity.x = 0;
					g_platformStates[platformIndex].velocity.y = 0;
					g_platformStates[platformIndex].velocity.z = 0;
				}
				return;
			}

			case 2:
				if (*waitTimer == 0)
				{
					*waitTimer = command[1];
					return;
				}
				*waitTimer -= Renderer::g_frameDelta;
				if (*waitTimer < 1)
				{
					*waitTimer = 0;
					*scriptPosition += 2;
				}
				return;

			case 3:
				if (*waitTimer == 0)
				{
					*waitTimer = (command[1] & *g_randDatBufferPtr) + command[2];
					g_randDatBufferPtr++;
					return;
				}
				*waitTimer -= Renderer::g_frameDelta;
				if (*waitTimer < 1)
				{
					*waitTimer = 0;
					*scriptPosition += 3;
				}
				return;

			case 4:
			{
				int16_t collisionMeshIndex = g_platformStates[platformIndex].collisionMeshIndex;
				if (collisionMeshIndex != 0)
				{
					platformPosition.x = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.x;
					platformPosition.y = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.y;
					platformPosition.z = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.z;
				}
				platformPosition.x >>= 5;
				platformPosition.y >>= 5;
				platformPosition.z >>= 5;
				targetPosition = Levels::g_recordData[command[1]]->data[command[2]];
				Nu3D::Link::SetPositionRawAndCommit(
					linkId, platformPosition.x, platformPosition.y, platformPosition.z);

				int32_t distanceX = abs(platformPosition.x - targetPosition.x);
				int32_t distanceY = abs(platformPosition.y - targetPosition.y);
				int32_t distanceZ = abs(platformPosition.z - targetPosition.z);
				int32_t speed = command[3];
				int32_t slowDistance = speed * 4 + 0x30;
				if (distanceX < slowDistance && distanceY < slowDistance && distanceZ < slowDistance && *speedScale > 0)
					*speedScale = -0x40;

				if (*speedScale < 0)
				{
					*speedScale += Renderer::g_frameDelta * 2;
					if (*speedScale > 0)
						*speedScale = 0;
				}
				else if (*speedScale < 0x40)
				{
					*speedScale += Renderer::g_frameDelta * 2;
				}

				if (*speedScale != 0)
				{
					if (distanceX >= speed + 8 || distanceY >= speed + 8 || distanceZ >= speed + 8)
					{
						if (*speedScale < 0)
							speed = -(*speedScale * speed / 0x40);
						else if (*speedScale < 0x40)
							speed = *speedScale * speed / 0x40;
						if (speed < 1)
							speed = 1;

						targetPosition.x -= platformPosition.x;
						targetPosition.y -= platformPosition.y;
						targetPosition.z -= platformPosition.z;
						Nu3D::Math::NormalizeToFixedPoint(&targetPosition, &targetPosition);
						int32_t frameSpeed = Renderer::g_frameDelta * speed;
						int32_t movementX = frameSpeed * targetPosition.x;
						int32_t movementY = frameSpeed * targetPosition.y;
						int32_t movementZ = frameSpeed * targetPosition.z;
						if (g_platformStates[platformIndex].collisionMeshIndex != 0)
						{
							if ((movementX >> 7) == 0 && (movementY >> 7) == 0 && (movementZ >> 7) == 0)
							{
								g_platformStates[platformIndex].flags &= ~PLATFORM_FLAG_TRANSLATING;
								g_platformStates[platformIndex].velocity.x = 0;
								g_platformStates[platformIndex].velocity.y = 0;
								g_platformStates[platformIndex].velocity.z = 0;
								return;
							}
							g_platformStates[platformIndex].flags |= PLATFORM_FLAG_TRANSLATING;
							g_platformStates[platformIndex].velocity.x = movementX >> 7;
							g_platformStates[platformIndex].velocity.y = movementY >> 7;
							g_platformStates[platformIndex].velocity.z = movementZ >> 7;
						}
						return;
					}
				}

				*speedScale = 0;
				*waitTimer = 0;
				*scriptPosition += 4;
				if (g_platformStates[platformIndex].collisionMeshIndex != 0)
				{
					g_platformStates[platformIndex].flags &= ~PLATFORM_FLAG_TRANSLATING;
					g_platformStates[platformIndex].velocity.x = 0;
					g_platformStates[platformIndex].velocity.y = 0;
					g_platformStates[platformIndex].velocity.z = 0;
				}
				return;
			}

			case 6:
				if ((command[1] & Collision::g_motionScriptEntryCount) != 0)
					*scriptPosition += 2;
				return;

			case 7:
				if ((command[1] & Collision::g_motionScriptEntryCount) == 0)
					*scriptPosition += 2;
				return;

			case 8:
				Collision::g_motionScriptEntryCount |= command[1];
				*scriptPosition += 2;
				return;

			case 9:
				Collision::g_motionScriptEntryCount &= ~command[1];
				*scriptPosition += 2;
				return;

			case 10:
			{
				int16_t collisionMeshIndex = g_platformStates[platformIndex].collisionMeshIndex;
				if (collisionMeshIndex != 0)
				{
					platformPosition.x = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.x;
					platformPosition.y = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.y;
					platformPosition.z = Collision::g_collisionMeshInstances[collisionMeshIndex].origin.z;
				}
				platformPosition.x >>= 5;
				platformPosition.y >>= 5;
				platformPosition.z >>= 5;
				targetPosition = Levels::g_recordData[command[1]]->data[command[2]];
				Nu3D::Link::SetPositionRawAndCommit(
					linkId, platformPosition.x, platformPosition.y, platformPosition.z);

				int32_t speed = command[3];
				if (abs(platformPosition.x - targetPosition.x) < speed + 8
					&& abs(platformPosition.y - targetPosition.y) < speed + 8
					&& abs(platformPosition.z - targetPosition.z) < speed + 8)
				{
					*waitTimer = 0;
					*scriptPosition += 4;
					if (g_platformStates[platformIndex].collisionMeshIndex != 0)
					{
						g_platformStates[platformIndex].flags &= ~PLATFORM_FLAG_TRANSLATING;
						g_platformStates[platformIndex].velocity.x = 0;
						g_platformStates[platformIndex].velocity.y = 0;
						g_platformStates[platformIndex].velocity.z = 0;
					}
					return;
				}

				targetPosition.x -= platformPosition.x;
				targetPosition.y -= platformPosition.y;
				targetPosition.z -= platformPosition.z;
				Nu3D::Math::NormalizeToFixedPoint(&targetPosition, &targetPosition);
				int32_t movementX = speed * Renderer::g_frameDelta * targetPosition.x >> 7;
				int32_t movementY = speed * Renderer::g_frameDelta * targetPosition.y >> 7;
				int32_t movementZ = speed * Renderer::g_frameDelta * targetPosition.z >> 7;
				if (g_platformStates[platformIndex].collisionMeshIndex != 0)
				{
					if (movementX == 0 && movementY == 0 && movementZ == 0)
					{
						g_platformStates[platformIndex].flags &= ~PLATFORM_FLAG_TRANSLATING;
						g_platformStates[platformIndex].velocity.x = 0;
						g_platformStates[platformIndex].velocity.y = 0;
						g_platformStates[platformIndex].velocity.z = 0;
						return;
					}
					g_platformStates[platformIndex].flags |= PLATFORM_FLAG_TRANSLATING;
					g_platformStates[platformIndex].velocity.x = movementX;
					g_platformStates[platformIndex].velocity.y = movementY;
					g_platformStates[platformIndex].velocity.z = movementZ;
				}
				return;
			}
			}
		}

		// FUNCTION: TOY2 0x0048B640 [MATCHED]
		int32_t HadBuzzContactThisFrame(int32_t platformIndex)
		{
			if ((g_platformStates[platformIndex].flags & 2) != 0 || (g_platformStates[platformIndex].flags & 0x10) != 0)
			{
				return 1;
			}
			return 0;
		}
	}
}

namespace Toy2
{
	namespace Level
	{
		// FUNCTION: TOY2 0x004885C0 [PROVISIONAL]
		int32_t GetSectorAtPosition(const Vector3I* position)
		{
			if (Levels::g_hasZoneData == 0)
				return 0;

			Collision::g_collisionTriangleCount = 0;
			int32_t queryX = Collision::ShiftTowardZero(position->x, 2);
			int32_t queryY = Collision::ShiftTowardZero(position->y, 2);
			int32_t queryZ = Collision::ShiftTowardZero(position->z, 2);
			int32_t nearestSurfaceY = 0x7FFFFFFF;

			Collision::CollisionGridCell& movingCell = Collision::g_collisionGrid[255];
			int16_t* meshList = &Collision::g_collisionGridMeshIndices[movingCell.meshListStart];
			for (int32_t meshListIndex = 0; meshListIndex < movingCell.meshCount; meshListIndex++, meshList++)
			{
				int32_t meshIndex = *meshList;
				Collision::CollisionMeshRecord& mesh = reinterpret_cast<Collision::CollisionMeshRecord*>(Collision::g_collisionMeshInstances)[meshIndex];
				int32_t maximumX = queryX + 0x1000;
				int32_t maximumZ = queryZ + 0x1000;
				if (maximumX - mesh.boundsMin.x >= mesh.boundsExt.x + 0x2000
					|| maximumZ - mesh.boundsMin.z >= mesh.boundsExt.z + 0x2000 || mesh.typeFlags == 0)
				{
					continue;
				}

				int32_t localMaximumX = Collision::ShiftTowardZero(maximumX - mesh.origin.x, 5);
				int32_t localMaximumZ = Collision::ShiftTowardZero(maximumZ - mesh.origin.z, 5);
				int32_t localX = (queryX - mesh.origin.x) >> 5;
				int32_t localZ = (queryZ - mesh.origin.z) >> 5;
				Collision::CollisionTreeGroup* group = mesh.collisionTree;

				while (group->marker >= 0)
				{
					int32_t faceCount = group->faceCount;
					Collision::PackedCollisionFace* face = reinterpret_cast<Collision::PackedCollisionFace*>(group + 1);
					if ((uint32_t)(localMaximumX - group->boundsMinX) >= (uint32_t)(group->boundsExtentX + 0x100)
						|| (uint32_t)(localMaximumZ - group->boundsMinZ) >= (uint32_t)(group->boundsExtentZ + 0x100))
					{
						face += faceCount;
						group = reinterpret_cast<Collision::CollisionTreeGroup*>(face);
						continue;
					}

					for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
					{
						if ((uint32_t)(localMaximumX - face->boundsMinX) >= (uint32_t)(face->boundsExtentX + 0x100)
							|| (uint32_t)(localMaximumZ - face->boundsMinZBlock * 0x40 - face->vertex0.z)
								>= (uint32_t)((face->boundsExtentZBlock + 4) * 0x40))
						{
							continue;
						}

						int32_t vertex0X = face->vertex0.x;
						int32_t vertex0Z = face->vertex0.z;
						int32_t vertex1X = vertex0X + face->vertex1Offset.x;
						int32_t vertex1Z = vertex0Z + face->vertex1Offset.z;
						int32_t vertex2X = vertex0X + face->vertex2Offset.x;
						int32_t vertex2Z = vertex0Z + face->vertex2Offset.z;

						if (-face->vertex1Offset.x * (localZ - vertex0Z) + face->vertex1Offset.z * (localX - vertex0X) < 0
							|| (localZ - vertex2Z) * face->vertex2Offset.x + (vertex0Z - vertex2Z) * (localX - vertex2X) < 0)
						{
							continue;
						}

						bool useFirstPlane = false;
						if (face->secondPlaneNormal.y == 0x7FFF)
						{
							useFirstPlane = (face->vertex1Offset.x - face->vertex2Offset.x) * (localZ - vertex1Z)
								+ (vertex2Z - vertex1Z) * (localX - vertex1X) >= 0;
						}
						else
						{
							int32_t vertex3X = vertex0X + face->vertex3Offset.x;
							int32_t vertex3Z = vertex0Z + face->vertex3Offset.z;
							if ((face->vertex1Offset.x - face->vertex3Offset.x) * (localZ - vertex1Z)
								+ (vertex3Z - vertex1Z) * (localX - vertex1X) < 0
								|| (face->vertex3Offset.x - face->vertex2Offset.x) * (localZ - vertex3Z)
									+ (vertex2Z - vertex3Z) * (localX - vertex3X) < 0)
							{
								continue;
							}

							useFirstPlane = (face->vertex1Offset.x - face->vertex2Offset.x) * (localZ - vertex1Z)
								+ (vertex2Z - vertex1Z) * (localX - vertex1X) >= 0 || face->secondPlaneNormal.y == 0;
						}

						int32_t surfaceY;
						if (useFirstPlane)
						{
							int32_t heightOffset = ((localX - vertex0X) * face->firstPlaneNormal.z
								+ (localZ - vertex0Z) * face->firstPlaneNormal.x) * 8 / face->firstPlaneNormal.y;
							surfaceY = mesh.origin.y + (face->vertex0.y * 8 - heightOffset) * 4;
						}
						else
						{
							int32_t heightOffset = ((localX - vertex0X - face->vertex3Offset.x) * face->secondPlaneNormal.z
								+ (localZ - vertex0Z - face->vertex3Offset.z) * face->secondPlaneNormal.x) * 8 / face->secondPlaneNormal.y;
							surfaceY = mesh.origin.y + ((face->vertex0.y + face->vertex3Offset.y) * 8 - heightOffset) * 4;
						}

						if (queryY <= surfaceY && surfaceY < nearestSurfaceY)
						{
							Collision::g_groundCollisionMeshIndex = *meshList;
							nearestSurfaceY = surfaceY;
						}
					}
					group = reinterpret_cast<Collision::CollisionTreeGroup*>(face);
				}
			}

			if (nearestSurfaceY == 0x7FFFFFFF)
				return -1;
			return (uint8_t)reinterpret_cast<Collision::CollisionMeshRecord*>(Collision::g_collisionMeshInstances)
				[Collision::g_groundCollisionMeshIndex].flags;
		}
	}
}

namespace Nu3D
{
	namespace Collision
	{
		// The sweep keeps its hit normal as an 8-byte record: the retail copies a plane
		// normal as two dwords, taking the word after the 6-byte Vector3I16. Declared here
		// rather than in Numerics.h because the shared declaration shifts VC6 register
		// allocation in unrelated level functions.
		struct Vector4I16
		{
			int16_t x;
			int16_t y;
			int16_t z;
			int16_t w;
		};

		int32_t SweepAgainstEdges(int32_t* nearestFraction,
			int32_t* startDistance,
			int32_t* endDistance,
			Vector3I16* hitNormal,
			const Vector4I* start,
			const Vector4I* movement,
			int32_t radius);

		// FUNCTION: TOY2 0x0048B750 [PROVISIONAL]
		int32_t SweepAgainstCandidates(const Vector4I* movement, Vector4I* position, int32_t radius)
		{
			int16_t hitCount = 0;
			int32_t nearestFraction = 0x7FFFFFFF;
			int32_t startDistance;
			int32_t endDistance;
			union
			{
				Vector3I16 xyz;
				Vector4I16 xyzw;
			} hitNormal;
			int16_t preparedMeshIndex = -1;
			Vector3I localStart;
			Vector3I localEnd;

			for (int32_t triangleIndex = Toy2::Collision::g_collisionTriangleCount - 1; triangleIndex >= 0; triangleIndex--)
			{
				int16_t meshIndex = Toy2::Collision::g_collisionTriangleMeshIndices[triangleIndex];
				Toy2::Collision::PackedCollisionFace* face = Toy2::Collision::g_collisionTriangles[triangleIndex];
				if (meshIndex != preparedMeshIndex)
				{
					Vector4I start = *position;
					Vector4I delta = *movement;
					preparedMeshIndex = meshIndex;
					if (Toy2::Collision::g_collisionMeshInstances[meshIndex].typeFlags == Toy2::Collision::COLLISION_MESH_MOVING
						&& (Toy2::Platform::g_platformStates[Toy2::Collision::g_collisionMeshInstances[meshIndex].platformIdx].flags
							   & Toy2::Platform::PLATFORM_FLAG_ROTATED)
							!= 0)
					{
						Toy2::Platform::PlatformState& platform =
							Toy2::Platform::g_platformStates[Toy2::Collision::g_collisionMeshInstances[meshIndex].platformIdx];
						Vector3I16 angles;
						angles.x = platform.rotationAnglesFixed.x >> 2;
						angles.y = platform.rotationAnglesFixed.y >> 2;
						angles.z = platform.rotationAnglesFixed.z >> 2;
						const Matrix3x3I16& rotation = Toy2::Animation::g_keyframeRotation.matrix;
						Math::SetRotationXYZ(&angles, &Toy2::Animation::g_keyframeRotation.matrix);
						int32_t relativeX = start.x - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.x;
						int32_t relativeY = start.y - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.y;
						int32_t relativeZ = start.z - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.z;
						localStart.x = (rotation.m00 * relativeX + rotation.m10 * relativeY + rotation.m20 * relativeZ) / 0x1000;
						localStart.y = (rotation.m01 * relativeX + rotation.m11 * relativeY + rotation.m21 * relativeZ) / 0x1000;
						localStart.z = (rotation.m02 * relativeX + rotation.m12 * relativeY + rotation.m22 * relativeZ) / 0x1000;
						relativeX = delta.x - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.x + start.x;
						relativeY = delta.y - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.y + start.y;
						relativeZ = delta.z - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.z + start.z;
						localEnd.x = (rotation.m00 * relativeX + rotation.m10 * relativeY + rotation.m20 * relativeZ) / 0x1000;
						localEnd.y = (rotation.m01 * relativeX + rotation.m11 * relativeY + rotation.m21 * relativeZ) / 0x1000;
						localEnd.z = (rotation.m02 * relativeX + rotation.m12 * relativeY + rotation.m22 * relativeZ) / 0x1000;
					}
					else
					{
						localStart.x = start.x - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.x;
						localStart.y = start.y - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.y;
						localStart.z = start.z - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.z;
						localEnd.x = delta.x - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.x + start.x;
						localEnd.y = delta.y - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.y + start.y;
						localEnd.z = delta.z - Toy2::Collision::g_collisionMeshInstances[meshIndex].origin.z + start.z;
					}
				}

				int32_t endX = localEnd.x >> 5;
				int32_t endY = localEnd.y >> 5;
				int32_t endZ = localEnd.z >> 5;
				const Vector3I16& normal = face->firstPlaneNormal;
				int32_t endDistanceFromPlane =
					((endX - face->vertex0.x) * normal.x + (endY - face->vertex0.y) * normal.y + (endZ - face->vertex0.z) * normal.z >> 14) - radius / 32;
				if (endDistanceFromPlane < 0)
				{
					int32_t startDistanceFromPlane =
						(((localStart.x >> 5) - face->vertex0.x) * normal.x + ((localStart.y >> 5) - face->vertex0.y) * normal.y
								+ ((localStart.z >> 5) - face->vertex0.z) * normal.z
							>> 14)
						- radius / 32;
					if (startDistanceFromPlane >= 0)
					{
						int32_t distanceRange = startDistanceFromPlane - endDistanceFromPlane;
						int32_t hitX = localStart.x + (localEnd.x - localStart.x) * startDistanceFromPlane / distanceRange - (normal.x * radius >> 14);
						int32_t hitY = localStart.y + (localEnd.y - localStart.y) * startDistanceFromPlane / distanceRange - (normal.y * radius >> 14);
						int32_t hitZ = localStart.z + (localEnd.z - localStart.z) * startDistanceFromPlane / distanceRange - (normal.z * radius >> 14);
						if (Math::InsidePolLines((hitX >> 5) - face->vertex0.x,
								(hitY >> 5) - face->vertex0.y,
								(hitZ >> 5) - face->vertex0.z,
								face->vertex1Offset.x,
								face->vertex1Offset.y,
								face->vertex1Offset.z,
								face->vertex2Offset.x,
								face->vertex2Offset.y,
								face->vertex2Offset.z,
								&face->firstPlaneNormal,
								radius)
							!= 0)
						{
							int32_t fraction = (startDistanceFromPlane << 14) / distanceRange;
							if (fraction < nearestFraction)
							{
								startDistance = startDistanceFromPlane;
								endDistance = endDistanceFromPlane;
								nearestFraction = fraction;
								hitNormal.xyzw = *reinterpret_cast<const Vector4I16*>(&face->firstPlaneNormal);
								hitCount++;
							}
						}
					}
				}

				if (face->secondPlaneNormal.y != 0x7FFF)
				{
					const Vector3I16& secondNormal = face->secondPlaneNormal;
					int32_t endDistanceFromPlane =
						(((endX - face->vertex1Offset.x) - face->vertex0.x) * secondNormal.x
								+ ((endY - face->vertex1Offset.y) - face->vertex0.y) * secondNormal.y
								+ ((endZ - face->vertex1Offset.z) - face->vertex0.z) * secondNormal.z
							>> 14)
						- radius / 32;
					if (endDistanceFromPlane < 0)
					{
						int32_t startDistanceFromPlane =
							((((localStart.x >> 5) - face->vertex1Offset.x) - face->vertex0.x) * secondNormal.x
									+ (((localStart.y >> 5) - face->vertex1Offset.y) - face->vertex0.y) * secondNormal.y
									+ (((localStart.z >> 5) - face->vertex1Offset.z) - face->vertex0.z) * secondNormal.z
								>> 14)
							- radius / 32;
						if (startDistanceFromPlane >= 0)
						{
							int32_t distanceRange = startDistanceFromPlane - endDistanceFromPlane;
							int32_t hitX =
								localStart.x + (localEnd.x - localStart.x) * startDistanceFromPlane / distanceRange - (secondNormal.x * radius >> 14);
							int32_t hitY =
								localStart.y + (localEnd.y - localStart.y) * startDistanceFromPlane / distanceRange - (secondNormal.y * radius >> 14);
							int32_t hitZ =
								localStart.z + (localEnd.z - localStart.z) * startDistanceFromPlane / distanceRange - (secondNormal.z * radius >> 14);
							if (Math::InsidePolLines(((hitX >> 5) - face->vertex1Offset.x) - face->vertex0.x,
									((hitY >> 5) - face->vertex1Offset.y) - face->vertex0.y,
									((hitZ >> 5) - face->vertex1Offset.z) - face->vertex0.z,
									face->vertex3Offset.x - face->vertex1Offset.x,
									face->vertex3Offset.y - face->vertex1Offset.y,
									face->vertex3Offset.z - face->vertex1Offset.z,
									face->vertex2Offset.x - face->vertex1Offset.x,
									face->vertex2Offset.y - face->vertex1Offset.y,
									face->vertex2Offset.z - face->vertex1Offset.z,
									&face->secondPlaneNormal,
									radius)
								!= 0)
							{
								int32_t fraction = (startDistanceFromPlane << 14) / distanceRange;
								if (fraction < nearestFraction)
								{
									startDistance = startDistanceFromPlane;
									endDistance = endDistanceFromPlane;
									nearestFraction = fraction;
									hitNormal.xyzw = *reinterpret_cast<const Vector4I16*>(&secondNormal);
									hitCount++;
								}
							}
						}
					}
				}
			}

			if (SweepAgainstEdges(&nearestFraction, &startDistance, &endDistance, &hitNormal.xyz, position, movement, radius) == 1)
			{
				hitCount++;
			}

			if (hitCount > 0)
			{
				int32_t distanceRange = startDistance - endDistance;
				position->x += movement->x * startDistance / distanceRange;
				position->y += movement->y * startDistance / distanceRange;
				position->z += movement->z * startDistance / distanceRange;
				Toy2::Collision::g_groundNormal.x = hitNormal.xyz.x;
				Toy2::Collision::g_groundNormal.y = hitNormal.xyz.y;
				Toy2::Collision::g_groundNormal.z = hitNormal.xyz.z;
				return 1;
			}

			position->x += movement->x;
			position->y += movement->y;
			position->z += movement->z;
			return 0;
		}

		// FUNCTION: TOY2 0x00481140 [MATCHED]
		int16_t IsPointInTriangle(int32_t pointX,
			int32_t pointY,
			int32_t pointZ,
			int32_t edge1X,
			int32_t edge1Y,
			int32_t edge1Z,
			int32_t edge2X,
			int32_t edge2Y,
			int32_t edge2Z,
			const Vector3I16* normal)
		{
			if (abs(normal->y) >= abs(normal->x) && abs(normal->y) >= abs(normal->z))
			{
				if (normal->y < 0)
				{
					if (pointX * edge1Z - pointZ * edge1X >= 0
						&& (pointZ - edge2Z) * edge2X - (pointX - edge2X) * edge2Z >= 0
						&& (pointZ - edge1Z) * (edge1X - edge2X)
							+ (pointX - edge1X) * (edge2Z - edge1Z)
						>= 0)
						return 1;
				}
				else
				{
					if ((pointZ - edge1Z) * edge1X - (pointX - edge1X) * edge1Z >= 0
						&& pointX * edge2Z - pointZ * edge2X >= 0
						&& (edge2X - edge1X) * (pointZ - edge2Z)
							+ (pointX - edge2X) * (edge1Z - edge2Z)
						>= 0)
						return 1;
				}
			}
			else if (abs(normal->x) >= abs(normal->y) && abs(normal->x) >= abs(normal->z))
			{
				if (normal->x < 0)
				{
					if ((pointZ - edge1Z) * edge1Y - (pointY - edge1Y) * edge1Z >= 0
						&& pointY * edge2Z - pointZ * edge2Y >= 0
						&& (edge2Y - edge1Y) * (pointZ - edge2Z)
							+ (pointY - edge2Y) * (edge1Z - edge2Z)
						>= 0)
						return 1;
				}
				else
				{
					if (pointY * edge1Z - pointZ * edge1Y >= 0
						&& (pointZ - edge2Z) * edge2Y - (pointY - edge2Y) * edge2Z >= 0
						&& (pointY - edge1Y) * (edge2Z - edge1Z)
							+ (pointZ - edge1Z) * (edge1Y - edge2Y)
						>= 0)
						return 1;
				}
			}
			else
			{
				if (normal->z < 0)
				{
					if (pointY * edge1X - pointX * edge1Y >= 0
						&& (pointX - edge2X) * edge2Y - (pointY - edge2Y) * edge2X >= 0
						&& (pointY - edge1Y) * (edge2X - edge1X)
							+ (pointX - edge1X) * (edge1Y - edge2Y)
						>= 0)
						return 1;
				}
				else
				{
					if ((pointX - edge1X) * edge1Y - (pointY - edge1Y) * edge1X >= 0
						&& pointY * edge2X - pointX * edge2Y >= 0
						&& (pointX - edge2X) * (edge2Y - edge1Y)
							+ (pointY - edge2Y) * (edge1X - edge2X)
						>= 0)
						return 1;
				}
			}

			return 0;
		}

		// FUNCTION: TOY2 0x00487B20 [PROVISIONAL]
		int32_t SweepAgainstEdges(int32_t* nearestFraction,
			int32_t* startDistance,
			int32_t* endDistance,
			Vector3I16* hitNormal,
			const Vector4I* start,
			const Vector4I* movement,
			int32_t radius)
		{
			if (Toy2::Collision::g_collisionEdgeVertexCount == 0)
				return 0;

			int32_t foundHit = 0;
			Vector3I16 normal;
			normal.y = 0;
			int32_t expandedRadius = (radius + 0x2000) >> 5;

			for (int32_t vertexIndex = 0; vertexIndex < Toy2::Collision::g_collisionEdgeVertexCount; vertexIndex += 2)
			{
				const Vector3I& edgeStart = Toy2::Collision::g_collisionEdgeVertices[vertexIndex];
				const Vector3I& edgeEnd = Toy2::Collision::g_collisionEdgeVertices[vertexIndex + 1];

				normal.x = (int16_t)edgeStart.z - (int16_t)edgeEnd.z;
				normal.z = (int16_t)edgeEnd.x - (int16_t)edgeStart.x;
				Nu3D::Math::NormalizeToFixedPoint16(&normal, &normal);

				int32_t startDistanceFromEdge;
				if (abs(movement->x) < 0x2000 && abs(movement->z) < 0x2000)
				{
					startDistanceFromEdge = (((start->x - edgeStart.x * 0x20) * normal.x
						+ (start->z - edgeStart.z * 0x20) * normal.z)
						>> 12)
						- radius;
				}
				else
				{
					startDistanceFromEdge = (((start->x >> 5) - edgeStart.x) * normal.x
						+ ((start->z >> 5) - edgeStart.z) * normal.z)
						>> 12;
					startDistanceFromEdge -= Toy2::Collision::ShiftTowardZero(radius, 5);
				}

				if (startDistanceFromEdge >= 0)
				{
					int32_t endDistanceFromEdge;
					if (abs(movement->x) < 0x2000 && abs(movement->z) < 0x2000)
					{
						endDistanceFromEdge = (((start->x - edgeStart.x * 0x20 + movement->x) * normal.x
							+ (start->z - edgeStart.z * 0x20 + movement->z) * normal.z)
							>> 12)
							- radius;
					}
					else
					{
						endDistanceFromEdge = ((((start->x + movement->x) >> 5) - edgeStart.x) * normal.x
							+ (((start->z + movement->z) >> 5) - edgeStart.z) * normal.z)
							>> 12;
						endDistanceFromEdge -= Toy2::Collision::ShiftTowardZero(radius, 5);
					}

					if (endDistanceFromEdge < 0)
					{
						int16_t edgeDeltaX = (int16_t)edgeEnd.x - (int16_t)edgeStart.x;
						int16_t edgeDeltaZ = (int16_t)edgeEnd.z - (int16_t)edgeStart.z;
						int32_t distanceRange = startDistanceFromEdge - endDistanceFromEdge;
						int32_t hitX = start->x + movement->x * startDistanceFromEdge / distanceRange
							- (normal.x * radius >> 12);
						int32_t hitZ = start->z + movement->z * startDistanceFromEdge / distanceRange
							- (normal.z * radius >> 12);
						int32_t fromStart = (hitX - edgeStart.x * 0x20) * edgeDeltaX
							+ (hitZ - edgeStart.z * 0x20) * edgeDeltaZ;
						int32_t fromEnd = (hitX - edgeEnd.x * 0x20) * edgeDeltaX + (hitZ - edgeEnd.z * 0x20) * edgeDeltaZ;

						if (fromStart >= -0x2000 && fromEnd <= 0x2000)
						{
							int32_t fraction = (startDistanceFromEdge << 14) / distanceRange;
							if (fraction < *nearestFraction)
							{
								*startDistance = startDistanceFromEdge;
								*endDistance = endDistanceFromEdge;
								normal.x *= 4;
								normal.z *= 4;
								*hitNormal = normal;
								*nearestFraction = fraction;
								foundHit = 1;
							}
						}
					}
				}

				int32_t edgeOffsetX = edgeStart.x - (start->x >> 5);
				int32_t edgeOffsetZ = edgeStart.z - (start->z >> 5);
				int32_t movementX = movement->x >> 5;
				int32_t movementZ = movement->z >> 5;
				if (edgeOffsetX * edgeOffsetX + edgeOffsetZ * edgeOffsetZ
					< movementX * movementX + movementZ * movementZ + expandedRadius * expandedRadius)
				{
					Vector3I16 direction = { (int16_t)movementX, 0, (int16_t)movementZ };
					Nu3D::Math::NormalizeToFixedPoint16(&direction, &direction);

					int32_t projectedDistance = ((edgeStart.z - (start->z >> 5)) * direction.z
						+ (edgeStart.x - (start->x >> 5)) * direction.x)
						>> 12;
					if (projectedDistance >= 0)
					{
						int32_t closestX = edgeStart.x - ((start->x + (direction.x * projectedDistance >> 7)) >> 5);
						int32_t closestZ = edgeStart.z - ((start->z + (direction.z * projectedDistance >> 7)) >> 5);
						int32_t closestDistanceSquared = closestX * closestX + closestZ * closestZ;
						int32_t nearRadius = (radius + 0x20) >> 5;
						if (closestDistanceSquared <= nearRadius * nearRadius)
						{
							int32_t hitRadius = (radius + 0x40) >> 5;
							int32_t hitDistance = projectedDistance
								- (int32_t)sqrt((double)(hitRadius * hitRadius - closestDistanceSquared));
							int32_t movementLengthSquared = movementX * movementX + movementZ * movementZ;
							if (hitDistance >= -2 && hitDistance * hitDistance <= movementLengthSquared)
							{
								if (hitDistance < 0)
									hitDistance = 0;

								int32_t movementLength = (int32_t)sqrt((double)movementLengthSquared);
								int32_t fraction = (hitDistance << 14) / movementLength;
								if (fraction < *nearestFraction)
								{
									normal.x = (int16_t)(direction.x * hitDistance >> 7) - (int16_t)edgeStart.x * 0x20
										+ (int16_t)start->x;
									normal.z = (int16_t)(direction.z * hitDistance >> 7) - (int16_t)edgeStart.z * 0x20
										+ (int16_t)start->z;
									Nu3D::Math::NormalizeToFixedPoint16(&normal, &normal);

									*startDistance = hitDistance;
									*endDistance = hitDistance - movementLength;
									normal.x *= 4;
									normal.z *= 4;
									*hitNormal = normal;
									*nearestFraction = fraction;
									foundHit = 1;
								}
							}
						}
					}
				}
			}
			return foundHit;
		}

		// FUNCTION: TOY2 0x004882F0 [PROVISIONAL]
		int32_t RaycastAgainstEdges(int32_t* nearestFraction,
			int32_t* startDistance,
			int32_t* endDistance,
			Vector3I16* hitNormal,
			uint16_t* reversed,
			const Vector3I* start,
			const Vector3I* movement)
		{
			if (Toy2::Collision::g_collisionEdgeVertexCount == 0)
				return 0;

			int32_t foundHit = 0;
			for (int32_t vertexIndex = 0; vertexIndex < Toy2::Collision::g_collisionEdgeVertexCount; vertexIndex += 2)
			{
				const Vector3I& edgeStart = Toy2::Collision::g_collisionEdgeVertices[vertexIndex];
				const Vector3I& edgeEnd = Toy2::Collision::g_collisionEdgeVertices[vertexIndex + 1];
				Vector3I16 edgeNormal = {
					(int16_t)(edgeStart.z - edgeEnd.z),
					0,
					(int16_t)(edgeEnd.x - edgeStart.x),
				};
				Nu3D::Math::NormalizeToFixedPoint16(&edgeNormal, &edgeNormal);

				int32_t rayStartDistance = (((start->z >> 5) - edgeStart.z) * edgeNormal.z + ((start->x >> 5) - edgeStart.x) * edgeNormal.x) >> 12;
				int32_t rayEndDistance =
					((((start->z + movement->z) >> 5) - edgeStart.z) * edgeNormal.z + (((start->x + movement->x) >> 5) - edgeStart.x) * edgeNormal.x) >> 12;
				int32_t rayReversed = 0;
				if (rayStartDistance < rayEndDistance)
				{
					rayStartDistance = -rayStartDistance;
					rayEndDistance = -rayEndDistance;
					rayReversed = 1;
				}

				if (rayStartDistance >= 0 && rayEndDistance < 0)
				{
					int32_t distanceRange = rayStartDistance - rayEndDistance;
					int16_t edgeDeltaX = edgeEnd.x - edgeStart.x;
					int16_t edgeDeltaZ = edgeEnd.z - edgeStart.z;
					int32_t hitX = start->x + movement->x * rayStartDistance / distanceRange;
					int32_t hitZ = start->z + movement->z * rayStartDistance / distanceRange;
					int32_t fromStart = (hitX - edgeStart.x * 32) * edgeDeltaX + (hitZ - edgeStart.z * 32) * edgeDeltaZ;
					int32_t fromEnd = (hitX - edgeEnd.x * 32) * edgeDeltaX + (hitZ - edgeEnd.z * 32) * edgeDeltaZ;
					int32_t fraction = (rayStartDistance << 14) / distanceRange;

					if (fromStart >= 0 && fromEnd <= 0 && fraction < *nearestFraction)
					{
						foundHit = 1;
						*nearestFraction = fraction;
						*startDistance = rayStartDistance;
						*endDistance = rayEndDistance;
						hitNormal->x = edgeNormal.x * 4;
						hitNormal->y = 0;
						hitNormal->z = edgeNormal.z * 4;
						*reversed = rayReversed;
					}
				}
			}
			return foundHit;
		}

		// FUNCTION: TOY2 0x00486280 [MATCHED]
		int32_t GetGroundHeight(const PosAndAngles* position, int32_t shadowSize)
		{
			int32_t previousTriangleCount = Toy2::Collision::g_collisionTriangleCount;
			Toy2::Collision::GatherTrianglesAtXZ(&position->pos);

			PosAndAngles groundPosition = *position;
			Toy2::Collision::ResolveGroundCeiling(&groundPosition, 0x10000);
			if (shadowSize != 0 && Renderer::Shadows::g_shadowCount < 47)
			{
				Toy2::Shadow::QueueStretched(groundPosition.pos.x, groundPosition.pos.y, groundPosition.pos.z, shadowSize, position->pos.y);
			}
			Toy2::Collision::g_collisionTriangleCount = previousTriangleCount;
			return groundPosition.pos.y;
		}

		// FUNCTION: TOY2 0x00486310 [MATCHED]
		int32_t GetGroundHeightEx(const PosAndAngles* position, int32_t shadowSize, int32_t probeRadius)
		{
			int32_t previousTriangleCount = Toy2::Collision::g_collisionTriangleCount;
			Toy2::Collision::GatherTrianglesAtXZ(&position->pos);

			PosAndAngles groundPosition = *position;
			Toy2::Collision::ResolveGroundCeiling(&groundPosition, probeRadius);
			if (shadowSize != 0 && Renderer::Shadows::g_shadowCount < 47)
			{
				Toy2::Shadow::QueueStretched(groundPosition.pos.x, groundPosition.pos.y, groundPosition.pos.z, shadowSize, position->pos.y);
			}
			Toy2::Collision::g_collisionTriangleCount = previousTriangleCount;
			return groundPosition.pos.y;
		}

		// FUNCTION: TOY2 0x00487A60 [MATCHED]
		int32_t IsFloorWalkable() { return Toy2::Collision::g_groundNormal.y >= -0x2000; }

		// FUNCTION: TOY2 0x00487AB0 [MATCHED]
		int32_t GetSurfaceQuality(int32_t queryIndex)
		{
			uint16_t surfaceType = Toy2::Collision::g_collisionQueryResults[queryIndex].surfaceType;
			if ((int8_t)surfaceType == -1)
				return -1;
			return surfaceType & 0xFF;
		}
	}
}

namespace Toy2
{
	namespace Camera
	{
		// FUNCTION: TOY2 0x0048A4C0 [PROVISIONAL]
		void PrepareCameraCollisionCandidates(const Vector3I* position, int32_t movementX, int32_t movementY, int32_t movementZ, int32_t radius)
		{
			const int32_t queryRadius = 0x6400;
			const int32_t queryDiameter = 0xC800;
			const int32_t diagonalScale = 0x1644;
			const int32_t coordinateShift = 5;
			const int32_t blockScale = 64;
			const int32_t maxCandidateCount = 240;
			const int32_t workspaceSlotIndex = 8;

			Collision::g_collisionEdgeVertexCount = 0;
			Collision::g_collisionPassFlags = 0;
			Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].entryCount = 0;

			int32_t maximumX;
			int32_t maximumY;
			int32_t maximumZ;
			int32_t querySizeX;
			int32_t querySizeY;
			int32_t querySizeZ;
			if (movementX < 0)
			{
				maximumX = position->x + queryRadius;
				querySizeX = queryDiameter - movementX;
			}
			else
			{
				maximumX = position->x + queryRadius + movementX;
				querySizeX = queryDiameter + movementX;
			}

			if (movementY < 0)
			{
				maximumY = position->y + queryRadius;
				querySizeY = queryDiameter - movementY;
			}
			else
			{
				maximumY = position->y + queryRadius + movementY;
				querySizeY = queryDiameter + movementY;
			}

			if (movementZ < 0)
			{
				maximumZ = position->z + queryRadius;
				querySizeZ = queryDiameter - movementZ;
			}
			else
			{
				maximumZ = position->z + queryRadius + movementZ;
				querySizeZ = queryDiameter + movementZ;
			}

			int32_t scaledRadius = radius * diagonalScale;
			int32_t padding = scaledRadius / 0x1000;
			maximumX += padding;
			maximumY += padding;
			maximumZ += padding;
			padding = scaledRadius / 0x800;
			querySizeX += padding;
			querySizeY += padding;
			querySizeZ += padding;

			Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].queryBounds.maximum.x = maximumX;
			Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].queryBounds.maximum.y = maximumY;
			Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].queryBounds.maximum.z = maximumZ;
			Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].queryBounds.minimum.x = maximumX - querySizeX;
			Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].queryBounds.minimum.y = maximumY - querySizeY;
			Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].queryBounds.minimum.z = maximumZ - querySizeZ;

			uint32_t querySizeXBlocks = (uint32_t)(querySizeX + 0x1F) >> coordinateShift;
			uint32_t querySizeYBlocks = (uint32_t)(querySizeY + 0x1F) >> coordinateShift;
			uint32_t querySizeZBlocks = (uint32_t)(querySizeZ + 0x1F) >> coordinateShift;
			int16_t* candidateMeshIndices = reinterpret_cast<int16_t*>(Collision::g_mathScratch);
			int16_t* candidateWrite = candidateMeshIndices;
			int32_t candidateMeshCount = 0;

			Collision::CollisionGridCell* cell = Collision::g_collisionGrid;
			int32_t remainingCellCount = Collision::g_activeCollisionGridCellCount;
			while (remainingCellCount > 0)
			{
				if ((uint32_t)(maximumX - cell->boundsMinX) < (uint32_t)(cell->boundsExtentX + querySizeX)
					&& (uint32_t)(maximumZ - cell->boundsMinZ) < (uint32_t)(cell->boundsExtentZ + querySizeZ))
				{
					int32_t meshCount = cell->meshCount;
					int16_t* meshList = &Collision::g_collisionGridMeshIndices[cell->meshListStart];
					int32_t remainingMeshCount = meshCount;
					while (remainingMeshCount > 0)
					{
						*candidateWrite++ = *meshList++;
						remainingMeshCount--;
					}
					candidateMeshCount += meshCount;
				}
				cell++;
				remainingCellCount--;
			}

			Collision::CollisionMeshRecord* collisionMeshes = reinterpret_cast<Collision::CollisionMeshRecord*>(Collision::g_collisionMeshInstances);
			int16_t* candidateMesh = candidateMeshIndices;
			for (; candidateMeshCount > 0; candidateMesh++, candidateMeshCount--)
			{
				int32_t meshIndex = *candidateMesh;
				Collision::CollisionMeshRecord& mesh = collisionMeshes[meshIndex];
				if ((uint32_t)(maximumX - mesh.boundsMin.x) >= (uint32_t)(mesh.boundsExt.x + querySizeX)
					|| (uint32_t)(maximumY - mesh.boundsMin.y) >= (uint32_t)(mesh.boundsExt.y + querySizeY)
					|| (uint32_t)(maximumZ - mesh.boundsMin.z) >= (uint32_t)(mesh.boundsExt.z + querySizeZ) || mesh.typeFlags == 0 || (mesh.flags & 0x200) != 0)
				{
					continue;
				}

				int32_t localMaximumX = (maximumX - mesh.origin.x) / 0x20;
				int32_t localMaximumY = (maximumY - mesh.origin.y) / 0x20;
				int32_t localMaximumZ = (maximumZ - mesh.origin.z) / 0x20;
				Collision::CollisionTreeGroup* group = mesh.collisionTree;

				while (group->marker >= 0)
				{
					int32_t faceCount = group->faceCount;
					Collision::PackedCollisionFace* face = reinterpret_cast<Collision::PackedCollisionFace*>(group + 1);
					if ((uint32_t)(localMaximumX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + querySizeXBlocks)
						&& (uint32_t)(localMaximumZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + querySizeZBlocks))
					{
						for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
						{
							if ((uint32_t)(localMaximumX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + querySizeXBlocks)
								&& (uint32_t)(localMaximumZ - face->boundsMinZBlock * blockScale - face->vertex0.z)
									< (uint32_t)(face->boundsExtentZBlock * blockScale + querySizeZBlocks)
								&& (uint32_t)(localMaximumY - face->boundsMinYBlock * blockScale - face->vertex0.y)
									< (uint32_t)(face->boundsExtentYBlock * blockScale + querySizeYBlocks)
								&& Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].entryCount < maxCandidateCount)
							{
								Terrain::g_collisionWorkspace->slots[workspaceSlotIndex]
									.faces[Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].entryCount] = face;
								Terrain::g_collisionWorkspace->slots[workspaceSlotIndex]
									.meshIndices[Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].entryCount] = *candidateMesh;
								Terrain::g_collisionWorkspace->slots[workspaceSlotIndex].entryCount++;
							}
						}
					}
					else
					{
						face += faceCount;
					}
					group = reinterpret_cast<Collision::CollisionTreeGroup*>(face);
				}
			}
		}
	}
}

namespace Toy2
{
	namespace Shadow
	{
		// FUNCTION: TOY2 0x00485680 [PROVISIONAL]
		void QueueStretched(int32_t x, int32_t groundY, int32_t z, int32_t size, int32_t sourceY)
		{
			int32_t opacity = (sourceY - groundY) / 0x400 + 0x40;
			if (opacity < 0)
				return;
			if (opacity > 0x20)
				opacity = 0x20;

			int16_t normalY = Collision::g_groundNormal.y;
			if (normalY >= -0x1000 || Renderer::Shadows::g_shadowCount >= Renderer::Shadows::MAX_SHADOWS)
				return;

			int32_t shadowIndex = Renderer::Shadows::g_shadowCount;
			Renderer::Shadows::g_shadowInstances[shadowIndex].opacity = (int16_t)opacity;
			Renderer::Shadows::g_shadowInstances[shadowIndex].pos.x = x;
			Renderer::Shadows::g_shadowInstances[shadowIndex].pos.z = z;
			if (normalY > -0x3000)
				size = ((-0x800 - normalY) * size) / 0x2800;

			Renderer::Shadows::g_shadowInstances[shadowIndex].size = (int16_t)-size;
			int32_t normalZ = Collision::g_groundNormal.z;
			int32_t normalX = Collision::g_groundNormal.x;
			int32_t slopeHeight = ((normalZ + normalX) * size) / normalY;
			Renderer::Shadows::g_shadowInstances[shadowIndex].pos.y = groundY + slopeHeight * 0x20;

			Renderer::Shadows::g_shadowProjections[shadowIndex].cornerYOffsets[0] = (int16_t)(((normalZ - normalX) * size) / normalY) - (int16_t)slopeHeight;
			Renderer::Shadows::g_shadowProjections[shadowIndex].cornerYOffsets[1] = (int16_t)(((normalX - normalZ) * size) / normalY) - (int16_t)slopeHeight;
			Renderer::Shadows::g_shadowProjections[shadowIndex].cornerYOffsets[2] = (int16_t)-(slopeHeight * 2);
			if (Collision::g_groundNormal.y < -0x2000)
			{
				Renderer::Shadows::g_shadowCount++;
				Renderer::Shadows::g_shadowProjections[shadowIndex].opacity = 0x30;
				return;
			}
			int16_t projectionOpacity = (int16_t)((-0x1000 - normalY) * 3 >> 8);
			Renderer::Shadows::g_shadowCount++;
			Renderer::Shadows::g_shadowProjections[shadowIndex].opacity = projectionOpacity;
		}

		// FUNCTION: TOY2 0x004857E0 [PROVISIONAL]
		void QueueStretchedForBuzz(int32_t x, int32_t groundY, int32_t z, int32_t size)
		{
			int32_t opacity = (g_buzzActor.posAngles.pos.y - groundY) / 0x400 + 0x40;
			if (opacity < 0)
				return;
			if (opacity > 0x20)
				opacity = 0x20;

			int16_t normalY = Collision::g_buzzGroundNormal.y;
			if (normalY >= -0x1000 || Renderer::Shadows::g_shadowCount >= Renderer::Shadows::MAX_SHADOWS)
				return;

			int32_t shadowIndex = Renderer::Shadows::g_shadowCount;
			Renderer::Shadows::g_shadowInstances[shadowIndex].opacity = (int16_t)opacity;
			Renderer::Shadows::g_shadowInstances[shadowIndex].pos.x = x;
			Renderer::Shadows::g_shadowInstances[shadowIndex].pos.z = z;
			if (normalY > -0x3000)
				size = ((-0x800 - normalY) * size) / 0x2800;

			Renderer::Shadows::g_shadowInstances[shadowIndex].size = (int16_t)-size;
			int32_t normalZ = Collision::g_buzzGroundNormal.z;
			int32_t normalX = Collision::g_buzzGroundNormal.x;
			int32_t slopeHeight = ((normalZ + normalX) * size) / normalY;
			Renderer::Shadows::g_shadowInstances[shadowIndex].pos.y = groundY + slopeHeight * 0x20;

			Renderer::Shadows::g_shadowProjections[shadowIndex].cornerYOffsets[0] = (int16_t)(((normalZ - normalX) * size) / normalY) - (int16_t)slopeHeight;
			Renderer::Shadows::g_shadowProjections[shadowIndex].cornerYOffsets[1] = (int16_t)(((normalX - normalZ) * size) / normalY) - (int16_t)slopeHeight;
			Renderer::Shadows::g_shadowProjections[shadowIndex].cornerYOffsets[2] = (int16_t)-(slopeHeight * 2);
			if (Collision::g_buzzGroundNormal.y < -0x2000)
			{
				Renderer::Shadows::g_shadowCount++;
				Renderer::Shadows::g_shadowProjections[shadowIndex].opacity = 0x30;
				return;
			}
			int16_t projectionOpacity = (int16_t)((-0x1000 - normalY) * 3 >> 8);
			Renderer::Shadows::g_shadowCount++;
			Renderer::Shadows::g_shadowProjections[shadowIndex].opacity = projectionOpacity;
		}
	}
}

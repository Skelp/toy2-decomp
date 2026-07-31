#include "Toy2/Collision.h"
#include "FileUtils.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Renderer/Renderer.h"
#include "Renderer/Shadows.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Toy2
{
	extern int32_t g_ziplineState;
	extern int32_t g_ledgeClimbTimer;
	extern int32_t g_poleClimbState;

	namespace Levels
	{
		extern void* g_cachedAllBuffer;

		void BuildLevelPath(int32_t level, char* output, const char* suffix);
	}

	namespace Collision
	{
		struct PackedCollisionFace;
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

		struct PlatformRuntimeRecord
		{
			Vector3I origin;
			Vector3I16 rotationAnglesFixed;
			int16_t motionMode;
			Vector3I16 velocity;
			Vector3I16 remainingTranslation;
			Vector3I16 angularVelocity;
			Vector3I16 remainingRotation;
			Platform::CollisionFace* contactFace;
			int16_t collisionMeshIndex;
			int16_t flags;
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
		STATIC_ASSERT(sizeof(PlatformRuntimeRecord) == 0x34);
		STATIC_ASSERT(offsetof(PlatformRuntimeRecord, collisionMeshIndex) == 0x30);
		STATIC_ASSERT(sizeof(CollisionCachePosition) == 0x18);
		STATIC_ASSERT(sizeof(CollisionQueryBounds) == 0x18);
		STATIC_ASSERT(sizeof(CollisionWorkspaceSlot) == 0x5C0);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, activeFrames) == 0x18);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, entryCount) == 0x1C);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, meshIndices) == 0x20);
		STATIC_ASSERT(offsetof(CollisionWorkspaceSlot, faces) == 0x200);
		STATIC_ASSERT(sizeof(CollisionWorkspace) == 0x33C0);
		STATIC_ASSERT(sizeof(TerrainRelocationLink) == 0x04);

		// GLOBAL: TOY2 0x007286EC
		PlatformRuntimeRecord g_platformRuntimeRecords[33];

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
							int32_t sourceMeshIndex = sourceMeshIndices[descriptor->sourceMeshId];
							if (sourceMeshIndex < 0)
							{
								mesh->typeFlags = 0;
							}
							else
							{
								*mesh = Collision::g_collisionMeshInstances[sourceMeshIndex];
								mesh->origin.x = descriptor->origin.x;
								mesh->origin.y = descriptor->origin.y;
								mesh->origin.z = descriptor->origin.z;
								mesh->platformIdx = descriptor->typeOrRelocationIndex;
								mesh->origin.x <<= 5;
								mesh->origin.y <<= 5;
								mesh->origin.z <<= 5;
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
								PlatformRuntimeRecord& platform = g_platformRuntimeRecords[descriptor->platformId];
								platform.collisionMeshIndex = static_cast<int16_t>(baseMeshIndex);
								platform.origin.x = mesh->origin.x;
								platform.origin.y = mesh->origin.y;
								platform.origin.z = mesh->origin.z;
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
		struct SurfaceCollisionResult
		{
			uint32_t contactFlags;
			Platform::CollisionFace* face;
			Vector3I16 normal;
			int16_t contactState;
			uint8_t reserved[8];
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
		};

		const int16_t COLLISION_MESH_STATIC_A = 6;
		const int16_t COLLISION_MESH_STATIC_B = 7;
		const uint16_t COLLISION_MESH_EXCLUDE_FROM_GRID = 0x400;
		const uint16_t COLLISION_MESH_EXCLUDE_FROM_QUERY = 0x100;

		static __forceinline int32_t ShiftTowardZero(int32_t value, int32_t bits) { return (value + ((value >> 31) & ((1 << bits) - 1))) >> bits; }

		STATIC_ASSERT(sizeof(CollisionNormal) == 0x08);
		STATIC_ASSERT(offsetof(CollisionNormal, reserved) == 0x06);
		STATIC_ASSERT(sizeof(SurfaceCollisionResult) == sizeof(CollisionQueryResult));
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, normal) == 0x08);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, contactState) == 0x0E);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, movement) == 0x18);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, surfaceVelocity) == 0x1E);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, contactTimer) == 0x24);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, collisionDistance) == 0x2C);
		STATIC_ASSERT(sizeof(PackedCollisionFace) == 0x2C);
		STATIC_ASSERT(offsetof(PackedCollisionFace, firstPlaneNormal) == 0x20);
		STATIC_ASSERT(offsetof(PackedCollisionFace, secondPlaneNormal) == 0x26);
		STATIC_ASSERT(sizeof(CollisionTreeGroup) == 0x0C);
		STATIC_ASSERT(sizeof(CollisionGridCell) == 0x14);
		STATIC_ASSERT(sizeof(CollisionMeshRecord) == sizeof(CollisionMeshInstance));
		STATIC_ASSERT(sizeof(CollisionSweep) == 0x3C);
		STATIC_ASSERT(offsetof(CollisionSweep, nearestFraction) == 0x20);
		STATIC_ASSERT(offsetof(CollisionSweep, hitNormal) == 0x34);

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

		// STUB: TOY2 0x0048C860
		int32_t SweepAndSlide(Vector3I* position, Vector3I* movement, int32_t collisionThreshold, int16_t* collisionAngles, int32_t radius) { return 0; }

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
					Nu3D::Math::EulerToRotationMatrix(&Animation::g_nextKeyframeRotation.angles, &Animation::g_keyframeRotation.matrix);

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

		// GLOBAL: TOY2 0x0072872C
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

namespace Nu3D
{
	namespace Collision
	{
		// FUNCTION: TOY2 0x00481140 [PROVISIONAL]
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
			int32_t absNormalX = abs(normal->x);
			int32_t absNormalY = abs(normal->y);
			int32_t absNormalZ = abs(normal->z);
			PointI point;
			PointI edge1;
			PointI edge2;
			int32_t normalDirection;

			if (absNormalX >= absNormalY && absNormalX >= absNormalZ)
			{
				point.x = pointY;
				point.y = pointZ;
				edge1.x = edge1Y;
				edge1.y = edge1Z;
				edge2.x = edge2Y;
				edge2.y = edge2Z;
				normalDirection = normal->x;
			}
			else if (absNormalY >= absNormalZ)
			{
				point.x = pointX;
				point.y = pointZ;
				edge1.x = edge1X;
				edge1.y = edge1Z;
				edge2.x = edge2X;
				edge2.y = edge2Z;
				normalDirection = -normal->y;
			}
			else
			{
				point.x = pointX;
				point.y = pointY;
				edge1.x = edge1X;
				edge1.y = edge1Y;
				edge2.x = edge2X;
				edge2.y = edge2Y;
				normalDirection = normal->z;
			}

			int32_t side1 = edge1.x * point.y - edge1.y * point.x;
			int32_t side2 = (edge1.y - edge2.y) * (point.x - edge2.x) - (point.y - edge2.y) * (edge1.x - edge2.x);
			int32_t side3 = edge2.y * point.x - point.y * edge2.x;

			if (normalDirection < 0)
				return side1 >= 0 && side2 >= 0 && side3 >= 0;
			return side1 <= 0 && side2 <= 0 && side3 <= 0;
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
				bool rayReversed = rayStartDistance < rayEndDistance;
				if (rayReversed)
				{
					rayStartDistance = -rayStartDistance;
					rayEndDistance = -rayEndDistance;
				}

				if (rayStartDistance >= 0 && rayEndDistance < 0)
				{
					int32_t distanceRange = rayStartDistance - rayEndDistance;
					int32_t hitX = start->x + movement->x * rayStartDistance / distanceRange;
					int32_t hitZ = start->z + movement->z * rayStartDistance / distanceRange;
					int32_t edgeDeltaX = edgeEnd.x - edgeStart.x;
					int32_t edgeDeltaZ = edgeEnd.z - edgeStart.z;
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
			int32_t maximumPadding = scaledRadius / 0x1000;
			int32_t sizePadding = scaledRadius / 0x800;
			maximumX += maximumPadding;
			maximumY += maximumPadding;
			maximumZ += maximumPadding;
			querySizeX += sizePadding;
			querySizeY += sizePadding;
			querySizeZ += sizePadding;

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

			for (int32_t cellIndex = 0; cellIndex < Collision::g_activeCollisionGridCellCount; cellIndex++)
			{
				Collision::CollisionGridCell& cell = Collision::g_collisionGrid[cellIndex];
				if ((uint32_t)(maximumX - cell.boundsMinX) < (uint32_t)(cell.boundsExtentX + querySizeX)
					&& (uint32_t)(maximumZ - cell.boundsMinZ) < (uint32_t)(cell.boundsExtentZ + querySizeZ))
				{
					int32_t meshCount = cell.meshCount;
					int16_t* meshList = &Collision::g_collisionGridMeshIndices[cell.meshListStart];
					while (meshCount > 0)
					{
						*candidateWrite++ = *meshList++;
						meshCount--;
					}
					candidateMeshCount += cell.meshCount;
				}
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

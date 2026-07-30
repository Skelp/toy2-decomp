#include "Toy2/Collision.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Renderer/Shadows.h"
#include "Toy2/Buzz.h"

namespace Toy2
{
	extern int32_t g_ziplineState;
	extern int32_t g_ledgeClimbTimer;
	extern int32_t g_poleClimbState;

	namespace Levels
	{
		void BuildLevelPath(int32_t level, char* output, const char* suffix);
	}

	namespace Terrain
	{
		// STUB: TOY2 0x00489980
		int32_t LoadAll(char*, int32_t, uint8_t**) { return 0; }
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
			uint8_t reserved[12];
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

		const int16_t COLLISION_MESH_STATIC_A = 6;
		const int16_t COLLISION_MESH_STATIC_B = 7;
		const uint16_t COLLISION_MESH_EXCLUDE_FROM_GRID = 0x400;

		STATIC_ASSERT(sizeof(SurfaceCollisionResult) == sizeof(CollisionQueryResult));
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, normal) == 0x08);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, contactState) == 0x0E);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, movement) == 0x18);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, surfaceVelocity) == 0x1E);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, contactTimer) == 0x24);
		STATIC_ASSERT(offsetof(SurfaceCollisionResult, collisionDistance) == 0x2C);
		STATIC_ASSERT(sizeof(PackedCollisionFace) == 0x2C);
		STATIC_ASSERT(sizeof(CollisionTreeGroup) == 0x0C);
		STATIC_ASSERT(sizeof(CollisionGridCell) == 0x14);
		STATIC_ASSERT(sizeof(CollisionMeshRecord) == sizeof(CollisionMeshInstance));

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

		// STUB: TOY2 0x00485940
		void GatherTrianglesAtXZ(const Vector3I* position) {}

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
		// GLOBAL: TOY2 0x0072872C
		PlatformState g_platformStates[32];

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
					g_platformStates[platformIndex].flags &= ~0x80;
					g_platformStates[platformIndex].velocity.x = 0;
					g_platformStates[platformIndex].velocity.y = 0;
					g_platformStates[platformIndex].velocity.z = 0;
					return;
				}
				g_platformStates[platformIndex].flags |= 0x80;
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
				g_platformStates[destinationPlatformIndex].flags |= 0x80;
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

#include "Toy2/Collision.h"
#include "Toy2/CollisionInternal.h"
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

// The stretched shadows and the ground height they stand on: retail holds
// 0x00485680 through 0x00486310 as one object, in the order of this file.
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

namespace Toy2
{
	namespace Collision
	{
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

	}
}

namespace Nu3D
{
	namespace Collision
	{
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
	}
}

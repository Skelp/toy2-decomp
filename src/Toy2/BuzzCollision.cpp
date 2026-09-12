#include "Toy2/Buzz.h"
#include "Toy2/BuzzInternal.h"
#include "Toy2/Animation.h"
#include "Toy2/Actor.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/CollisionInternal.h"
#include "Toy2/Collectables.h"
#include "Toy2/Levels.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/PoleRecord.h"
#include "Toy2/Toy2.h"
#include "AudioManager/AudioManager.h"
#include "CharacterLoader.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// The collision resolve of Buzz: retail holds 0x00484380 and 0x004855F0 as
// one object, in the order of this file.
namespace Toy2
{
	namespace Buzz
	{
		// FUNCTION: TOY2 0x00484380 [PROVISIONAL]
		void ResolveCollisions(Toy2BuzzActor* buzz, MovementVelocity* movement, uint8_t* contactState, int32_t queryIndex, int32_t collisionPass)
		{
			Collision::SurfaceCollisionResult& result = reinterpret_cast<Collision::SurfaceCollisionResult*>(Collision::g_collisionQueryResults)[queryIndex];
			buzz->posAngles.pos.y -= result.collisionDistance + 0xC0;
			result.platformIndex = -1;
			result.movement.x = 0;
			result.movement.y = 0;
			result.movement.z = 0;
			Collision::g_collisionEdgeVertexCount = 0;
			Collision::g_collisionTriangleCount = 0;
			Collision::g_rotatedCollisionTriangleCount = 0;
			Collision::g_collisionPassFlags = 0;

			Vector3I16 requestedMovement;
			requestedMovement.x = (int16_t)movement->lateral;
			if (result.contactState == 1)
			{
				requestedMovement.y = (int16_t)movement->vertical;
			}
			else
			{
				requestedMovement.x += result.movement.x;
				requestedMovement.y = (int16_t)movement->vertical + result.movement.y;
			}
			requestedMovement.z = (int16_t)movement->forward;
			if (collisionPass != 0)
			{
				requestedMovement.x /= 2;
				requestedMovement.y /= 2;
				requestedMovement.z /= 2;
			}

			Platform::g_contactPlatformCount = 0;
			result.collisionDistance = 0;
			int32_t movementLength = (int32_t)sqrt(
				(double)(requestedMovement.x * requestedMovement.x + requestedMovement.y * requestedMovement.y + requestedMovement.z * requestedMovement.z));

			if (contactState[1] == 1)
			{
				int32_t meshIndex = result.contactFlags & Collision::COLLISION_CONTACT_MESH_INDEX_MASK;
				Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[meshIndex];
				const Vector3I16* normal =
					(result.contactFlags & Collision::COLLISION_CONTACT_SECONDARY_FACE) == 0 ? &result.face->normal : &result.face->secondaryNormal;
				Vector3I16 rotatedNormal;
				bool rotated = mesh.typeFlags == Collision::COLLISION_MESH_MOVING
					&& (Platform::g_platformStates[mesh.platformIdx].flags & Platform::PLATFORM_FLAG_ROTATED) != 0;
				if (rotated)
				{
					Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
					Vector3I16 angles = { (int16_t)(platform.rotationAnglesFixed.x >> 2),
						(int16_t)(platform.rotationAnglesFixed.y >> 2),
						(int16_t)(platform.rotationAnglesFixed.z >> 2) };
					Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
					int32_t value = Animation::g_keyframeRotation.matrix.m00 * normal->x + Animation::g_keyframeRotation.matrix.m01 * normal->y
						+ Animation::g_keyframeRotation.matrix.m02 * normal->z;
					rotatedNormal.x = (int16_t)((value + ((value >> 31) & 0xFFF)) >> 12);
					value = Animation::g_keyframeRotation.matrix.m10 * normal->x + Animation::g_keyframeRotation.matrix.m11 * normal->y
						+ Animation::g_keyframeRotation.matrix.m12 * normal->z;
					rotatedNormal.y = (int16_t)((value + ((value >> 31) & 0xFFF)) >> 12);
					value = Animation::g_keyframeRotation.matrix.m20 * normal->x + Animation::g_keyframeRotation.matrix.m21 * normal->y
						+ Animation::g_keyframeRotation.matrix.m22 * normal->z;
					rotatedNormal.z = (int16_t)((value + ((value >> 31) & 0xFFF)) >> 12);
					normal = &rotatedNormal;
				}

				if (normal->y >= -11999)
				{
					int32_t divisor;
					if (normal->y < -0x1000)
					{
						int32_t value = (movementLength + 0xC80) * normal->y;
						divisor = (value + ((value >> 31) & (rotated ? 0x7F : 0x1F))) >> (rotated ? 7 : 5);
						movement->lateral += normal->x * -0x1000 / divisor;
						movement->forward += normal->z * -0x1000 / divisor;
					}
					else
					{
						int32_t value = (movementLength + 0xC80) * 0x1000;
						divisor = (value + ((value >> 31) & (rotated ? 0x7F : 0x1F))) >> (rotated ? 7 : 5);
						movement->lateral += ((int32_t)normal->x << 12) / divisor;
						movement->forward += ((int32_t)normal->z << 12) / divisor;
					}
				}
			}

			int32_t value = result.collisionDistance * 8000;
			int32_t collisionRadius = movementLength + 0x1880 + ((value + ((value >> 31) & 0xFFF)) >> 12);
			if (queryIndex == 0)
				Platform::UpdateBuzzPlatformMotion(0, collisionPass);

			int16_t* candidateMeshes = reinterpret_cast<int16_t*>(Collision::g_mathScratch);
			int32_t candidateCount = 0;
			int32_t queryMaxX = buzz->posAngles.pos.x + collisionRadius;
			int32_t queryMaxY = buzz->posAngles.pos.y + collisionRadius;
			int32_t queryMaxZ = buzz->posAngles.pos.z + collisionRadius;
			int32_t queryDiameter = collisionRadius * 2;
			int32_t faceTolerance = (collisionRadius + 15 + (((collisionRadius + 15) >> 31) & 15)) >> 4;

			for (int32_t cellIndex = 0; cellIndex < Collision::g_activeCollisionGridCellCount; cellIndex++)
			{
				Collision::CollisionGridCell& cell = Collision::g_collisionGrid[cellIndex];
				if ((uint32_t)(queryMaxX - cell.boundsMinX) < (uint32_t)(cell.boundsExtentX + queryDiameter)
					&& (uint32_t)(queryMaxZ - cell.boundsMinZ) < (uint32_t)(cell.boundsExtentZ + queryDiameter))
				{
					for (int32_t index = 0; index < cell.meshCount; index++)
						candidateMeshes[candidateCount++] = Collision::g_collisionGridMeshIndices[cell.meshListStart + index];
				}
			}

			for (int32_t candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++)
			{
				int32_t meshIndex = candidateMeshes[candidateIndex];
				Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[meshIndex];
				if ((uint32_t)(queryMaxX - mesh.boundsMin.x) >= (uint32_t)(mesh.boundsExt.x + queryDiameter)
					|| (uint32_t)(queryMaxY - mesh.boundsMin.y) >= (uint32_t)(mesh.boundsExt.y + queryDiameter)
					|| (uint32_t)(queryMaxZ - mesh.boundsMin.z) >= (uint32_t)(mesh.boundsExt.z + queryDiameter) || mesh.typeFlags == 0
					|| (mesh.unk & 0x100) != 0)
					continue;

				int32_t localX = queryMaxX - mesh.origin.x;
				int32_t localY = queryMaxY - mesh.origin.y;
				int32_t localZ = queryMaxZ - mesh.origin.z;
				localX = (localX + ((localX >> 31) & 31)) >> 5;
				localZ = (localZ + ((localZ >> 31) & 31)) >> 5;
				Collision::CollisionTreeGroup* group = reinterpret_cast<Collision::CollisionTreeGroup*>(mesh.collisionTree);
				while (group->marker >= 0)
				{
					int32_t faceCount = group->faceCount;
					Collision::PackedCollisionFace* face = reinterpret_cast<Collision::PackedCollisionFace*>(group + 1);
					if ((uint32_t)(localX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + faceTolerance)
						&& (uint32_t)(localZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + faceTolerance))
					{
						for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
						{
							int32_t scaledY = (localY + ((localY >> 31) & 31)) >> 5;
							if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + faceTolerance)
								&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z) < (uint32_t)(face->boundsExtentZBlock * 64 + faceTolerance)
								&& (uint32_t)(scaledY - face->boundsMinYBlock * 64 - face->vertex0.y)
									< (uint32_t)(face->boundsExtentYBlock * 64 + faceTolerance)
								&& Collision::g_collisionTriangleCount < 240)
							{
								int32_t triangleIndex = Collision::g_collisionTriangleCount++;
								Collision::g_collisionTriangles[triangleIndex] = face;
								Collision::g_collisionTriangleMeshIndices[triangleIndex] = (int16_t)meshIndex;
							}
						}
					}
					else
						face += faceCount;
					group = reinterpret_cast<Collision::CollisionTreeGroup*>(face);
				}
			}

			int32_t staticCandidateCount = candidateCount;
			if (queryIndex == 0)
			{
				Collision::CollisionGridCell& movingCell = Collision::g_collisionGrid[256];
				for (int32_t index = 0; index < movingCell.meshCount; index++)
					candidateMeshes[candidateCount++] = Collision::g_collisionGridMeshIndices[movingCell.meshListStart + index];

				for (int32_t candidateIndex = staticCandidateCount; candidateIndex < candidateCount; candidateIndex++)
				{
					int32_t meshIndex = candidateMeshes[candidateIndex];
					Collision::CollisionMeshInstance& mesh = Collision::g_collisionMeshInstances[meshIndex];
					Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
					int32_t platformRadius = collisionRadius + 0x400 + abs(platform.velocity.x) + abs(platform.velocity.y) + abs(platform.velocity.z);
					queryMaxX = buzz->posAngles.pos.x + platformRadius;
					queryMaxY = buzz->posAngles.pos.y;
					queryMaxZ = buzz->posAngles.pos.z + platformRadius;
					queryDiameter = platformRadius * 2;
					faceTolerance = (platformRadius + 15 + (((platformRadius + 15) >> 31) & 15)) >> 4;

					int32_t transformedX = queryMaxX;
					int32_t transformedY = queryMaxY;
					int32_t transformedZ = queryMaxZ;
					if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
					{
						Vector3I16 angles = { (int16_t)(platform.rotationAnglesFixed.x >> 2),
							(int16_t)(platform.rotationAnglesFixed.y >> 2),
							(int16_t)(platform.rotationAnglesFixed.z >> 2) };
						Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);
						int32_t deltaX = (buzz->posAngles.pos.x - mesh.origin.x) >> 5;
						int32_t deltaY = (buzz->posAngles.pos.y - mesh.origin.y) >> 5;
						int32_t deltaZ = (buzz->posAngles.pos.z - mesh.origin.z) >> 5;
						int32_t transformed = Animation::g_keyframeRotation.matrix.m00 * deltaX + Animation::g_keyframeRotation.matrix.m10 * deltaY
							+ Animation::g_keyframeRotation.matrix.m20 * deltaZ;
						transformedX = ((transformed + ((transformed >> 31) & 0x7F)) >> 7) + mesh.origin.x + platformRadius;
						transformed = Animation::g_keyframeRotation.matrix.m01 * deltaX + Animation::g_keyframeRotation.matrix.m11 * deltaY
							+ Animation::g_keyframeRotation.matrix.m21 * deltaZ;
						transformedY = ((transformed + ((transformed >> 31) & 0x7F)) >> 7) + mesh.origin.y;
						transformed = Animation::g_keyframeRotation.matrix.m02 * deltaX + Animation::g_keyframeRotation.matrix.m12 * deltaY
							+ Animation::g_keyframeRotation.matrix.m22 * deltaZ;
						transformedZ = ((transformed + ((transformed >> 31) & 0x7F)) >> 7) + mesh.origin.z + platformRadius;
					}
					transformedY += platformRadius;

					if ((uint32_t)(transformedX - mesh.boundsMin.x) < (uint32_t)(mesh.boundsExt.x + queryDiameter)
						&& (uint32_t)(transformedZ - mesh.boundsMin.z) < (uint32_t)(mesh.boundsExt.z + queryDiameter))
					{
						Platform::g_contactPlatformIndices[Platform::g_contactPlatformCount++] = (int16_t)mesh.platformIdx;
						if (mesh.typeFlags != 0 && (mesh.unk & 0x100) == 0)
						{
							int32_t localX = transformedX - mesh.origin.x;
							int32_t localY = transformedY - mesh.origin.y;
							int32_t localZ = transformedZ - mesh.origin.z;
							localX = (localX + ((localX >> 31) & 31)) >> 5;
							localZ = (localZ + ((localZ >> 31) & 31)) >> 5;
							Collision::CollisionTreeGroup* group = reinterpret_cast<Collision::CollisionTreeGroup*>(mesh.collisionTree);
							while (group->marker >= 0)
							{
								int32_t faceCount = group->faceCount;
								Collision::PackedCollisionFace* face = reinterpret_cast<Collision::PackedCollisionFace*>(group + 1);
								if ((uint32_t)(localX - group->boundsMinX) < (uint32_t)(group->boundsExtentX + faceTolerance)
									&& (uint32_t)(localZ - group->boundsMinZ) < (uint32_t)(group->boundsExtentZ + faceTolerance))
								{
									for (int32_t faceIndex = 0; faceIndex < faceCount; faceIndex++, face++)
									{
										int32_t scaledY = (localY + ((localY >> 31) & 31)) >> 5;
										if ((uint32_t)(localX - face->boundsMinX) < (uint32_t)(face->boundsExtentX + faceTolerance)
											&& (uint32_t)(localZ - face->boundsMinZBlock * 64 - face->vertex0.z)
												< (uint32_t)(face->boundsExtentZBlock * 64 + faceTolerance)
											&& (uint32_t)(scaledY - face->boundsMinYBlock * 64 - face->vertex0.y)
												< (uint32_t)(face->boundsExtentYBlock * 64 + faceTolerance))
										{
											if (Collision::g_collisionTriangleCount < 240)
											{
												int32_t triangleIndex = Collision::g_collisionTriangleCount++;
												Collision::g_collisionTriangles[triangleIndex] = face;
												Collision::g_collisionTriangleMeshIndices[triangleIndex] = (int16_t)meshIndex;
											}
											if ((platform.flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
											{
												int32_t rotatedIndex = Collision::g_rotatedCollisionTriangleCount++;
												Collision::g_rotatedCollisionTriangles[rotatedIndex] = face;
												Collision::g_rotatedCollisionTriangleMeshIndices[rotatedIndex] = (int16_t)meshIndex;
											}
										}
									}
								}
								else
									face += faceCount;
								group = reinterpret_cast<Collision::CollisionTreeGroup*>(face);
							}
						}
					}
					else
					{
						mesh.origin.x += platform.remainingTranslation.x;
						mesh.origin.y += platform.remainingTranslation.y;
						mesh.origin.z += platform.remainingTranslation.z;
						mesh.boundsMin.x += platform.remainingTranslation.x;
						mesh.boundsMin.z += platform.remainingTranslation.z;
						platform.remainingTranslation.x = 0;
						platform.remainingTranslation.y = 0;
						platform.remainingTranslation.z = 0;
						platform.rotationAnglesFixed.x += platform.remainingRotation.x;
						platform.rotationAnglesFixed.y += platform.remainingRotation.y;
						platform.rotationAnglesFixed.z += platform.remainingRotation.z;
						platform.remainingRotation.x = 0;
						platform.remainingRotation.y = 0;
						platform.remainingRotation.z = 0;
					}
				}
			}

			int32_t minimumX = (queryMaxX - queryDiameter + (((queryMaxX - queryDiameter) >> 31) & 31)) >> 5;
			int32_t minimumZ = (queryMaxZ - queryDiameter + (((queryMaxZ - queryDiameter) >> 31) & 31)) >> 5;
			int32_t maximumX = (queryMaxX + ((queryMaxX >> 31) & 31)) >> 5;
			int32_t maximumZ = (queryMaxZ + ((queryMaxZ >> 31) & 31)) >> 5;
			uint8_t* terrainHead = Terrain::g_terrainRelocationHeads[1];
			Terrain::TerrainEdgeChain* terrainChain =
				terrainHead == 0 ? 0 : reinterpret_cast<Terrain::TerrainEdgeChain*>(terrainHead - offsetof(Terrain::TerrainEdgeChain, edgeCount));
			while (terrainChain != 0)
			{
				int32_t edgeIndex = 0;
				int32_t edgeCount = terrainChain->edgeCount;
				while (edgeIndex < edgeCount)
				{
					Vector3I* vertices = &terrainChain->vertices[edgeIndex];
					int32_t blockEnd = 0;
					if (vertices[0].y != (int32_t)0x80000000 && (uint32_t)(maximumX - vertices[0].y) < (uint32_t)(vertices[1].y + faceTolerance)
						&& (uint32_t)(maximumZ - vertices[2].y) < (uint32_t)(vertices[3].y + faceTolerance))
						blockEnd = edgeIndex + 16;

					if (edgeIndex < blockEnd)
					{
						for (int32_t index = edgeIndex; index < blockEnd; index++, vertices++)
						{
							Vector3I* start = vertices;
							Vector3I* end = vertices + 1;
							if (((minimumX <= start->x && end->x <= maximumX) || (minimumX <= end->x && start->x <= maximumX))
								&& ((minimumZ <= start->z && end->z <= maximumZ) || (minimumZ <= end->z && start->z <= maximumZ))
								&& Collision::g_collisionEdgeVertexCount < 32)
							{
								int32_t outputIndex = Collision::g_collisionEdgeVertexCount;
								Collision::g_collisionEdgeVertices[outputIndex] = *start;
								Collision::g_collisionEdgeVertices[outputIndex + 1] = *end;
								Collision::g_collisionEdgeVertexCount += 2;
							}
						}
					}
					edgeIndex += 16;
				}
				terrainChain = terrainChain->next;
			}

			contactState[0] = 0;
			int32_t triangleCount = Collision::g_collisionTriangleCount;
			result.contactState = 0;
			result.surfaceType = 0xFF;
			bool hadGroundResponse = false;
			if (triangleCount < 1 && Collision::g_collisionEdgeVertexCount < 1)
			{
				Platform::AdvancePartialMotion(100, 0, 0);
				Platform::AdvancePartialMotion(100, 0, 1);
				contactState[1] = 0;
				buzz->posAngles.pos.x += collisionPass == 0 ? movement->lateral : movement->lateral / 2;
				buzz->posAngles.pos.y += collisionPass == 0 ? movement->vertical : movement->vertical / 2;
				buzz->posAngles.pos.z += collisionPass == 0 ? movement->forward : movement->forward / 2;
				result.movement.x = 0;
				result.movement.y = 0;
				result.movement.z = 0;
			}
			else
			{
				Vector3I position = buzz->posAngles.pos;
				Vector3I velocity = { movement->lateral, movement->vertical, movement->forward };
				Collision::g_hadGroundResponse = 0;
				Collision::CollisionStepMotion stepMotion;
				stepMotion.movement.x = (int16_t)movement->lateral;
				stepMotion.movement.y = (int16_t)movement->vertical;
				stepMotion.movement.z = (int16_t)movement->forward;
				if (queryIndex == 0)
					stepMotion.platformMovement = result.movement;
				else
					stepMotion.platformMovement.x = stepMotion.platformMovement.y = stepMotion.platformMovement.z = 0;
				if (collisionPass != 0)
				{
					stepMotion.movement.x /= 2;
					stepMotion.movement.y /= 2;
					stepMotion.movement.z /= 2;
					stepMotion.platformMovement.x /= 2;
					stepMotion.platformMovement.y /= 2;
					stepMotion.platformMovement.z /= 2;
				}

				int16_t remainingSteps = (Collision::g_collisionTriangleCount < 11) + 3;
				int32_t stepResult;
				do
				{
					int32_t footingResult = 0;
					if (contactState[1] == 1)
					{
						footingResult = Collision::ResolvePlatformFooting(&stepMotion, &position, &velocity, &result);
						if (footingResult == 1)
							contactState[0] = 1;
					}
					stepResult = Collision::ResolveSubstep(&stepMotion,
						&position,
						&velocity,
						&result,
						Collision::g_collisionTriangleMeshIndices,
						Collision::g_collisionTriangles,
						Collision::g_collisionTriangleCount,
						1);
					if (stepResult == 2)
					{
						contactState[1] = 0;
						hadGroundResponse = true;
					}
					else if (footingResult == 1 || stepResult == 1)
					{
						contactState[0] = 1;
						contactState[1] = 1;
					}
					else
						contactState[1] = 0;
					remainingSteps--;
				} while (remainingSteps > 0 && stepResult > 0);

				buzz->posAngles.pos = position;
				movement->lateral = velocity.x;
				movement->vertical = velocity.y;
				movement->forward = velocity.z;
				if (queryIndex == 0)
				{
					value = stepMotion.platformMovement.x * 3;
					result.movement.x = (int16_t)((value + ((value >> 31) & 3)) >> 2);
					value = stepMotion.platformMovement.y * 3;
					result.movement.y = (int16_t)((value + ((value >> 31) & 3)) >> 2);
					value = stepMotion.platformMovement.z * 3;
					result.movement.z = (int16_t)((value + ((value >> 31) & 3)) >> 2);
				}
			}

			buzz->posAngles.pos.y += result.collisionDistance + 0xC0;
			if (buzz->posAngles.pos.x == g_buzzActor.motionTargetPos.x && buzz->posAngles.pos.y == g_buzzActor.motionTargetPos.y
				&& buzz->posAngles.pos.z == g_buzzActor.motionTargetPos.z && hadGroundResponse)
				result.contactTimer += Renderer::g_frameDelta;
			else
				result.contactTimer = 0;
			if (result.contactTimer > 20)
			{
				result.contactTimer = 20;
				contactState[0] = 1;
			}
			result.surfaceVelocity.x = 0;
			result.surfaceVelocity.y = 0;
			result.surfaceVelocity.z = 0;
		}

		// FUNCTION: TOY2 0x004855F0 [MATCHED]
		void HandleCollisions(Toy2BuzzActor* buzz, MovementVelocity* movement, uint8_t* contactState, int32_t queryIndex)
		{
			if (movement->lateral * movement->lateral + movement->vertical * movement->vertical + movement->forward * movement->forward > 0x400000)
			{
				ResolveCollisions(buzz, movement, contactState, queryIndex, 1);
				uint8_t firstPassContacts = contactState[0] | contactState[1];
				ResolveCollisions(buzz, movement, contactState, queryIndex, 2);
				contactState[1] |= firstPassContacts;
			}
			else
			{
				ResolveCollisions(buzz, movement, contactState, queryIndex, 0);
			}
		}
	}
}

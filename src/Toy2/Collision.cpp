#include "Toy2/Collision.h"
#include "Nu3D/Link.h"
#include "Renderer/Shadows.h"
#include "Toy2/Buzz.h"

namespace Toy2
{
	namespace Collision
	{
		// GLOBAL: TOY2 0x00729178
		CollisionQueryResult g_collisionQueryResults[2];

		// GLOBAL: TOY2 0x007295A8
		CollisionMeshInstance g_collisionMeshInstances[300];

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

		// GLOBAL: TOY2 0x00554FA0
		MathScratchVector g_mathScratch[64];

		// STUB: TOY2 0x00489C30
		void BuildCollisionWorld(int32_t level, uint8_t** buffer, int32_t terrainNum) {}

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
		// FUNCTION: TOY2 0x00481140
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
		// FUNCTION: TOY2 0x00485680
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

		// FUNCTION: TOY2 0x004857E0
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

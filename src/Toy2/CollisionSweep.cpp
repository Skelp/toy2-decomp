#include "Toy2/Collision.h"
#include "Toy2/CollisionInternal.h"
#include "Toy2/Animation.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Nu3D/Math.h"
#include "Renderer/Renderer.h"
#include <math.h>

// The swept collision solver. SweepEdge, SweepVertex and SweepTriangle test one
// motion against one face of the collision mesh, IsPointInTriangle is the
// containment test they share, and ResolveSubstep and ResolvePlatformFooting turn
// the nearest hit into a corrected motion.
//
// Retail run 0x00481140-0x00483EF0, between the Nu3D/Math.cpp and Toy2/Buzz.cpp
// runs.

namespace Toy2
{
	namespace Collision
	{
		// GLOBAL: TOY2 0x00729170
		int32_t g_lastCollisionEndDistance;

		// GLOBAL: TOY2 0x0072959C
		int32_t g_lastCollisionStartDistance;

		// GLOBAL: TOY2 0x0072D298
		int32_t g_hadGroundResponse;

		static __forceinline int32_t RotateTransposeX(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{ return ShiftTowardZero(matrix.m00 * x + matrix.m10 * y + matrix.m20 * z, 12); }

		static __forceinline int32_t RotateTransposeY(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{ return ShiftTowardZero(matrix.m01 * x + matrix.m11 * y + matrix.m21 * z, 12); }

		static __forceinline int32_t RotateTransposeZ(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{ return ShiftTowardZero(matrix.m02 * x + matrix.m12 * y + matrix.m22 * z, 12); }

		static __forceinline int32_t RotateX(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{ return ShiftTowardZero(matrix.m00 * x + matrix.m01 * y + matrix.m02 * z, 12); }

		static __forceinline int32_t RotateY(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{ return ShiftTowardZero(matrix.m10 * x + matrix.m11 * y + matrix.m12 * z, 12); }

		static __forceinline int32_t RotateZ(const Matrix3x3I16& matrix, int32_t x, int32_t y, int32_t z)
		{ return ShiftTowardZero(matrix.m20 * x + matrix.m21 * y + matrix.m22 * z, 12); }

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

	}
}
namespace Nu3D
{
	namespace Collision
	{}
}
namespace Toy2
{
	namespace Collision
	{
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

			g_mathScratch[1] = sweep->movement;
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
				int32_t edgeProjection = planeNormal.x * edgeX + planeNormal.y * edgeY + planeNormal.z * edgeZ;
				int32_t edgeLengthSquared = edgeX * edgeX + edgeY * edgeY + edgeZ * edgeZ;
				int32_t scaledEdgeLengthSquared = edgeLengthSquared;
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

				int32_t contactLengthSquared = edgeLengthSquared;
				while (alongEdge > 0x40000)
				{
					alongEdge >>= 1;
					contactLengthSquared >>= 1;
				}
				contactPoint.x -= alongEdge * edgeX / contactLengthSquared;
				contactPoint.y -= alongEdge * edgeY / contactLengthSquared;
				contactPoint.z -= alongEdge * edgeZ / contactLengthSquared;
				Nu3D::Math::NormalizeToFixedPoint(&contactPoint, &contactPoint);
				contactPoint.x *= 4;
				contactPoint.y *= 4;
				contactPoint.z *= 4;

				int32_t accepted = false;
				if (adjacentNormals[0].y != 0x7FFF
					&& adjacentNormals[0].x * hitNormal.x + adjacentNormals[0].y * hitNormal.y + adjacentNormals[0].z * hitNormal.z > -0x80000)
				{
					accepted = true;
				}
				if ((adjacentNormals[1].y != 0x7FFF
						&& adjacentNormals[1].x * hitNormal.x + adjacentNormals[1].y * hitNormal.y + adjacentNormals[1].z * hitNormal.z > -0x80000)
					|| accepted)
				{
					sweep->startDistance = startDistance;
					sweep->endDistance = endDistance;
					sweep->nearestFraction = fraction;
					sweep->hitNormal.direction.x = (int16_t)hitNormal.x;
					sweep->hitNormal.direction.y = (int16_t)hitNormal.y;
					sweep->hitNormal.direction.z = (int16_t)hitNormal.z;
					return 1;
				}
				return 0;
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
			int32_t movementLength = sweep->movement.scalar;
			if (hitDistance < 0 || hitDistance >= movementLength)
				return 0;

			int32_t fraction = hitDistance * 0x4000 / movementLength;
			if (fraction >= sweep->nearestFraction)
				return 0;

			crossOffset.x = start->x + (end->x - start->x) * hitDistance / movementLength;
			crossOffset.y = start->y + (end->y - start->y) * hitDistance / movementLength;
			crossOffset.z = start->z + (end->z - start->z) * hitDistance / movementLength;
			int32_t alongEdge = crossOffset.x * edgeX + crossOffset.y * edgeY + crossOffset.z * edgeZ;
			if (alongEdge < 0)
				return 0;
			int32_t edgeLengthSquared = edgeX * edgeX + edgeY * edgeY + edgeZ * edgeZ;
			if ((alongEdge >> 5) > edgeLengthSquared)
				return 0;

			int32_t scaledEdgeLengthSquared = edgeLengthSquared;
			while (alongEdge > 0x40000)
			{
				alongEdge >>= 1;
				scaledEdgeLengthSquared >>= 1;
			}
			hitNormal.x = crossOffset.x - alongEdge * edgeX / scaledEdgeLengthSquared;
			hitNormal.y = crossOffset.y - alongEdge * edgeY / scaledEdgeLengthSquared;
			hitNormal.z = crossOffset.z - alongEdge * edgeZ / scaledEdgeLengthSquared;
			Nu3D::Math::NormalizeToFixedPoint(&hitNormal, &hitNormal);
			hitNormal.x *= 4;
			hitNormal.y *= 4;
			hitNormal.z *= 4;

			int32_t accepted = false;
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
				sweep->endDistance = hitDistance - movementLength;
				sweep->nearestFraction = fraction;
				sweep->hitNormal.direction.x = (int16_t)hitNormal.x;
				sweep->hitNormal.direction.y = (int16_t)hitNormal.y;
				sweep->hitNormal.direction.z = (int16_t)hitNormal.z;
				return 1;
			}
			return 0;
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

					sweep.movement.value.x = sweep.end.x - sweep.start.x;
					sweep.movement.value.y = sweep.end.y - sweep.start.y;
					sweep.movement.value.z = sweep.end.z - sweep.start.z;
					while (abs(sweep.movement.value.x) > 0x4000 || abs(sweep.movement.value.y) > 0x4000 || abs(sweep.movement.value.z) > 0x4000)
					{
						sweep.movement.value.x >>= 1;
						sweep.movement.value.y >>= 1;
						sweep.movement.value.z >>= 1;
					}
					Nu3D::Math::NormalizeToFixedPoint(&sweep.movement.value, &sweep.movement.value);
					sweep.movement.scalar =
						(((sweep.end.x - sweep.start.x) * sweep.movement.value.x + (sweep.end.y - sweep.start.y) * sweep.movement.value.y
							 + (sweep.end.z - sweep.start.z) * sweep.movement.value.z)
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
					normal.x = (int16_t)RotateX(
						Animation::g_keyframeRotation.matrix, sweep.hitNormal.direction.x, sweep.hitNormal.direction.y, sweep.hitNormal.direction.z);
					normal.y = (int16_t)RotateY(
						Animation::g_keyframeRotation.matrix, sweep.hitNormal.direction.x, sweep.hitNormal.direction.y, sweep.hitNormal.direction.z);
					normal.z = (int16_t)RotateZ(
						Animation::g_keyframeRotation.matrix, sweep.hitNormal.direction.x, sweep.hitNormal.direction.y, sweep.hitNormal.direction.z);
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
		int32_t ResolvePlatformFooting(CollisionStepMotion* motion, Vector3I* position, Vector3I*, SurfaceCollisionResult* result)
		{
			Platform::CollisionFace* contactFace = result->face;
			int16_t meshIndex = (int16_t)(result->contactFlags & COLLISION_CONTACT_MESH_INDEX_MASK);
			CollisionMeshInstance& mesh = g_collisionMeshInstances[meshIndex];
			uint8_t surfaceType = (uint8_t)mesh.unk;
			if (surfaceType != 0xFF)
				result->surfaceType ^= (uint8_t)result->surfaceType ^ surfaceType;

			Vector3I16 primaryNormal;
			Vector3I16 secondaryNormal;
			if (mesh.typeFlags == COLLISION_MESH_MOVING && (Platform::g_platformStates[mesh.platformIdx].flags & Platform::PLATFORM_FLAG_ROTATED) != 0)
			{
				Platform::PlatformState& platform = Platform::g_platformStates[mesh.platformIdx];
				Vector3I16 angles;
				angles.x = platform.rotationAnglesFixed.x >> 2;
				angles.y = platform.rotationAnglesFixed.y >> 2;
				angles.z = platform.rotationAnglesFixed.z >> 2;
				Nu3D::Math::SetRotationXYZ(&angles, &Animation::g_keyframeRotation.matrix);

				primaryNormal.x = (int16_t)RotateX(Animation::g_keyframeRotation.matrix, contactFace->normal.x, contactFace->normal.y, contactFace->normal.z);
				primaryNormal.y = (int16_t)RotateY(Animation::g_keyframeRotation.matrix, contactFace->normal.x, contactFace->normal.y, contactFace->normal.z);
				primaryNormal.z = (int16_t)RotateZ(Animation::g_keyframeRotation.matrix, contactFace->normal.x, contactFace->normal.y, contactFace->normal.z);

				if (contactFace->secondaryNormal.y != 0x7FFF)
				{
					secondaryNormal.x = (int16_t)RotateX(
						Animation::g_keyframeRotation.matrix, contactFace->secondaryNormal.x, contactFace->secondaryNormal.y, contactFace->secondaryNormal.z);
					secondaryNormal.y = (int16_t)RotateY(
						Animation::g_keyframeRotation.matrix, contactFace->secondaryNormal.x, contactFace->secondaryNormal.y, contactFace->secondaryNormal.z);
					secondaryNormal.z = (int16_t)RotateZ(
						Animation::g_keyframeRotation.matrix, contactFace->secondaryNormal.x, contactFace->secondaryNormal.y, contactFace->secondaryNormal.z);
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

			const Vector3I16& contactNormal = (result->contactFlags & COLLISION_CONTACT_SECONDARY_FACE) != 0 ? secondaryNormal : primaryNormal;
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
				const Vector3I16& resolvedNormal = (stepResult.contactFlags & COLLISION_CONTACT_SECONDARY_FACE) != 0 ? secondaryNormal : primaryNormal;
				motion->movement.x = (int16_t)((resolvedNormal.x >> 9) - (int16_t)position->x - motion->platformMovement.x + (int16_t)stepPosition.x);
				motion->movement.y = (int16_t)((resolvedNormal.y >> 8) - (int16_t)position->y - motion->platformMovement.y + (int16_t)stepPosition.y);
				motion->movement.z = (int16_t)((resolvedNormal.z >> 9) - (int16_t)position->z - motion->platformMovement.z + (int16_t)stepPosition.z);
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

	}
}

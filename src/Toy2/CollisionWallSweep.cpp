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

// The wall sweep of the collision world: retail holds 0x00480660 as one object.
namespace Toy2
{
	namespace Collision
	{
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
					int32_t endDistance = (((startDeltaZ + movement->z) * normal.z + (startDeltaX + movement->x) * normal.x) >> 12) - radius;
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
				if (edgeOffsetX * edgeOffsetX + edgeOffsetZ * edgeOffsetZ < movementX * movementX + movementZ * movementZ + expandedRadius * expandedRadius)
				{
					Vector3I16 direction = { (int16_t)movement->x, 0, (int16_t)movement->z };
					Nu3D::Math::NormalizeToFixedPoint16(&direction, &direction);

					int32_t edgeStartX = edgeStart.x * 0x20;
					int32_t edgeStartZ = edgeStart.z * 0x20;
					int32_t projectedDistance = ((edgeStartZ - start->z) * direction.z + (edgeStartX - start->x) * direction.x) >> 12;
					if (projectedDistance >= 0)
					{
						int32_t closestX = edgeStartX - (start->x + (direction.x * projectedDistance >> 12));
						int32_t closestZ = edgeStartZ - (start->z + (direction.z * projectedDistance >> 12));
						int32_t closestDistanceSquared = closestX * closestX + closestZ * closestZ;
						if (closestDistanceSquared <= (radius + 0x20) * (radius + 0x20))
						{
							int32_t hitDistance = projectedDistance - (int32_t)sqrt((double)((radius + 0x40) * (radius + 0x40) - closestDistanceSquared));
							int32_t movementLengthSquared = movement->x * movement->x + movement->z * movement->z;
							if (hitDistance >= -0x20 && hitDistance * hitDistance <= movementLengthSquared)
							{
								if (hitDistance < 0)
									hitDistance = 0;

								int32_t movementLength = (int32_t)sqrt((double)movementLengthSquared);
								int32_t fraction = (hitDistance << 14) / movementLength;
								if (fraction < sweep->nearestFraction)
								{
									normal.x = (int16_t)(direction.x * hitDistance >> 12) - (int16_t)edgeStart.x * 0x20 + (int16_t)start->x;
									normal.z = (int16_t)(direction.z * hitDistance >> 12) - (int16_t)edgeStart.z * 0x20 + (int16_t)start->z;
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
	}
}

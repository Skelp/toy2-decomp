#include "Toy2/Camera.h"
#include "Nu3D/Math.h"

namespace Toy2
{
	namespace Camera
	{
		// FUNCTION: TOY2 0x00403640
		void SmoothToTarget(GameplayCamera* camera)
		{
			camera->pos.x += (camera->target.x - camera->pos.x) >> 3;
			camera->pos.y += (camera->target.y - camera->pos.y) >> 3;
			camera->pos.z += (camera->target.z - camera->pos.z) >> 3;

			int32_t deltaX = (camera->lookAt.x - camera->target.x) >> 5;
			int32_t deltaY = (camera->lookAt.y - camera->target.y) >> 5;
			int32_t deltaZ = (camera->lookAt.z - camera->target.z) >> 5;

			int32_t yawAngle = Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ);
			int32_t yawDelta = (yawAngle - camera->angles.yaw) & 0xfff;
			if (yawDelta >= 0x800)
				yawDelta -= 0x1000;
			camera->angles.yaw = (uint16_t)(((yawDelta >> 2) + camera->angles.yaw) & 0xfff);

			int32_t horizSq;
			int32_t heightSq;
			if (deltaY < 0)
			{
				horizSq = deltaZ * deltaZ + deltaX * deltaX;
				heightSq = deltaY * deltaY;
			}
			else
			{
				horizSq = deltaZ * deltaZ + deltaX * deltaX;
				heightSq = -(deltaY * deltaY);
			}

			int32_t pitchAngle = Nu3D::Math::CartesianToFixedAngle(heightSq, horizSq);
			pitchAngle = -pitchAngle;
			int32_t pitchDelta = (pitchAngle - camera->angles.pitch) & 0xfff;
			if (pitchDelta >= 0x800)
				pitchDelta -= 0x1000;
			camera->angles.pitch = (uint16_t)(((pitchDelta >> 2) + camera->angles.pitch) & 0xfff);
		}
	}
} // namespace Toy2

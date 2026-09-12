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

// The tilt physics of a platform: retail holds 0x0049EC00 as one object.
namespace Toy2
{
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
	}
}

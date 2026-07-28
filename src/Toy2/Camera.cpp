#include "Toy2/Camera.h"
#include "InputManager.h"
#include "Nu3D/Math.h"
#include "Toy2/Toy2.h"
#include <math.h>

namespace Toy2
{
	namespace Camera
	{
		// GLOBAL: TOY2 0x0052F3A0
		GameplayCamera g_gameplayCamera;

		// GLOBAL: TOY2 0x0050A13C
		int32_t g_scriptedCameraState;

		// STUB: TOY2 0x00403450
		void InitGameplayCamera(GameplayCamera* camera, Buzz::Toy2BuzzActor* buzz) {}

		// STUB: TOY2 0x004020F0
		void BeginScriptedCutsceneAtPoint(Vector3I* focusPosition, int32_t duration, int32_t cameraDistance) {}

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

namespace Camera
{
	// GLOBAL: TOY2 0x004F5AB4
	int16_t g_analogDirectionAngles[9] = { 0x000, 0x200, 0x400, 0x600, 0x800, 0xA00, 0xC00, 0xE00, 0x1000 };

	// GLOBAL: TOY2 0x004F5AC8
	int16_t g_digitalDirectionAngles[16] = {
		0x000,
		0x000,
		0x400,
		0x200,
		0x800,
		0x000,
		0x600,
		0x000,
		0xC00,
		0xE00,
		0x000,
		0x000,
		0xA00,
		0x000,
		0x000,
		0x000,
	};

	// FUNCTION: TOY2 0x00433F40
	int32_t CalculateMaxTurnAngle(uint16_t directionInputState)
	{
		int32_t inputMagnitude = 0;
		int32_t cameraRelativeAngle = Nu3D::Math::CartesianToFixedAngle((Toy2::g_buzzActor.posAngles.pos.x - Toy2::Camera::g_gameplayCamera.lookAt.x) >> 5,
			(Toy2::g_buzzActor.posAngles.pos.z - Toy2::Camera::g_gameplayCamera.lookAt.z) >> 5);

		if ((InputManager::g_directionalInputCount == 1 || InputManager::g_directionalInputCount == 2) && Toy2::g_demoMode == 0)
		{
			if (abs(InputManager::g_analogInputX) < 0x1800)
			{
				InputManager::g_analogInputX = 0;
			}
			if (abs(InputManager::g_analogInputY) < 0x1800)
			{
				InputManager::g_analogInputY = 0;
			}

			int32_t analogAngle = Nu3D::Math::CartesianToFixedAngle(InputManager::g_analogInputX, InputManager::g_analogInputY) & 0xFFF;
			int32_t octant = analogAngle >> 9;
			int32_t octantFraction = analogAngle & 0x1FF;
			int32_t inputAngle =
				((g_analogDirectionAngles[octant + 1] - g_analogDirectionAngles[octant]) * octantFraction >> 9) + g_analogDirectionAngles[octant];
			Toy2::g_buzzActor.facingAngle = (int16_t)((inputAngle + cameraRelativeAngle) & 0xFFF);

			int32_t halfInputY = InputManager::g_analogInputY / 2;
			int32_t halfInputX = InputManager::g_analogInputX / 2;
			inputMagnitude = (int32_t)sqrt((double)(halfInputX * halfInputX + halfInputY * halfInputY));
			if (inputMagnitude > 0x4000)
			{
				inputMagnitude = 0x4000;
			}
		}

		if (InputManager::g_directionalInputCount == 0 || Toy2::g_demoMode != 0)
		{
			Toy2::g_buzzActor.facingAngle = (int16_t)((g_digitalDirectionAngles[(directionInputState >> 4) & 0xF] + cameraRelativeAngle) & 0xFFF);
			return 0x4000;
		}
		return inputMagnitude;
	}
}

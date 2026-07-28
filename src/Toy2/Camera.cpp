#include "Toy2/Camera.h"
#include "InputManager.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Toy2/Toy2.h"
#include <math.h>

namespace Toy2
{
	namespace Camera
	{
		// GLOBAL: TOY2 0x0052F3A0
		GameplayCamera g_gameplayCamera;

		// GLOBAL: TOY2 0x0052ADC0
		Nu3D::Camera::ActiveCameraTransform g_renderCameraTransform;

		// GLOBAL: TOY2 0x0050A13C
		int32_t g_scriptedCameraState;

		// GLOBAL: TOY2 0x0050A140
		int32_t g_cutsceneInputLockTimer;

		// GLOBAL: TOY2 0x0050A1F4
		int32_t g_cutsceneDuration;

		// GLOBAL: TOY2 0x0050A500
		Vector3I g_cutsceneFocusPosition;

		// GLOBAL: TOY2 0x0050A520
		Vector3I g_cutsceneCameraPosition;

		// GLOBAL: TOY2 0x0052B7E8
		GameplayCamera g_cutsceneCamera;

		// GLOBAL: TOY2 0x0050A510
		int32_t g_shakeTimer;

		// GLOBAL: TOY2 0x0050A538
		Nu3D::Particles::ParticleInstance* g_cameraMarkerParticle;

		// GLOBAL: TOY2 0x0050A4DC
		Nu3D::Particles::ParticleInstance* g_targetMarkerParticle;

		// STUB: TOY2 0x00403450
		void InitGameplayCamera(GameplayCamera* camera, Buzz::Toy2BuzzActor* buzz) {}

		// FUNCTION: TOY2 0x00402030
		void InitCutsceneCamera(const Vector3I* focusPosition, const Vector3I* cameraPosition)
		{
			g_cutsceneCamera.pos.x = cameraPosition->x;
			g_cutsceneCamera.pos.y = cameraPosition->y;
			g_cutsceneCamera.pos.z = cameraPosition->z;
			g_cutsceneCamera.lookAt.x = focusPosition->x;
			g_cutsceneCamera.lookAt.y = focusPosition->y;
			g_cutsceneCamera.lookAt.z = focusPosition->z;

			int32_t deltaX = (g_cutsceneCamera.lookAt.x - g_cutsceneCamera.pos.x) >> 5;
			int32_t deltaY = (g_cutsceneCamera.lookAt.y - g_cutsceneCamera.pos.y) >> 5;
			int32_t deltaZ = (g_cutsceneCamera.lookAt.z - g_cutsceneCamera.pos.z) >> 5;

			g_cutsceneCamera.target.x = g_cutsceneCamera.pos.x;
			g_cutsceneCamera.target.y = g_cutsceneCamera.pos.y;
			g_cutsceneCamera.target.z = g_cutsceneCamera.pos.z;
			g_cutsceneCamera.angles.yaw = (uint16_t)Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ);
			int32_t horizontalDistanceSq = deltaZ * deltaZ + deltaX * deltaX;
			int32_t heightSq = deltaY < 0 ? deltaY * deltaY : -(deltaY * deltaY);
			g_cutsceneCamera.angles.pitch = (uint16_t)-Nu3D::Math::CartesianToFixedAngle(heightSq, horizontalDistanceSq);
			g_cutsceneCamera.roll = 0;
		}

		// FUNCTION: TOY2 0x004020F0 [MATCHED]
		void BeginScriptedCutsceneAtPoint(Vector3I* focusPosition, int32_t duration, int32_t cameraDistance)
		{
			if (g_scriptedCameraState != 0)
			{
				if (g_cameraMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
				{
					g_cameraMarkerParticle->lifetime = 1;
					g_cameraMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
				}
				if (g_targetMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
				{
					g_targetMarkerParticle->lifetime = 1;
					g_targetMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
				}

				g_buzzActor.actorFlags |= 1;
				g_gameplayCamera.roll = g_buzzActor.posAngles.angles.yaw;
				g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
				g_gameplayCamera.pos.y = g_buzzActor.posAngles.pos.y - 0x3000;
				g_gameplayCamera.angles.yaw = 0x4B0;
				g_gameplayCamera.target.visorAimAngles.pitch = 0;
				g_gameplayCamera.lookAt.x = g_gameplayCamera.pos.x;
				g_gameplayCamera.data[3] = 0;
				g_scriptedCameraState = 0;

				Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
			}

			g_gameplayStateFlags |= 1;
			g_buzzActor.actorFlags |= 4;
			InputManager::g_directionInputState &= 0x309;
			g_cutsceneInputLockTimer = 0x40;
			g_cutsceneFocusPosition.x = focusPosition->x;
			g_cutsceneFocusPosition.y = focusPosition->y;
			g_cutsceneFocusPosition.z = focusPosition->z;

			int32_t yaw =
				Nu3D::Math::CartesianToFixedAngle(
					g_cutsceneFocusPosition.x - g_buzzActor.posAngles.pos.x, g_cutsceneFocusPosition.z - g_buzzActor.posAngles.pos.z)
				& 0xFFF;
			g_cutsceneCameraPosition.x = (Numerics::g_sinCosLUT[(yaw - 0x800) & 0xFFF] >> 4) * cameraDistance + g_cutsceneFocusPosition.x;
			g_cutsceneCameraPosition.y = g_cutsceneFocusPosition.y;
			g_cutsceneCameraPosition.z = (Numerics::g_sinCosLUT[(yaw - 0x400) & 0xFFF] >> 4) * cameraDistance + g_cutsceneFocusPosition.z;
			g_cutsceneDuration = duration;
			InitCutsceneCamera(&g_cutsceneFocusPosition, &g_cutsceneCameraPosition);
		}

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

		// FUNCTION: TOY2 0x00403730 [MATCHED]
		void SnapBehindBuzz(GameplayCamera* camera)
		{
			if (g_cameraMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
			{
				g_cameraMarkerParticle->lifetime = 1;
				g_cameraMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
			}
			if (g_targetMarkerParticle != (Nu3D::Particles::ParticleInstance*)-1)
			{
				g_targetMarkerParticle->lifetime = 1;
				g_targetMarkerParticle = (Nu3D::Particles::ParticleInstance*)-1;
			}

			camera->roll = g_buzzActor.posAngles.angles.yaw;
			g_buzzActor.facingAngle = g_buzzActor.posAngles.angles.yaw;
			camera->angles.yaw = 0x4B0;
			camera->pos.y = g_buzzActor.posAngles.pos.y - 0x3000;
			camera->target.visorAimAngles.pitch = 0;
			camera->lookAt.x = camera->pos.x;
			camera->data[3] = 0;
			g_buzzActor.actorFlags |= 1;
			g_scriptedCameraState = 0;

			Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
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

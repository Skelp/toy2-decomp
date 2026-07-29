#pragma once

#include "Numerics.h"
#include "Toy2/Buzz.h"

namespace Nu3D
{
	namespace Camera
	{
		struct ActiveCameraTransform;
	}

	namespace Particles
	{
		struct ParticleInstance;
	}
}

namespace Toy2
{
	namespace Camera
	{
		// Gameplay camera controller state. Fixed-point positions and 12-bit
		// angles drive the view transform that UpdateActiveTransform blends
		// into the render camera. Fields beyond the angles are confirmed by
		// InitGameplayCamera zeroing the full 0x34-byte struct; their roles
		// are reconstructed as the cluster is recovered.
		struct GameplayCamera
		{
			struct TargetPosition
			{
				int32_t x;
				int32_t y;
				union
				{
					int32_t z;
					Angles visorAimAngles;
				};
			};

			union
			{
				struct
				{
					Vector3I pos; // 0x00 — current camera position (fixed-point)
					Vector3I lookAt; // 0x0C — look-at position (fixed-point)
				};
				PosAndAngles groundProbe;
			};
			TargetPosition target; // 0x18 — position the camera moves toward (fixed-point)
			Angles angles; // 0x24 — pitch and yaw (12-bit fixed-point angles)
			uint16_t roll; // 0x28 — roll angle (12-bit fixed-point)
			union
			{
				uint16_t data[5]; // 0x2A — remaining camera state
				struct
				{
					uint16_t reserved2A;
					uint16_t reserved2C;
					uint16_t reserved2E;
					uint16_t modeTransitionState;
					uint16_t reserved32;
				};
			};
		};

		STATIC_ASSERT(sizeof(GameplayCamera) == 0x34);
		STATIC_ASSERT(sizeof(GameplayCamera::TargetPosition) == 0xC);
		STATIC_ASSERT(offsetof(GameplayCamera, groundProbe) == 0);
		STATIC_ASSERT(offsetof(GameplayCamera, modeTransitionState) == 0x30);

		extern GameplayCamera g_gameplayCamera;
		extern Nu3D::Camera::ActiveCameraTransform g_renderCameraTransform;
		extern int32_t g_scriptedCameraState;
		extern int32_t g_cutsceneDuration;
		extern int32_t g_shakeTimer;
		extern int32_t g_cutsceneRecordType;
		extern const int32_t* g_cutsceneCommandCursor;
		extern int32_t g_cutsceneWaitTimer;
		extern int32_t g_cutsceneSegmentProgress;
		extern int32_t g_cutsceneMoveSpeed;
		extern int32_t g_nextCutsceneMoveSpeed;
		extern int32_t g_cutsceneFocusPathPoint;
		extern int32_t g_cutsceneCameraPathPoint;
		extern int32_t g_cutsceneElapsedTime;
		extern int32_t g_cutsceneSegmentDuration;
		extern Nu3D::Particles::ParticleInstance* g_cameraMarkerParticle;
		extern Nu3D::Particles::ParticleInstance* g_targetMarkerParticle;

		void InitGameplayCamera(GameplayCamera* camera, Buzz::Toy2BuzzActor* buzz);
		void InitCutsceneCamera(const Vector3I* focusPosition, const Vector3I* cameraPosition);
		void BeginScriptedCutsceneAtPoint(Vector3I* focusPosition, int32_t duration, int32_t cameraDistance);
		void SmoothToTarget(GameplayCamera* camera);
		void SnapBehindBuzz(GameplayCamera* camera);
		void CullActors(const Vector3I* cameraPosition);
		int32_t UpdateRocketBoots(Buzz::Toy2BuzzActor* buzz, Buzz::MovementRates* movementRates);
	}
} // namespace Toy2

namespace Camera
{
	int32_t CalculateMaxTurnAngle(uint16_t directionInputState);
}

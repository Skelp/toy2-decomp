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
#pragma pack(push, 2)
		// Gameplay camera controller state. Fixed-point positions and 12-bit
		// angles drive the view transform that UpdateActiveTransform blends
		// into the render camera. Fields beyond the angles are confirmed by
		// InitGameplayCamera zeroing the full 0x34-byte struct; their roles
		// are reconstructed as the cluster is recovered.
		struct CameraCore
		{
			union Position
			{
				struct View
				{
					Vector3I pos;
					Vector3I lookAt;
				} view;
				PosAndAngles groundProbe;
			};

			struct TargetPosition
			{
				int32_t x;
				int32_t y;
				union View
				{
					int32_t z;
					Angles visorAimAngles;
				} view;
			};

			Position position;
			TargetPosition target; // 0x18 — position the camera moves toward (fixed-point)
			Angles angles; // 0x24 — pitch and yaw (12-bit fixed-point angles)
			uint16_t roll; // 0x28 — roll angle (12-bit fixed-point)
		};

		struct CameraState : CameraCore
		{
			uint16_t state;
		};

		struct GameplayCamera : CameraCore
		{
			union State
			{
				uint16_t data[5];
				struct Fields
				{
					uint16_t unused0;
					uint16_t unused1;
					uint16_t orbitPitch;
					uint16_t modeTransitionState;
					uint16_t orbitInputFlags;
				} fields;
			} state;
		};

		STATIC_ASSERT(sizeof(CameraCore) == 0x2A);
		STATIC_ASSERT(sizeof(CameraState) == 0x2C);
		STATIC_ASSERT(sizeof(GameplayCamera) == 0x34);
		STATIC_ASSERT(sizeof(CameraCore::TargetPosition) == 0xC);
		STATIC_ASSERT(offsetof(GameplayCamera, state) == 0x2A);
		STATIC_ASSERT(offsetof(GameplayCamera::State::Fields, modeTransitionState) == 6);
#pragma pack(pop)

		extern GameplayCamera g_gameplayCamera;
		extern CameraState g_cutsceneCamera;
		extern Nu3D::Camera::ActiveCameraTransform g_renderCameraTransform;
		extern int32_t g_scriptedCameraState;
		extern int32_t g_cutsceneInputLockTimer;
		extern int32_t g_cutsceneTransitionTimer;
		extern int32_t g_cutsceneDuration;
		extern Vector3I g_cutsceneFocusPosition;
		extern Vector3I g_cutsceneCameraPosition;
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
		extern Vector3I g_actorCameraTarget;
		extern Nu3D::Particles::ParticleInstance* g_cameraMarkerParticle;
		extern Nu3D::Particles::ParticleInstance* g_targetMarkerParticle;

		void UpdateGravityBoots(Buzz::Toy2BuzzActor* buzz);
		void InitGameplayCamera(GameplayCamera* camera, Buzz::Toy2BuzzActor* buzz);
		void InitCutsceneCamera(const Vector3I* focusPosition, const Vector3I* cameraPosition);
		void BeginScriptedCutsceneAtPoint(Vector3I* focusPosition, int32_t duration, int32_t cameraDistance);
		void SmoothToTarget(CameraState* camera);
		void SnapBehindBuzz(GameplayCamera* camera);
		void GameplayMode(GameplayCamera* camera);
		void VisorMode(GameplayCamera* camera);
		void UpdateActiveTransform();
		void CullActors(const Vector3I* cameraPosition);
		int32_t UpdateRocketBoots(Buzz::Toy2BuzzActor* buzz, Buzz::MovementRates* movementRates);
	}
} // namespace Toy2

namespace Camera
{
	int32_t CalculateMaxTurnAngle(uint16_t directionInputState);
}

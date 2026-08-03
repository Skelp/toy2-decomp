#include "Toy2/Camera.h"
#include "InputManager.h"
#include "CharacterLoader.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Link.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Portal.h"
#include "Nu3D/Viewport.h"
#include "Renderer/Shadows.h"
#include "Toy2/Actor.h"
#include "Toy2/Buzz.h"
#include "Toy2/Collision.h"
#include "Toy2/Toy2.h"
#include <math.h>
#include <string.h>

namespace Toy2
{
	namespace Visor
	{
		// FUNCTION: TOY2 0x004037E0 [MATCHED]
		int32_t GetActorLockOnPoint(Vector3I* lockPoint, Actor::Toy2Actor* actor, int32_t maxDistanceSquared)
		{
			int32_t backwardSine = Numerics::g_sinCosLUT[(actor->yawAngle - 0x800) & 0xFFF] >> 2;
			int32_t cosine = Numerics::g_sinCosLUT[(actor->yawAngle - 0x400) & 0xFFF] >> 2;
			Actor::ActorCollisionVolume* volume = &actor->collisionVolumes[actor->primaryAnimIdx];
			int32_t offsetX = (volume->offset.x * cosine + volume->offset.z * backwardSine) >> 12;
			int32_t offsetZ = (volume->offset.z * cosine - volume->offset.x * backwardSine) >> 12;

			if (maxDistanceSquared == -1)
			{
				lockPoint->x = actor->pos.x + offsetX;
				lockPoint->y = actor->pos.y + volume->offset.y;
				lockPoint->z = actor->pos.z + offsetZ;
			}
			else
			{
				lockPoint->x = (actor->pos.x - Camera::g_gameplayCamera.position.view.pos.x + offsetX) >> 5;
				lockPoint->y = (actor->pos.y - Camera::g_gameplayCamera.position.view.pos.y + volume->offset.y) >> 5;
				lockPoint->z = (actor->pos.z - Camera::g_gameplayCamera.position.view.pos.z + offsetZ) >> 5;
				if (maxDistanceSquared != 0)
				{
					return lockPoint->x * lockPoint->x + lockPoint->y * lockPoint->y + lockPoint->z * lockPoint->z < maxDistanceSquared;
				}
			}

			return false;
		}
	}

	namespace Camera
	{
		// GLOBAL: TOY2 0x0052B814
		int16_t g_cameraTransitionState;

		// STUB: TOY2 0x004045E0
		void GameplayMode(GameplayCamera* camera) {}

		// STUB: TOY2 0x004038E0
		void VisorMode(GameplayCamera* camera) {}

		// FUNCTION: TOY2 0x00405860 [PROVISIONAL]
		void UpdateActiveTransform()
		{
			switch (g_cameraTransitionState)
			{
				case 1:
					if (Nu3D::Camera::g_cameraTintRed == 0)
					{
						Nu3D::Camera::SetTint(0x80, 0x80, 0x80, 0x10);
						g_gameplayStateFlags |= GAMEPLAY_STATE_CUTSCENE_ACTIVE;
						g_cameraTransitionState = 0;
					}
					break;

				case 2:
					Nu3D::Camera::SetTint(0, 0, 0, 0x10);
					g_cameraTransitionState = 1;
					if ((g_gameplayStateFlags & GAMEPLAY_STATE_LOCK_FACING_DURING_TRANSITION) != 0)
						g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING;
					break;

				case 3:
					if (Nu3D::Camera::g_cameraTintRed == 0)
					{
						Nu3D::Camera::SetTint(0x80, 0x80, 0x80, 0x10);
						g_gameplayStateFlags &= ~GAMEPLAY_STATE_CUTSCENE_ACTIVE;
						g_cameraTransitionState = 0;
						if ((g_gameplayStateFlags & GAMEPLAY_STATE_LOCK_FACING_DURING_TRANSITION) != 0)
							g_buzzActor.actorFlags &= ~Buzz::ACTOR_FLAG_LOCK_FACING;
					}
					break;

				case 4:
					Nu3D::Camera::SetTint(0, 0, 0, 0x10);
					g_cameraTransitionState = 3;
					break;
			}

			if ((g_gameplayStateFlags & GAMEPLAY_STATE_CUTSCENE_ACTIVE) == 0)
			{
				if (g_cutsceneTransitionTimer > 0)
				{
					g_cutsceneTransitionTimer -= Renderer::g_frameDelta;
					if (g_cutsceneTransitionTimer <= 0)
						g_cutsceneTransitionTimer = 0;
				}

				if (g_scriptedCameraState == 0)
					GameplayMode(&g_gameplayCamera);
				else
					VisorMode(&g_gameplayCamera);

				if (g_cutsceneInputLockTimer > 0)
				{
					g_renderCameraTransform.pos.x = g_gameplayCamera.position.view.pos.x
						+ ((g_renderCameraTransform.pos.x - g_gameplayCamera.position.view.pos.x) * g_cutsceneInputLockTimer >> 6);
					g_renderCameraTransform.pos.y = g_gameplayCamera.position.view.pos.y
						+ ((g_renderCameraTransform.pos.y - g_gameplayCamera.position.view.pos.y) * g_cutsceneInputLockTimer >> 6);
					g_renderCameraTransform.pos.z = g_gameplayCamera.position.view.pos.z
						+ ((g_renderCameraTransform.pos.z - g_gameplayCamera.position.view.pos.z) * g_cutsceneInputLockTimer >> 6);

					int32_t pitchDelta = (g_renderCameraTransform.rotation.euler.angles.pitch - g_gameplayCamera.target.view.visorAimAngles.pitch) & 0xFFF;
					if (pitchDelta >= 0x800)
						pitchDelta -= 0x1000;
					int32_t yawDelta = (g_renderCameraTransform.rotation.euler.angles.yaw - g_gameplayCamera.target.view.visorAimAngles.yaw) & 0xFFF;
					if (yawDelta >= 0x800)
						yawDelta -= 0x1000;
					int32_t rollDelta = (g_renderCameraTransform.rotation.euler.roll - g_gameplayCamera.angles.pitch) & 0xFFF;
					if (rollDelta >= 0x800)
						rollDelta -= 0x1000;

					g_renderCameraTransform.rotation.euler.angles.pitch =
						(uint16_t)(g_gameplayCamera.target.view.visorAimAngles.pitch + (pitchDelta * g_cutsceneInputLockTimer >> 6)) & 0xFFF;
					g_renderCameraTransform.rotation.euler.angles.yaw =
						(uint16_t)(g_gameplayCamera.target.view.visorAimAngles.yaw + (yawDelta * g_cutsceneInputLockTimer >> 6)) & 0xFFF;
					g_renderCameraTransform.rotation.euler.roll =
						(int16_t)(g_gameplayCamera.angles.pitch + (rollDelta * g_cutsceneInputLockTimer >> 6)) & 0xFFF;
					g_cutsceneInputLockTimer -= Renderer::g_frameDelta;
					return;
				}

				g_renderCameraTransform.pos = g_gameplayCamera.position.view.pos;
				g_renderCameraTransform.rotation.euler.angles.pitch = g_gameplayCamera.target.view.visorAimAngles.pitch;
				g_renderCameraTransform.rotation.euler.angles.yaw = g_gameplayCamera.target.view.visorAimAngles.yaw;
				g_renderCameraTransform.rotation.euler.roll = g_gameplayCamera.angles.pitch;
				return;
			}
			else
			{
				if (g_cutsceneTransitionTimer < 0x18)
				{
					g_cutsceneTransitionTimer += Renderer::g_frameDelta;
					if (g_cutsceneTransitionTimer >= 0x18)
						g_cutsceneTransitionTimer = 0x18;
				}

				GameplayMode(&g_gameplayCamera);
				SmoothToTarget(&g_cutsceneCamera);

				if (g_cutsceneInputLockTimer > 0)
				{
					g_renderCameraTransform.pos.x = g_cutsceneCamera.position.view.pos.x
						+ ((g_renderCameraTransform.pos.x - g_cutsceneCamera.position.view.pos.x) * g_cutsceneInputLockTimer >> 6);
					g_renderCameraTransform.pos.y = g_cutsceneCamera.position.view.pos.y
						+ ((g_renderCameraTransform.pos.y - g_cutsceneCamera.position.view.pos.y) * g_cutsceneInputLockTimer >> 6);
					g_renderCameraTransform.pos.z = g_cutsceneCamera.position.view.pos.z
						+ ((g_renderCameraTransform.pos.z - g_cutsceneCamera.position.view.pos.z) * g_cutsceneInputLockTimer >> 6);

					int32_t pitchDelta = (g_renderCameraTransform.rotation.euler.angles.pitch - g_cutsceneCamera.angles.pitch) & 0xFFF;
					if (pitchDelta >= 0x800)
						pitchDelta -= 0x1000;
					int32_t yawDelta = (g_renderCameraTransform.rotation.euler.angles.yaw - g_cutsceneCamera.angles.yaw) & 0xFFF;
					if (yawDelta >= 0x800)
						yawDelta -= 0x1000;
					int32_t rollDelta = (g_renderCameraTransform.rotation.euler.roll - g_cutsceneCamera.roll) & 0xFFF;
					if (rollDelta >= 0x800)
						rollDelta -= 0x1000;

					g_renderCameraTransform.rotation.euler.angles.pitch =
						(uint16_t)(g_cutsceneCamera.angles.pitch + (pitchDelta * g_cutsceneInputLockTimer >> 6)) & 0xFFF;
					g_renderCameraTransform.rotation.euler.angles.yaw =
						(uint16_t)(g_cutsceneCamera.angles.yaw + (yawDelta * g_cutsceneInputLockTimer >> 6)) & 0xFFF;
					g_renderCameraTransform.rotation.euler.roll = (int16_t)(g_cutsceneCamera.roll + (rollDelta * g_cutsceneInputLockTimer >> 6)) & 0xFFF;
					g_cutsceneInputLockTimer -= Renderer::g_frameDelta;
					return;
				}

				g_renderCameraTransform.pos = g_cutsceneCamera.position.view.pos;
				g_renderCameraTransform.rotation.euler.angles = g_cutsceneCamera.angles;
				g_renderCameraTransform.rotation.euler.roll = g_cutsceneCamera.roll;
				return;
			}
		}

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
		CameraState g_cutsceneCamera;

		// GLOBAL: TOY2 0x0050A0CC
		int32_t g_nextCutsceneMoveSpeed;

		// GLOBAL: TOY2 0x0050A0C8
		int32_t g_cutsceneRecordType;

		// GLOBAL: TOY2 0x0050A118
		Vector3I g_actorCameraTarget;

		// GLOBAL: TOY2 0x0050A128
		int32_t g_targetCameraDistance;

		// GLOBAL: TOY2 0x0050A12C
		int32_t g_cameraCollisionFlags;

		// GLOBAL: TOY2 0x0050A134
		int32_t g_cutsceneWaitTimer;

		// GLOBAL: TOY2 0x0050A144
		int32_t g_cutsceneMoveSpeed;

		// GLOBAL: TOY2 0x0050A148
		int32_t g_cutsceneTransitionTimer;

		// GLOBAL: TOY2 0x0050A294
		const int32_t* g_cutsceneCommandCursor;

		// GLOBAL: TOY2 0x0050A4B4
		int32_t g_cameraYawVelocity;

		// GLOBAL: TOY2 0x0050A4B8
		int32_t g_unusedCameraState0;

		// GLOBAL: TOY2 0x0050A4BC
		int32_t g_cutsceneCameraPathPoint;

		// GLOBAL: TOY2 0x0050A4D0
		int32_t g_cutsceneElapsedTime;

		// GLOBAL: TOY2 0x0050A4E0
		int32_t g_movingPlatformCameraBlend;

		// GLOBAL: TOY2 0x0050A4E4
		int32_t g_cameraRecenterYawDelta;

		// GLOBAL: TOY2 0x0050A4E8
		int32_t g_cameraSnapToTarget;

		// GLOBAL: TOY2 0x0050A514
		int32_t g_cutsceneFocusPathPoint;

		// GLOBAL: TOY2 0x0050A534
		int32_t g_cameraRecenterTimer;

		// GLOBAL: TOY2 0x0050A53C
		int32_t g_cutsceneSegmentProgress;

		// GLOBAL: TOY2 0x0050A1F0
		int32_t g_cutsceneSegmentDuration;

		// GLOBAL: TOY2 0x0052AD98
		int32_t g_cameraSmoothingDivisor;

		// GLOBAL: TOY2 0x0052F118
		int32_t g_unusedCameraState1;

		// GLOBAL: TOY2 0x0052F1CC
		int32_t g_forwardInputDisabled;

		// GLOBAL: TOY2 0x0050A510
		int32_t g_shakeTimer;

		// GLOBAL: TOY2 0x0050A538
		Nu3D::Particles::ParticleInstance* g_cameraMarkerParticle;

		// GLOBAL: TOY2 0x0050A4DC
		Nu3D::Particles::ParticleInstance* g_targetMarkerParticle;

		// FUNCTION: TOY2 0x00447BD0 [PROVISIONAL]
		void CullActors(const Vector3I* cameraPosition)
		{
			if (Actor::g_activeActors[0] == 0)
				return;

			Actor::Toy2Actor** actorSlot = Actor::g_activeActors;
			do
			{
				Actor::Toy2Actor* actor = *actorSlot;
				int32_t visibilityDistanceSquared = actor->visibilityDistance * actor->visibilityDistance * 16;
				uint16_t previousFlags = actor->actorFlags;
				int32_t targetableDistanceBonus = (previousFlags & Actor::ACTOR_FLAG_TARGETABLE) != 0 ? 250000 : 0;
				actor->actorFlags &= ~(Actor::ACTOR_FLAG_TARGETABLE | Actor::ACTOR_FLAG_CULLED);

				if (CharacterLoader::g_characterAnimationData[actor->creatureId]->modelId != 0)
				{
					if (actor->areaIndex < 0 || ! Nu3D::Portal::IsAreaVisible(actor->areaIndex))
					{
						actor->actorFlags |= Actor::ACTOR_FLAG_CULLED;
					}
					else
					{
						int32_t deltaX = (cameraPosition->x - actor->pos.x) >> 5;
						int32_t deltaY = (cameraPosition->y - actor->pos.y) >> 5;
						int32_t deltaZ = (cameraPosition->z - actor->pos.z) >> 5;
						if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < visibilityDistanceSquared + targetableDistanceBonus)
						{
							Vector3F center;
							center.x = (float)(actor->pos.x / 32);
							center.y = (float)(actor->pos.y / 32);
							center.z = (float)(actor->pos.z / 32);
							if ((Nu3D::Frustum::TestSphereAllPlanes(&center, (float)actor->boundingSphereRadius) & Nu3D::Frustum::OUTSIDE_PLANE_MASK) == 0)
								actor->actorFlags |= Actor::ACTOR_FLAG_TARGETABLE;
						}
					}
				}
				actorSlot++;
			} while (*actorSlot != 0);
		}

		// FUNCTION: TOY2 0x00403450 [PROVISIONAL]
		void InitGameplayCamera(GameplayCamera* camera, Buzz::Toy2BuzzActor* buzz)
		{
			memset(&g_renderCameraTransform, 0, sizeof(g_renderCameraTransform));
			memset(camera, 0, sizeof(*camera));
			memset(&g_cutsceneCamera, 0, sizeof(g_cutsceneCamera) + sizeof(g_cameraTransitionState) + sizeof(g_gameplayStateFlags));

			int16_t buzzYaw = buzz->posAngles.angles.yaw;
			camera->angles.yaw = 0x4B0;
			camera->target.view.visorAimAngles.yaw = buzzYaw;
			camera->roll = buzzYaw;
			camera->target.view.visorAimAngles.pitch = 0;
			camera->angles.pitch = 0;
			camera->position.view.pos.x = (Numerics::g_sinCosLUT[(buzzYaw - 0x800) & 0xFFF] * 0x4B0 >> 9) + buzz->posAngles.pos.x;
			camera->position.view.pos.z = (Numerics::g_sinCosLUT[(buzzYaw - 0x400) & 0xFFF] * 0x4B0 >> 9) + buzz->posAngles.pos.z;
			camera->position.view.pos.y = buzz->posAngles.pos.y;
			camera->position.view.pos.y = Nu3D::Collision::GetGroundHeight(&camera->position.groundProbe, 0);
			if (camera->position.view.pos.y == (int32_t)0x80000000)
			{
				camera->position.view.pos.y = buzz->posAngles.pos.y;
			}
			if (camera->position.view.pos.y > buzz->posAngles.pos.y - 0x4000)
			{
				camera->position.view.pos.y = buzz->posAngles.pos.y - 0x4000;
			}

			camera->position.view.lookAt.x = camera->position.view.pos.x;
			camera->position.view.pos.y -= 0x3200;
			camera->position.view.lookAt.y = camera->position.view.pos.y;
			camera->position.view.lookAt.z = camera->position.view.pos.z;
			camera->state.fields.unused1 = 0;
			camera->state.fields.modeTransitionState = 0;
			camera->state.fields.orbitPitch = 0x40;
			Actor::g_renderActors[65] = 0;
			g_unusedCameraState1 = 0;
			g_forwardInputDisabled = 0;
			camera->state.fields.orbitInputFlags = 0;
			camera->target.x = g_buzzActor.posAngles.pos.y;
			camera->target.y = g_buzzActor.posAngles.pos.y;
			camera->state.fields.unused0 = 0;

			g_scriptedCameraState = 0;
			g_actorCameraTarget.x = (int32_t)0x80000000;
			g_cutsceneDuration = 0;
			g_cameraSnapToTarget = 0;
			g_cameraSmoothingDivisor = 0x10;
			g_cameraYawVelocity = 0;
			g_unusedCameraState0 = 0;
			g_cameraCollisionFlags = 0;
			g_cameraRecenterYawDelta = 0;
			g_targetCameraDistance = 0x4B0;
			g_cameraRecenterTimer = 0;
			g_shakeTimer = 0;
			g_movingPlatformCameraBlend = 0;
			g_cutsceneInputLockTimer = 0;
			Nu3D::Camera::g_viewHistoryInitialized = 0;
			g_cutsceneCommandCursor = 0;
			g_cutsceneWaitTimer = 0;
			g_cutsceneSegmentProgress = 0;
			g_cutsceneMoveSpeed = 0;
			g_nextCutsceneMoveSpeed = 0;
			g_cutsceneTransitionTimer = 0;
			g_cutsceneFocusPathPoint = 0;
			g_cutsceneCameraPathPoint = 0;

			Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
		}

		// FUNCTION: TOY2 0x00402030 [MATCHED]
		void InitCutsceneCamera(const Vector3I* focusPosition, const Vector3I* cameraPosition)
		{
			g_cutsceneCamera.position.view.pos.x = cameraPosition->x;
			g_cutsceneCamera.position.view.pos.y = cameraPosition->y;
			g_cutsceneCamera.position.view.pos.z = cameraPosition->z;
			g_cutsceneCamera.position.view.lookAt.x = focusPosition->x;
			g_cutsceneCamera.position.view.lookAt.y = focusPosition->y;
			g_cutsceneCamera.position.view.lookAt.z = focusPosition->z;

			g_cutsceneCamera.target.x = g_cutsceneCamera.position.view.pos.x;
			g_cutsceneCamera.target.y = g_cutsceneCamera.position.view.pos.y;
			g_cutsceneCamera.target.view.z = g_cutsceneCamera.position.view.pos.z;

			int32_t deltaX = (g_cutsceneCamera.position.view.lookAt.x - g_cutsceneCamera.position.view.pos.x) >> 5;
			int32_t deltaY = (g_cutsceneCamera.position.view.lookAt.y - g_cutsceneCamera.position.view.pos.y) >> 5;
			int32_t deltaZ = (g_cutsceneCamera.position.view.lookAt.z - g_cutsceneCamera.position.view.pos.z) >> 5;

			g_cutsceneCamera.angles.yaw = (uint16_t)Nu3D::Math::CartesianToFixedAngle(deltaX, deltaZ);
			if (deltaY < 0)
			{
				g_cutsceneCamera.angles.pitch = (uint16_t)-Nu3D::Math::CartesianToFixedAngle(deltaY * deltaY, deltaZ * deltaZ + deltaX * deltaX);
			}
			else
			{
				g_cutsceneCamera.angles.pitch = (uint16_t)-Nu3D::Math::CartesianToFixedAngle(-(deltaY * deltaY), deltaZ * deltaZ + deltaX * deltaX);
			}
			g_cutsceneCamera.roll = 0;
		}

		// FUNCTION: TOY2 0x004020F0 [EFFECTIVE]
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
				g_gameplayCamera.position.view.pos.y = g_buzzActor.posAngles.pos.y - 0x3000;
				g_gameplayCamera.angles.yaw = 0x4B0;
				g_gameplayCamera.target.view.visorAimAngles.pitch = 0;
				g_gameplayCamera.position.view.lookAt.x = g_gameplayCamera.position.view.pos.x;
				g_gameplayCamera.state.fields.modeTransitionState = 0;
				g_scriptedCameraState = 0;

				Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
			}

			g_gameplayStateFlags |= 1;
			g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING;
			InputManager::g_directionInputState &= INPUT_SECRET_MENU | INPUT_MENU | INPUT_CAMERA_LEFT | INPUT_CAMERA_RIGHT;
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

		// FUNCTION: TOY2 0x00402290 [EFFECTIVE]
		void BeginScriptedCutsceneOnBuzz(int32_t duration)
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
				g_gameplayCamera.position.view.pos.y = g_buzzActor.posAngles.pos.y - 0x3000;
				g_gameplayCamera.angles.yaw = 0x4B0;
				g_gameplayCamera.target.view.visorAimAngles.pitch = 0;
				g_gameplayCamera.position.view.lookAt.x = g_gameplayCamera.position.view.pos.x;
				g_gameplayCamera.state.fields.modeTransitionState = 0;
				g_scriptedCameraState = 0;

				Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
				Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
			}

			g_gameplayStateFlags |= GAMEPLAY_STATE_CUTSCENE_ACTIVE;
			g_buzzActor.actorFlags |= Buzz::ACTOR_FLAG_LOCK_FACING;
			InputManager::g_directionInputState &= INPUT_SECRET_MENU | INPUT_MENU | INPUT_CAMERA_LEFT | INPUT_CAMERA_RIGHT;
			g_cutsceneFocusPosition.x = g_buzzActor.posAngles.pos.x;
			g_cutsceneFocusPosition.y = g_buzzActor.posAngles.pos.y - 0x3000;
			g_cutsceneFocusPosition.z = g_buzzActor.posAngles.pos.z;
			g_cutsceneInputLockTimer = 0x40;

			int32_t yaw =
				Nu3D::Math::CartesianToFixedAngle(
					g_buzzActor.posAngles.pos.x - g_renderCameraTransform.pos.x, g_buzzActor.posAngles.pos.z - g_renderCameraTransform.pos.z)
				& 0xFFF;
			g_cutsceneCameraPosition.x = Numerics::g_sinCosLUT[(yaw - 0x800) & 0xFFF] + g_cutsceneFocusPosition.x;
			g_cutsceneCameraPosition.y = g_cutsceneFocusPosition.y;
			g_cutsceneCameraPosition.z = Numerics::g_sinCosLUT[(yaw - 0x400) & 0xFFF] + g_cutsceneFocusPosition.z;
			g_cutsceneDuration = duration;
			InitCutsceneCamera(&g_cutsceneFocusPosition, &g_cutsceneCameraPosition);
		}

		// FUNCTION: TOY2 0x00403640 [PROVISIONAL]
		void SmoothToTarget(CameraState* camera)
		{
			camera->position.view.pos.x += (camera->target.x - camera->position.view.pos.x) >> 3;
			camera->position.view.pos.y += (camera->target.y - camera->position.view.pos.y) >> 3;
			camera->position.view.pos.z += (camera->target.view.z - camera->position.view.pos.z) >> 3;

			int32_t deltaX = (camera->position.view.lookAt.x - camera->target.x) >> 5;
			int32_t deltaY = (camera->position.view.lookAt.y - camera->target.y) >> 5;
			int32_t deltaZ = (camera->position.view.lookAt.z - camera->target.view.z) >> 5;

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
			camera->position.view.pos.y = g_buzzActor.posAngles.pos.y - 0x3000;
			camera->target.view.visorAimAngles.pitch = 0;
			camera->position.view.lookAt.x = camera->position.view.pos.x;
			camera->state.fields.modeTransitionState = 0;
			g_buzzActor.actorFlags |= 1;
			g_scriptedCameraState = 0;

			Nu3D::Link::SetScaleFromFixedOffsets(0x2D, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x2E, 0, 0, 0);
			Nu3D::Link::SetScaleFromFixedOffsets(0x2F, 0, 0, 0);
		}
	}

	namespace Shadow
	{
		// FUNCTION: TOY2 0x00447D30 [MATCHED]
		void ResetShadowCount()
		{
			Renderer::Shadows::g_shadowCount = 0;
			Renderer::Shadows::g_unusedShadowVar = 0;
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

	// FUNCTION: TOY2 0x00433F40 [PROVISIONAL]
	int32_t CalculateMaxTurnAngle(uint16_t directionInputState)
	{
		int32_t inputMagnitude = 0;
		int32_t cameraRelativeAngle = Nu3D::Math::CartesianToFixedAngle(
			(Toy2::g_buzzActor.posAngles.pos.x - Toy2::Camera::g_gameplayCamera.position.view.lookAt.x) >> 5,
			(Toy2::g_buzzActor.posAngles.pos.z - Toy2::Camera::g_gameplayCamera.position.view.lookAt.z) >> 5);

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

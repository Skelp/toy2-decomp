#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Portal.h"
#include "Nu3D/Scene.h"
#include "Nu3D/Viewport.h"
#include "CharacterLoader.h"
#include "DrawingDevice.h"
#include "Random.h"
#include "Renderer/Renderer.h"
#include "SoftwareRenderer.h"
#include "Toy2/Actor.h"
#include "Toy2/Direct6.h"
#include "Toy2/Toy2.h"
#include "Toy2/Weather.h"
#include <FLOAT.H>
#include <MATH.H>
#include <STDLIB.H>

namespace Toy2
{
	extern int32_t g_destRectHalfWidth;
}

namespace SoftwareRenderer
{
	struct SoftwareRasterVertex;
}

namespace Nu3D
{
	namespace Camera
	{
		static __forceinline int32_t ShiftFixedTowardZero(int32_t value, int32_t bits) { return (value + ((value >> 31) & ((1 << bits) - 1))) >> bits; }

		// STUB: TOY2 0x00450E50
		int32_t ProjectTriangle(
			const Vector3I16* point0, const Vector3I16* point1, const Vector3I16* point2, SoftwareRenderer::SoftwareRasterVertex* projectedVertices)
		{ return 0; }

		struct ViewRotationHistoryEntry
		{
			Vector3I16 angles;
			int16_t reserved;
		};

		STATIC_ASSERT(sizeof(ViewRotationHistoryEntry) == 8);

		// GLOBAL: TOY2 0x0054DE9C
		int16_t g_cameraTintBlue;

		// GLOBAL: TOY2 0x00554038
		int16_t g_cameraTintGreen;

		// GLOBAL: TOY2 0x0054DD6C
		int16_t g_cameraTintRed;

		// GLOBAL: TOY2 0x0052ADB8
		uint8_t g_targetTintBlue;

		// GLOBAL: TOY2 0x0052AD8C
		uint8_t g_targetTintGreen;

		// GLOBAL: TOY2 0x0052C820
		uint8_t g_targetTintRed;

		// GLOBAL: TOY2 0x00529E50
		uint8_t g_targetTintFadeSpeed;

		// GLOBAL: TOY2 0x00557A9C
		int16_t g_tintBlend;

		// GLOBAL: TOY2 0x0054C100
		FixedViewTransform g_workingFixedViewTransform;

		// FUNCTION: TOY2 0x00448F00 [PROVISIONAL]
		int32_t IsActorSpawnVisible(const Vector3I* cameraPosition, const Toy2::Actor::Toy2Actor* actor)
		{
			int32_t deltaX;
			int32_t deltaY;
			int32_t deltaZ;
			int32_t depth;
			int32_t horizontal;
			int32_t vertical;
			int32_t radius;
			int32_t viewBoundary;

			if (CharacterLoader::g_characterAnimationData[actor->creatureId]->modelId != 0)
			{
				deltaY = actor->creatureRam->pos.y * 32 + actor->boundingOffset.y - cameraPosition->y;
				deltaZ = actor->creatureRam->pos.z * 32 - cameraPosition->z;
				deltaX = actor->creatureRam->pos.x * 32 - cameraPosition->x;
				radius = actor->boundingSphereRadius;
				depth = ShiftFixedTowardZero(g_workingFixedViewTransform.rotation.m22 * deltaZ + g_workingFixedViewTransform.rotation.m21 * deltaY
						+ g_workingFixedViewTransform.rotation.m20 * deltaX,
					17);

				if (depth > -radius)
				{
					horizontal = ShiftFixedTowardZero(g_workingFixedViewTransform.rotation.m02 * deltaZ + g_workingFixedViewTransform.rotation.m01 * deltaY
							+ g_workingFixedViewTransform.rotation.m00 * deltaX,
						17);
					viewBoundary = (depth << 8) / Toy2::g_destRectHalfWidth;
					if (viewBoundary > abs(horizontal) - radius)
					{
						vertical = ShiftFixedTowardZero(g_workingFixedViewTransform.rotation.m12 * deltaZ + g_workingFixedViewTransform.rotation.m11 * deltaY
								+ g_workingFixedViewTransform.rotation.m10 * deltaX,
							17);
						if (viewBoundary > abs(vertical) - radius)
							return 1;
					}
				}
			}

			return 0;
		}

		// GLOBAL: TOY2 0x00E4D880
		CameraData g_activeCamera;

		// GLOBAL: TOY2 0x00B623FC
		CameraData* g_currentCamera;

		// GLOBAL: TOY2 0x00B223C8
		ActiveCameraTransform g_activeCameraTransform;

		// GLOBAL: TOY2 0x00555314
		Vector4I g_fixedViewPosition;

		// GLOBAL: TOY2 0x00555324
		Vector3I g_sectorViewPosition;

		// GLOBAL: TOY2 0x00555334
		FixedViewTransform g_fixedViewTransform;

		// GLOBAL: TOY2 0x005D2AE0
		ObjectViewTransform g_objectViewTransform;

		// GLOBAL: TOY2 0x00B62404
		int32_t g_cameraSkewEnabled;

		// GLOBAL: TOY2 0x00B62408
		int32_t g_cameraSkewPhase;

		// GLOBAL: TOY2 0x00E4D980
		Vector3F g_cameraPosition;

		// GLOBAL: TOY2 0x00E4D994
		int32_t g_cameraRoll;

		// GLOBAL: TOY2 0x00E4D998
		int32_t g_cameraPitch;

		// GLOBAL: TOY2 0x00E4D99C
		int32_t g_cameraYaw;

		// GLOBAL: TOY2 0x00E4D970
		float g_softwareProjectionScaleX;

		// GLOBAL: TOY2 0x00E4D9A0
		float g_softwareProjectionScaleY;

		// GLOBAL: TOY2 0x009F6014
		int32_t g_billboardYaw;

		// GLOBAL: TOY2 0x00554F0C
		int16_t g_viewHeightHistory[64];

		// GLOBAL: TOY2 0x0054BEF8
		ViewRotationHistoryEntry g_viewMatrixHistory[64];

		// GLOBAL: TOY2 0x0054E058
		FixedViewTransform g_previousFixedViewTransform;

		// GLOBAL: TOY2 0x0054F630
		Vector4I g_previousFixedViewPosition;

		// GLOBAL: TOY2 0x0054DEA8
		ViewRotationHistoryEntry g_fixedViewAngles;

		// GLOBAL: TOY2 0x0054DD68
		int32_t g_fixedViewTransformValid;

		// GLOBAL: TOY2 0x00557AB0
		FixedViewTransform g_blendedViewTransforms[4];

		// GLOBAL: TOY2 0x00554DC0
		int32_t g_zoneViewportLeftOffset;

		// GLOBAL: TOY2 0x00553028
		int32_t g_zoneViewportTopOffset;

		// GLOBAL: TOY2 0x0055302C
		int32_t g_zoneViewportRightOffset;

		// GLOBAL: TOY2 0x005546B4
		int32_t g_zoneViewportBottomOffset;

		// GLOBAL: TOY2 0x0050A1F8
		int32_t g_viewHistoryInitialized;

		// GLOBAL: TOY2 0x0054DEC0
		int16_t g_viewSwayPhase;

		// GLOBAL: TOY2 0x0054F620
		int16_t g_viewBobPhase;

		// GLOBAL: TOY2 0x00553034
		int16_t g_viewHeightTarget;

		// GLOBAL: TOY2 0x0054E04C
		int16_t g_viewHeightVelocity;

		// GLOBAL: TOY2 0x00547EDC
		int16_t g_viewHistoryPhase;

		// GLOBAL: TOY2 0x0054DEB2
		int16_t g_viewHistoryIndex;

		// GLOBAL: TOY2 0x00556FA0
		int16_t g_viewMotionState;

		// GLOBAL: TOY2 0x00A4C410
		int32_t g_effectMode;

		// FUNCTION: TOY2 0x004A1BB0 [MATCHED]
		void SetTint(uint8_t blue, uint8_t green, uint8_t red, uint8_t fadeSpeed)
		{
			g_targetTintBlue = blue;
			g_targetTintGreen = green;
			g_targetTintRed = red;
			g_targetTintFadeSpeed = fadeSpeed;
			g_tintBlend = -1;
		}

		// FUNCTION: TOY2 0x0044DF90 [EFFECTIVE]
		void InitViewMatrixGlobals()
		{
			memset(g_viewHeightHistory, 0, sizeof(g_viewHeightHistory));
			memset(g_viewMatrixHistory, 0, sizeof(g_viewMatrixHistory));
			g_viewHistoryInitialized = 0;
			g_viewSwayPhase = 0;
			g_viewBobPhase = 0;
			g_viewHeightTarget = 0;
			g_viewHeightVelocity = 0;
			g_viewHistoryPhase = 0;
			g_viewHistoryIndex = 0;
			g_viewMotionState = 0;
		}

		// FUNCTION: TOY2 0x004A1BE0 [PROVISIONAL]
		int32_t FadeToTargetTint()
		{
			int32_t fadeStep = Renderer::g_frameDelta * g_targetTintFadeSpeed / 2;
			int32_t anyChannelChanged = 0;

			if (fadeStep)
			{
				if (g_cameraTintBlue != g_targetTintBlue)
				{
					anyChannelChanged = 1;
					if (g_cameraTintBlue < g_targetTintBlue)
					{
						g_cameraTintBlue += fadeStep;
						if (g_cameraTintBlue > g_targetTintBlue)
							g_cameraTintBlue = g_targetTintBlue;
					}
					else
					{
						g_cameraTintBlue -= fadeStep;
						if (g_cameraTintBlue < g_targetTintBlue)
							g_cameraTintBlue = g_targetTintBlue;
					}
				}

				if (g_cameraTintGreen != g_targetTintGreen)
				{
					anyChannelChanged = 1;
					if (g_cameraTintGreen < g_targetTintGreen)
					{
						g_cameraTintGreen += fadeStep;
						if (g_cameraTintGreen > g_targetTintGreen)
							g_cameraTintGreen = g_targetTintGreen;
					}
					else
					{
						g_cameraTintGreen -= fadeStep;
						if (g_cameraTintGreen < g_targetTintGreen)
							g_cameraTintGreen = g_targetTintGreen;
					}
				}

				if (g_cameraTintRed != g_targetTintRed)
				{
					anyChannelChanged = 1;
					if (g_cameraTintRed < g_targetTintRed)
					{
						g_cameraTintRed += fadeStep;
						if (g_cameraTintRed > g_targetTintRed)
							g_cameraTintRed = g_targetTintRed;
					}
					else
					{
						g_cameraTintRed -= fadeStep;
						if (g_cameraTintRed < g_targetTintRed)
							g_cameraTintRed = g_targetTintRed;
					}
					return anyChannelChanged;
				}

				if (anyChannelChanged)
					return anyChannelChanged;
			}

			g_targetTintFadeSpeed = 0;
			return anyChannelChanged;
		}

		static __forceinline void BuildBlendedViewTransform(FixedViewTransform* transform, int32_t rotationIndex, int32_t row0ScaleIndex)
		{
			Math::SetRotationXYZ(&g_viewMatrixHistory[rotationIndex].angles, &transform->rotation);

			int32_t blend = abs((int32_t)g_viewHeightHistory[rotationIndex]);
			int32_t row0Scale = ShiftFixedTowardZero(g_viewHeightHistory[row0ScaleIndex], 4) + 0xF80;
			int32_t row2Scale = ShiftFixedTowardZero(g_viewHeightHistory[rotationIndex], 4) + 0xF80;

			transform->rotation.m00 =
				(int16_t)ShiftFixedTowardZero((ShiftFixedTowardZero(transform->rotation.m00 * blend, 12) - blend + 0x1000) * row0Scale, 12);
			transform->rotation.m01 = (int16_t)ShiftFixedTowardZero(ShiftFixedTowardZero(transform->rotation.m01 * blend, 12) * row0Scale, 12);
			transform->rotation.m02 = (int16_t)ShiftFixedTowardZero(ShiftFixedTowardZero(transform->rotation.m02 * blend, 12) * row0Scale, 12);
			transform->rotation.m10 = (int16_t)ShiftFixedTowardZero(transform->rotation.m10 * blend, 12);
			transform->rotation.m11 = (int16_t)(ShiftFixedTowardZero(transform->rotation.m11 * blend, 12) - blend + 0x1000);
			transform->rotation.m12 = (int16_t)ShiftFixedTowardZero(transform->rotation.m12 * blend, 12);
			transform->rotation.m20 = (int16_t)ShiftFixedTowardZero(ShiftFixedTowardZero(transform->rotation.m20 * blend, 12) * row2Scale, 12);
			transform->rotation.m21 = (int16_t)ShiftFixedTowardZero(ShiftFixedTowardZero(transform->rotation.m21 * blend, 12) * row2Scale, 12);
			transform->rotation.m22 =
				(int16_t)ShiftFixedTowardZero((ShiftFixedTowardZero(transform->rotation.m22 * blend, 12) - blend + 0x1000) * row2Scale, 12);

			ViewMatrix::MultiplyFixed(&g_fixedViewTransform.rotation, &transform->rotation, &transform->rotation);
		}

		// FUNCTION: TOY2 0x00446FC0 [PROVISIONAL]
		void SetupViewMatrix(ActiveCameraTransform* camera)
		{
			ViewRotationHistoryEntry viewAngles;
			viewAngles.angles.x = camera->rotation.vector.x;
			viewAngles.angles.y = (int16_t)(-camera->rotation.vector.y & 0xFFF);
			viewAngles.angles.z = camera->rotation.vector.z;

			Toy2::Sector::g_viewRotation = viewAngles.angles;
			g_previousFixedViewPosition = g_fixedViewPosition;
			g_previousFixedViewTransform = g_fixedViewTransform;
			g_fixedViewAngles = viewAngles;
			g_fixedViewTransformValid = 1;

			g_zoneViewportLeftOffset = 0;
			g_zoneViewportTopOffset = 0;
			g_zoneViewportRightOffset = 0;
			g_zoneViewportBottomOffset = 0;

			Math::SetRotationXYZ(&viewAngles.angles, &g_workingFixedViewTransform.rotation);

			if (g_renderMode == RENDERMODE_SOFTWARE)
			{
				g_workingFixedViewTransform.rotation.m00 = (int16_t)(g_workingFixedViewTransform.rotation.m00 * Toy2::g_softWindowWidth / 320);
				g_workingFixedViewTransform.rotation.m01 = (int16_t)(g_workingFixedViewTransform.rotation.m01 * Toy2::g_softWindowWidth / 320);
				g_workingFixedViewTransform.rotation.m02 = (int16_t)(g_workingFixedViewTransform.rotation.m02 * Toy2::g_softWindowWidth / 320);
				g_workingFixedViewTransform.rotation.m10 = (int16_t)(g_workingFixedViewTransform.rotation.m10 * Toy2::g_softWindowHeight / 240);
				g_workingFixedViewTransform.rotation.m11 = (int16_t)(g_workingFixedViewTransform.rotation.m11 * Toy2::g_softWindowHeight / 240);
				g_workingFixedViewTransform.rotation.m12 = (int16_t)(g_workingFixedViewTransform.rotation.m12 * Toy2::g_softWindowHeight / 240);
			}

			g_fixedViewPosition.x = camera->pos.x;
			g_fixedViewPosition.y = camera->pos.y;
			g_fixedViewPosition.z = camera->pos.z;
			g_sectorViewPosition.x = ShiftFixedTowardZero(camera->pos.x, 5);
			g_sectorViewPosition.y = ShiftFixedTowardZero(camera->pos.y, 5);
			g_sectorViewPosition.z = ShiftFixedTowardZero(camera->pos.z, 5);
			g_fixedViewTransform = g_workingFixedViewTransform;

			g_viewBobPhase += 0x18;
			g_viewHistoryPhase += 0x20;
			g_viewSwayPhase += 0xD;
			g_viewHistoryIndex = (uint8_t)(g_viewHistoryIndex - 1) & 0x3F;

			int32_t historyIndex = g_viewHistoryIndex;
			int32_t nextHistoryIndex = (historyIndex + 1) & 0x3F;
			int32_t bobHeight = ShiftFixedTowardZero(Numerics::g_sinCosLUT[(uint16_t)g_viewHistoryPhase & 0xFFF], 4);
			int32_t heightDelta = g_viewHeightTarget - g_viewHeightHistory[nextHistoryIndex] + bobHeight;

			if (heightDelta > 0)
				g_viewHeightVelocity = (int16_t)(ShiftFixedTowardZero(g_viewHeightVelocity * 62, 6) + 0x10);
			else
				g_viewHeightVelocity = (int16_t)(ShiftFixedTowardZero(g_viewHeightVelocity * 62, 6) - 0x10);

			g_viewHeightHistory[historyIndex] = g_viewHeightVelocity + g_viewHeightHistory[nextHistoryIndex];

			int32_t bobAngle = g_viewBobPhase;
			int32_t swayAngle = (g_viewSwayPhase >> 5) & 0x7F;
			int32_t bobSine = ShiftFixedTowardZero(Numerics::g_sinCosLUT[bobAngle & 0xFFF], 10);
			int32_t bobCosine = ShiftFixedTowardZero(Numerics::g_sinCosLUT[(bobAngle + 0x400) & 0xFFF], 10);
			int32_t swaySine = ShiftFixedTowardZero(Numerics::g_sinCosLUT[swayAngle], 9);
			int32_t swayCosine = ShiftFixedTowardZero(Numerics::g_sinCosLUT[swayAngle + 0x400], 9);

			if (g_viewHeightHistory[historyIndex] < 0)
			{
				g_viewMatrixHistory[historyIndex].angles.x = (int16_t)(bobSine - swayCosine);
				g_viewMatrixHistory[historyIndex].angles.z = (int16_t)(bobCosine - swaySine);
			}
			else
			{
				g_viewMatrixHistory[historyIndex].angles.x = (int16_t)(swayCosine + bobSine);
				g_viewMatrixHistory[historyIndex].angles.z = (int16_t)(bobCosine + swaySine);
			}

			if (abs(g_viewHeightTarget - g_viewHeightHistory[nextHistoryIndex] + bobHeight) < 0x80)
				g_viewHeightTarget = (int16_t)(*g_randDatBufferPtr++ * 0x18 + 0x800);

			int32_t oldestSampleIndex = (historyIndex - 0x10) & 0x3F;
			int32_t middleSampleIndex = (historyIndex - 0x20) & 0x3F;
			int32_t recentSampleIndex = (historyIndex + 0x10) & 0x3F;
			BuildBlendedViewTransform(&g_blendedViewTransforms[0], historyIndex, recentSampleIndex);
			BuildBlendedViewTransform(&g_blendedViewTransforms[1], oldestSampleIndex, historyIndex);
			BuildBlendedViewTransform(&g_blendedViewTransforms[2], middleSampleIndex, oldestSampleIndex);
			BuildBlendedViewTransform(&g_blendedViewTransforms[3], recentSampleIndex, middleSampleIndex);
		}

		// FUNCTION: TOY2 0x004507F0 [MATCHED]
		void SetObjectViewMatrix(const FixedViewTransform* transform)
		{
			g_objectViewTransform.m00 = transform->rotation.m00;
			g_objectViewTransform.m01 = transform->rotation.m01;
			g_objectViewTransform.m02 = transform->rotation.m02;
			g_objectViewTransform.m10 = transform->rotation.m10;
			g_objectViewTransform.m11 = transform->rotation.m11;
			g_objectViewTransform.m12 = transform->rotation.m12;
			g_objectViewTransform.m20 = transform->rotation.m20;
			g_objectViewTransform.m21 = transform->rotation.m21;
			g_objectViewTransform.m22 = transform->rotation.m22;
		}

		// FUNCTION: TOY2 0x00450850 [MATCHED]
		void SetObjectViewPosition(const FixedViewTransform* transform)
		{
			g_objectViewTransform.position.x = transform->position.x;
			g_objectViewTransform.position.y = transform->position.y;
			g_objectViewTransform.position.z = transform->position.z;
		}

		// FUNCTION: TOY2 0x00451810 [MATCHED]
		void SetObjectViewTransform(const Matrix3x3I16* rotation, const Vector3I* position)
		{
			g_objectViewTransform.m00 = rotation->m00;
			g_objectViewTransform.m01 = rotation->m01;
			g_objectViewTransform.m02 = rotation->m02;
			g_objectViewTransform.m10 = rotation->m10;
			g_objectViewTransform.m11 = rotation->m11;
			g_objectViewTransform.m12 = rotation->m12;
			g_objectViewTransform.m20 = rotation->m20;
			g_objectViewTransform.m21 = rotation->m21;
			g_objectViewTransform.m22 = rotation->m22;
			g_objectViewTransform.position.x = position->x;
			g_objectViewTransform.position.y = position->y;
			// Retail copies the Y position into Z.
			g_objectViewTransform.position.z = position->y;
		}

		// FUNCTION: TOY2 0x0043D9E0 [PROVISIONAL]
		void WorldToView(const Vector3I16* source, Vector3I* destination, int32_t* viewDistance)
		{
			Vector3I16 sourcePosition = *source;
			int32_t transformedX =
				sourcePosition.x * g_objectViewTransform.m00 + sourcePosition.y * g_objectViewTransform.m01 + sourcePosition.z * g_objectViewTransform.m02;
			int16_t viewX = (int16_t)(transformedX / 0x1000 + g_objectViewTransform.position.x);

			int32_t transformedY =
				sourcePosition.x * g_objectViewTransform.m10 + sourcePosition.y * g_objectViewTransform.m11 + sourcePosition.z * g_objectViewTransform.m12;
			int16_t viewY = (int16_t)(transformedY / 0x1000 + g_objectViewTransform.position.y);

			int32_t transformedZ =
				sourcePosition.x * g_objectViewTransform.m20 + sourcePosition.y * g_objectViewTransform.m21 + sourcePosition.z * g_objectViewTransform.m22;
			int16_t viewZ = (int16_t)(transformedZ / 0x1000 + g_objectViewTransform.position.z);

			destination->x = viewX;
			destination->y = viewY;
			destination->z = viewZ;
		}

		// FUNCTION: TOY2 0x004B68A0 [MATCHED]
		void SetBillboardYaw(int32_t yaw) { g_billboardYaw = yaw; }

		// FUNCTION: TOY2 0x004CE020 [MATCHED]
		void ToggleCameraSkew(int32_t enabled) { g_cameraSkewEnabled = enabled; }

		// FUNCTION: TOY2 0x0044F820 [MATCHED]
		void EnableCameraSkew() { ToggleCameraSkew(1); }

		// FUNCTION: TOY2 0x004CE030 [MATCHED]
		void SetSkewPhase(int32_t phaseDelta) { g_cameraSkewPhase += phaseDelta; }

		// FUNCTION: TOY2 0x004CE050 [PROVISIONAL]
		void ApplyTransformToCamera(ActiveCameraTransform* camera)
		{
			if (! g_currentCamera)
			{
				Vector3F scale = { 1.0f, -1.0f, 1.0f };
				SetMatrixScaleVector(&scale);
				g_currentCamera = Build();
				g_currentCamera->aspectRatio = 0.75f;
				g_currentCamera->fieldOfView = 1.25f;
				g_currentCamera->farClip = 32768.0f;
				g_currentCamera->nearClip = 50.0f;

				float halfFieldOfView = g_currentCamera->fieldOfView * 0.5f;
				float cotangent = (float)(cos(halfFieldOfView) / sin(halfFieldOfView));
				g_softwareProjectionScaleX = DrawingDevice::GetDestWidth() * g_currentCamera->aspectRatio * cotangent;
				g_softwareProjectionScaleY = DrawingDevice::GetDestHeight() * cotangent;
			}

			if (! camera)
				return;

			g_activeCameraTransform = *camera;
			g_cameraPitch = -(int16_t)camera->rotation.euler.angles.pitch * 16;
			g_cameraYaw = (int16_t)camera->rotation.euler.angles.yaw << 4;
			g_cameraRoll = camera->rotation.euler.roll << 4;

			Math::ApplyRotateXFromLut(&g_currentCamera->transform, g_cameraPitch);
			Math::RotateYFromLut(&g_currentCamera->transform, g_cameraYaw);
			Math::RotateZFromLut(&g_currentCamera->transform, g_cameraRoll);

			g_cameraPosition.x = camera->pos.x * 0.03125f;
			g_cameraPosition.y = camera->pos.y * 0.03125f;
			g_cameraPosition.z = camera->pos.z * 0.03125f;
			Math::AddWorldSpaceTransform(&g_currentCamera->transform, &g_cameraPosition);

			if (! g_cameraSkewEnabled || Renderer::GetIsSoftwareRendering())
			{
				g_currentCamera->scale.z = 1.0f;
				g_currentCamera->scale.y = 1.0f;
				g_currentCamera->scale.x = 1.0f;
			}
			else
			{
				g_currentCamera->scale.x = Numerics::g_trigLUT[g_cameraSkewPhase & 0xFFFF] * 0.05f + 1.0f;
				g_currentCamera->scale.y = Numerics::g_trigLUT[(g_cameraSkewPhase - 0x8000) & 0xFFFF] * 0.05f + 1.0f;
				g_currentCamera->scale.z = Numerics::g_trigLUT[(g_cameraSkewPhase * 2) & 0xFFFF] * 0.05f + 1.0f;
			}

			ApplyCameraTransforms(g_currentCamera);
			SetBillboardYaw(g_cameraYaw);
			SetupViewMatrix(camera);
			SoftwareRenderer::SetCameraNearFarZ(Scene::g_primaryNearClip, Scene::g_primaryFarClip);
		}

		// FUNCTION: TOY2 0x004BB850 [MATCHED]
		D3DMATRIX* GetViewMatrix() { return &g_viewMatrix; }

		// FUNCTION: TOY2 0x004BB860 [MATCHED]
		D3DMATRIX* GetProjectionMatrix() { return &g_projectionMatrix; }

		// FUNCTION: TOY2 0x004BB870 [MATCHED]
		D3DMATRIX* GetClipNormMatrix() { return &g_clipNormMatrix; }

		// FUNCTION: TOY2 0x004BB880 [MATCHED]
		D3DMATRIX* GetScreenSpaceMatrix() { return &g_screenSpaceMatrix; }

		// FUNCTION: TOY2 0x004BB890 [MATCHED]
		CameraData* Build()
		{
			CameraData* camera = (CameraData*)malloc(sizeof(CameraData));

			Math::BuildIdentityMatrix(&camera->transform);
			camera->fieldOfView = 0.75f;
			camera->aspectRatio = 1.0f;
			camera->nearClip = 1.0f;
			camera->portalNearClip = 0.0f;
			camera->farClip = 1000.0f;
			camera->fogFarClip = FLT_MAX;
			camera->aspectRatio = (float)DrawingDevice::GetDestHeight() / (float)DrawingDevice::GetDestWidth();
			camera->scale.z = 1.0f;
			camera->scale.y = 1.0f;
			camera->scale.x = 1.0f;
			return camera;
		}

		// FUNCTION: TOY2 0x004BB910 [MATCHED]
		void Destroy(CameraData* camera)
		{
			if (camera)
				free(camera);
		}

		// FUNCTION: TOY2 0x004BA420 [PROVISIONAL]
		void CalculateFrustumPlanes(CameraData* camera)
		{
			Vector3F right;
			Vector3F up;
			Vector3F forward;
			Vector3F position;
			Math::GetRightVector(&camera->transform, &right);
			Math::GetUpVector(&camera->transform, &up);
			Math::GetForwardVector(&camera->transform, &forward);
			Math::GetPositionVector(&camera->transform, &position);

			float tangent = (float)tan(camera->fieldOfView * 0.5f);
			float nearY = tangent * camera->nearClip;
			float nearX = nearY / camera->aspectRatio;
			float farY = tangent * camera->farClip;
			float farX = farY / camera->aspectRatio;
			float portalY = tangent * camera->portalNearClip;
			float portalX = portalY / camera->aspectRatio;
			float fogY = tangent * camera->fogFarClip;
			float fogX = fogY / camera->aspectRatio;

			Vector3F localPoint;
			Vector3F nearTopLeft;
			Vector3F nearTopRight;
			Vector3F nearBottomRight;
			Vector3F nearBottomLeft;
			Vector3F farTopLeft;
			Vector3F farTopRight;
			Vector3F farBottomRight;
			Vector3F farBottomLeft;
			Vector3F portalTopLeft;
			Vector3F portalTopRight;
			Vector3F portalBottomRight;
			Vector3F portalBottomLeft;
			Vector3F fogTopLeft;
			Vector3F fogTopRight;
			Vector3F fogBottomRight;
			Vector3F fogBottomLeft;

			localPoint.x = -nearX;
			localPoint.y = nearY;
			localPoint.z = camera->nearClip;
			Math::TransformPointByMatrix(&nearTopLeft, &localPoint, &camera->transform);
			localPoint.x = nearX;
			Math::TransformPointByMatrix(&nearTopRight, &localPoint, &camera->transform);
			localPoint.y = -nearY;
			Math::TransformPointByMatrix(&nearBottomRight, &localPoint, &camera->transform);
			localPoint.x = -nearX;
			Math::TransformPointByMatrix(&nearBottomLeft, &localPoint, &camera->transform);

			localPoint.x = -farX;
			localPoint.y = farY;
			localPoint.z = camera->farClip;
			Math::TransformPointByMatrix(&farTopLeft, &localPoint, &camera->transform);
			localPoint.x = farX;
			Math::TransformPointByMatrix(&farTopRight, &localPoint, &camera->transform);
			localPoint.y = -farY;
			Math::TransformPointByMatrix(&farBottomRight, &localPoint, &camera->transform);
			localPoint.x = -farX;
			Math::TransformPointByMatrix(&farBottomLeft, &localPoint, &camera->transform);

			if (camera->portalNearClip > camera->nearClip)
			{
				localPoint.x = -portalX;
				localPoint.y = portalY;
				localPoint.z = camera->portalNearClip;
				Math::TransformPointByMatrix(&portalTopLeft, &localPoint, &camera->transform);
				localPoint.x = portalX;
				Math::TransformPointByMatrix(&portalTopRight, &localPoint, &camera->transform);
				localPoint.y = -portalY;
				Math::TransformPointByMatrix(&portalBottomRight, &localPoint, &camera->transform);
				localPoint.x = -portalX;
				Math::TransformPointByMatrix(&portalBottomLeft, &localPoint, &camera->transform);
			}

			if (camera->fogFarClip < camera->farClip)
			{
				localPoint.x = -fogX;
				localPoint.y = fogY;
				localPoint.z = camera->fogFarClip;
				Math::TransformPointByMatrix(&fogTopLeft, &localPoint, &camera->transform);
				localPoint.x = fogX;
				Math::TransformPointByMatrix(&fogTopRight, &localPoint, &camera->transform);
				localPoint.y = -fogY;
				Math::TransformPointByMatrix(&fogBottomRight, &localPoint, &camera->transform);
				localPoint.x = -fogX;
				Math::TransformPointByMatrix(&fogBottomLeft, &localPoint, &camera->transform);
			}

			Plane* planes = Viewport::g_frustumPlanes;
			if (Viewport::g_reverseFrustumWinding)
			{
				Math::CalculatePlaneFromTriangle(&farTopLeft, &farBottomLeft, &farBottomRight, &planes[1]);
				if (camera->fogFarClip < camera->farClip)
					Math::CalculatePlaneFromTriangle(&fogTopLeft, &fogBottomLeft, &fogBottomRight, &planes[3]);
				else
					planes[3] = planes[1];
				Math::CalculatePlaneFromTriangle(&nearBottomRight, &nearBottomLeft, &nearTopLeft, &planes[0]);
				if (camera->portalNearClip > camera->nearClip)
					Math::CalculatePlaneFromTriangle(&portalBottomRight, &portalBottomLeft, &portalTopLeft, &planes[2]);
				else
					planes[2] = planes[0];
				Math::CalculatePlaneFromTriangle(&farTopLeft, &nearBottomLeft, &farBottomLeft, &planes[4]);
				Math::CalculatePlaneFromTriangle(&farTopRight, &farBottomRight, &nearTopRight, &planes[5]);
				Math::CalculatePlaneFromTriangle(&farTopLeft, &farTopRight, &nearTopLeft, &planes[6]);
				Math::CalculatePlaneFromTriangle(&farBottomRight, &farBottomLeft, &nearBottomLeft, &planes[7]);
			}
			else
			{
				Math::CalculatePlaneFromTriangle(&farBottomRight, &farBottomLeft, &farTopLeft, &planes[1]);
				if (camera->fogFarClip < camera->farClip)
					Math::CalculatePlaneFromTriangle(&fogBottomRight, &fogBottomLeft, &fogTopLeft, &planes[3]);
				else
					planes[3] = planes[1];
				Math::CalculatePlaneFromTriangle(&nearTopLeft, &nearBottomLeft, &nearBottomRight, &planes[0]);
				if (camera->portalNearClip > camera->nearClip)
					Math::CalculatePlaneFromTriangle(&portalTopLeft, &portalBottomLeft, &portalBottomRight, &planes[2]);
				else
					planes[2] = planes[0];
				Math::CalculatePlaneFromTriangle(&farBottomLeft, &nearBottomLeft, &farTopLeft, &planes[4]);
				Math::CalculatePlaneFromTriangle(&nearTopRight, &farBottomRight, &farTopRight, &planes[5]);
				Math::CalculatePlaneFromTriangle(&nearTopLeft, &farTopRight, &farTopLeft, &planes[6]);
				Math::CalculatePlaneFromTriangle(&nearBottomLeft, &farBottomLeft, &farBottomRight, &planes[7]);
			}

			Vector3F projected;
			Nu3D::TransformPointProjective(&projected, &farBottomLeft, 1, 0);
			Viewport::g_viewClipRect.top = projected.x;
			Viewport::g_viewClipRect.right = projected.y;
			Nu3D::TransformPointProjective(&projected, &farTopRight, 1, 0);
			Viewport::g_viewClipRect.bottom = projected.x;
			Viewport::g_viewClipRect.left = projected.y;
			Viewport::g_frustumPlaneCount = 8;
		}

		// FUNCTION: TOY2 0x004BACF0 [PROVISIONAL]
		int32_t ClipViewToPortal(D3DMATRIX* cameraTransform, Portal::AreaPortal* portal)
		{
			Vector3F cameraPosition;
			Math::GetPositionVector(cameraTransform, &cameraPosition);

			Vector3F projectedVertices[100];
			Nu3D::TransformPointProjective(projectedVertices, portal->vertices, portal->vertexCount, 0);

			float minX = FLT_MAX;
			float minY = FLT_MAX;
			float minZ = FLT_MAX;
			float maxX = -FLT_MAX;
			float maxY = -FLT_MAX;
			float maxZ = -FLT_MAX;

			for (int32_t index = 0; index < portal->vertexCount; ++index)
			{
				Vector3F* point = &projectedVertices[index];
				if (point->x <= minX)
					minX = point->x;
				if (point->y <= minY)
					minY = point->y;
				if (point->z <= minZ)
					minZ = point->z;
				if (point->x >= maxX)
					maxX = point->x;
				if (point->y >= maxY)
					maxY = point->y;
				if (point->z >= maxZ)
					maxZ = point->z;
			}

			if (maxZ > 1.0f || minZ < 0.0f)
				return 1;

			Viewport::ViewportRectAlt& clip = Viewport::g_viewClipRect;
			if (minX > clip.bottom || maxX < clip.top || minY > clip.right || maxY < clip.left)
				return 0;

			Vector3F edge1;
			Vector3F edge2;
			Vector3F facing;
			Math::VertexSubtract(&edge1, &projectedVertices[1], &projectedVertices[0]);
			Math::VertexSubtract(&edge2, &projectedVertices[1], &projectedVertices[2]);
			Math::VertexCrossProduct(&facing, &edge1, &edge2);

			if (minX <= clip.top)
				minX = clip.top;
			if (maxX >= clip.bottom)
				maxX = clip.bottom;
			if (minY <= clip.left)
				minY = clip.left;
			if (maxY >= clip.right)
				maxY = clip.right;

			if (facing.z > 0.0f || minX >= maxX || minY >= maxY)
				return 0;

			Vector3F corners[4];
			corners[0].x = minX;
			corners[0].y = maxY;
			corners[0].z = 0.5f;
			corners[1].x = minX;
			corners[1].y = minY;
			corners[1].z = 0.5f;
			corners[2].x = maxX;
			corners[2].y = minY;
			corners[2].z = 0.5f;
			corners[3].x = maxX;
			corners[3].y = maxY;
			corners[3].z = 0.5f;

			UnprojectPointsFromCamera(corners, corners, 4);

			if (Viewport::g_drawPortalOutlines)
			{
				Portal::AreaPortal debugPortal;
				debugPortal.vertexCount = 4;
				debugPortal.vertices = corners;
				Nu3D::DrawDebugPortalOutlines(&debugPortal);
			}

			Math::CalculatePlaneFromTriangle(&corners[1], &corners[0], &cameraPosition, &Viewport::g_frustumPlanes[4]);
			Math::CalculatePlaneFromTriangle(&corners[3], &corners[2], &cameraPosition, &Viewport::g_frustumPlanes[5]);
			Math::CalculatePlaneFromTriangle(&corners[2], &corners[1], &cameraPosition, &Viewport::g_frustumPlanes[6]);
			Math::CalculatePlaneFromTriangle(&corners[0], &corners[3], &cameraPosition, &Viewport::g_frustumPlanes[7]);

			clip.top = minX;
			clip.bottom = maxX;
			clip.left = minY;
			clip.right = maxY;

			if (Viewport::g_viewportClippingEnabled)
			{
				Viewport::SetClipRect(minX, minY, maxX, maxY);
				RebuildTransformPipeline();
			}

			return 1;
		}

		// FUNCTION: TOY2 0x004BBAB0 [PROVISIONAL]
		void ApplyCameraTransforms(const CameraData* camera)
		{
			g_activeCamera = *camera;
			ScaleMatrix(&g_activeCamera.transform);

			if (g_effectMode == 0)
			{
				CreateInverseMatrix_T(&g_viewMatrix, &g_activeCamera.transform);
				Math::ScaleMatrixByVector(&g_viewMatrix, &g_activeCamera.scale);
			}
			else
			{
				D3DMATRIX reflectionFlip;
				D3DMATRIX reflectionInverse;
				D3DMATRIX cameraInverse;

				Math::BuildIdentityMatrix(&reflectionFlip);
				reflectionFlip._22 = -1.0f;
				Math::CreateInverseMatrix(&cameraInverse, &g_activeCamera.transform);
				Math::CreateInverseMatrix(&reflectionInverse, &g_reflectionState.transform);
				g_viewMatrix = reflectionInverse;

				if (g_effectMode == 1)
					Math::MultiplyMatrix3x4(&g_viewMatrix, &g_viewMatrix, &reflectionFlip);

				Math::MultiplyMatrix3x4(&g_viewMatrix, &g_viewMatrix, &g_reflectionState.transform);
				Math::MultiplyMatrix3x4(&g_viewMatrix, &g_viewMatrix, &cameraInverse);
			}

			DrawingDevice::SetViewTransform(&g_viewMatrix);
			BuildPerspectiveProjectionLH(
				&g_projectionMatrix, g_activeCamera.fieldOfView, g_activeCamera.aspectRatio, g_activeCamera.nearClip, g_activeCamera.farClip);
			DrawingDevice::SetProjectionTransform(&g_projectionMatrix);
			RebuildTransformPipeline();
			CalculateFrustumPlanes(&g_activeCamera);
		}

		// FUNCTION: TOY2 0x004BBC70 [MATCHED]
		void ApplyCameraTransformsWithReflection(const CameraData* camera, ReflectionState* reflection)
		{
			g_reflectionState = *reflection;
			ApplyCameraTransforms(camera);
			*reflection = g_reflectionState;
		}

		// FUNCTION: TOY2 0x004BBCB0 [MATCHED]
		void SetEffectMode(int32_t effectMode) { g_effectMode = effectMode; }

		// FUNCTION: TOY2 0x004BC080 [MATCHED]
		void UnprojectPointsFromCamera(Vector3F* output, const Vector3F* input, int32_t count)
		{
			const Vector3F* end = input + count;
			float projectionX = g_projectionMatrix._11;
			float projectionOffset = -g_projectionMatrix._43;
			float projectionY = g_projectionMatrix._22;
			float projectionZ = g_projectionMatrix._33;

			while (input < end)
			{
				Math::TransformPointByMatrix(output, input++, &g_screenToClipMatrix);

				output->z = (projectionOffset + output->z) / projectionZ;
				output->x = output->z * output->x / projectionX;
				output->y = output->z * output->y / projectionY;

				Math::TransformPointByMatrix(output, output, &g_activeCamera.transform);
				++output;
			}
		}

		// FUNCTION: TOY2 0x004BB9C0 [MATCHED]
		void ScaleMatrix(D3DMATRIX* matrix) { Math::ScaleMatrix(matrix); }

		// FUNCTION: TOY2 0x004BB9D0 [MATCHED]
		void SetMatrixScaleVector(const Vector3F* scale) { Math::g_matrixScale = *scale; }

		// FUNCTION: TOY2 0x004BB9F0 [MATCHED]
		void GetMatrixScaleVector(Vector3F* scale) { *scale = Math::g_matrixScale; }

		// FUNCTION: TOY2 0x004BBC00 [MATCHED]
		void BuildPerspectiveProjectionLH(D3DMATRIX* output, float fieldOfView, float aspectRatio, float nearClip, float farClip)
		{
			float depthScale = farClip / (farClip - nearClip);
			float halfFieldOfView = fieldOfView * 0.5f;
			float cotangent = (float)(cos(halfFieldOfView) / sin(halfFieldOfView));

			memset(output, 0, sizeof(*output));
			output->_34 = 1.0f;
			output->_11 = cotangent * aspectRatio;
			output->_22 = cotangent;
			output->_33 = depthScale;
			output->_43 = -depthScale * nearClip;
		}

		// FUNCTION: TOY2 0x004BBC50 [MATCHED]
		void CreateInverseMatrix_T(D3DMATRIX* output, const D3DMATRIX* input) { Math::CreateInverseMatrix(output, input); }
	}

	// FUNCTION: TOY2 0x004BBE10 [MATCHED]
	void TransformPointProjective(Vector3F* output, const Vector3F* input, int32_t count, const D3DMATRIX* transform)
	{
		const Vector3F* end = input + count;
		D3DMATRIX matrix;

		if (transform)
			Math::MultiplyMatrix3x4(&matrix, transform, &Camera::g_screenViewProjectionMatrix);
		else
			memcpy(&matrix, &Camera::g_screenViewProjectionMatrix, sizeof(matrix));

		while (input < end)
		{
			Math::ProjectPoint(output, input, &matrix);
			++input;
			++output;
		}
	}
}

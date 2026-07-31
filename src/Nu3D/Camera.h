#pragma once

#include "Common.h"
#include "Numerics.h"
#include <directx6/d3d.h>
#include <stddef.h>

namespace Nu3D
{
	namespace Portal
	{
		struct AreaPortal;
	}

	namespace Camera
	{
		struct CameraData
		{
			D3DMATRIX transform;
			float fieldOfView;
			float aspectRatio;
			float nearClip;
			float farClip;
			float portalNearClip;
			float fogFarClip;
			Vector3F scale;
		};

		struct ReflectionState
		{
			float distortion;
			D3DMATRIX transform;
			D3DMATRIX uvTransform;
		};

		struct ActiveCameraTransform
		{
			union Rotation
			{
				struct Euler
				{
					Angles angles;
					int16_t roll;
				} euler;
				Vector3I16 vector;
			};

			Vector3I pos;
			Rotation rotation;
			int16_t unkShort;
		};

		struct FixedViewTransform
		{
			Matrix3x3I16 rotation;
			int16_t reserved;
			Vector3I position;
		};

		struct ObjectViewTransform
		{
			int32_t m00;
			int32_t m01;
			int32_t m02;
			int32_t reserved0;
			int32_t m10;
			int32_t m11;
			int32_t m12;
			int32_t reserved1;
			int32_t m20;
			int32_t m21;
			int32_t m22;
			int32_t reserved2;
			Vector3I position;
			int32_t reserved3;
		};

		extern D3DMATRIX g_viewMatrix;
		extern D3DMATRIX g_projectionMatrix;
		extern D3DMATRIX g_clipNormMatrix;
		extern D3DMATRIX g_screenSpaceMatrix;
		extern D3DMATRIX g_screenToClipMatrix;
		extern D3DMATRIX g_screenViewProjectionMatrix;
		extern CameraData g_activeCamera;
		extern CameraData* g_currentCamera;
		extern ActiveCameraTransform g_activeCameraTransform;
		extern Vector4I g_fixedViewPosition;
		extern FixedViewTransform g_fixedViewTransform;
		extern ObjectViewTransform g_objectViewTransform;
		extern ReflectionState g_reflectionState;
		extern int32_t g_effectMode;
		extern int32_t g_billboardYaw;
		extern int32_t g_cameraSkewEnabled;
		extern int32_t g_cameraSkewPhase;
		extern int32_t g_viewHistoryInitialized;

		extern int16_t g_cameraTintBlue;
		extern int16_t g_cameraTintGreen;
		extern int16_t g_cameraTintRed;
		extern uint8_t g_targetTintBlue;
		extern uint8_t g_targetTintGreen;
		extern uint8_t g_targetTintRed;
		extern uint8_t g_targetTintFadeSpeed;
		extern int16_t g_tintBlend;

		void SetTint(uint8_t blue, uint8_t green, uint8_t red, uint8_t fadeSpeed);
		void InitViewMatrixGlobals();
		int32_t FadeToTargetTint();
		void SetupViewMatrix(ActiveCameraTransform* camera);
		void SetObjectViewMatrix(const FixedViewTransform* transform);
		void SetObjectViewPosition(const FixedViewTransform* transform);
		void SetBillboardYaw(int32_t yaw);
		void ToggleCameraSkew(int32_t enabled);
		void EnableCameraSkew();
		void SetSkewPhase(int32_t phaseDelta);
		void ApplyTransformToCamera(ActiveCameraTransform* camera);
		D3DMATRIX* GetViewMatrix();
		D3DMATRIX* GetProjectionMatrix();
		D3DMATRIX* GetClipNormMatrix();
		D3DMATRIX* GetScreenSpaceMatrix();
		CameraData* Build();
		void Destroy(CameraData* camera);
		void CalculateFrustumPlanes(CameraData* camera);
		int32_t ClipViewToPortal(D3DMATRIX* cameraTransform, Portal::AreaPortal* portal);
		void ApplyCameraTransforms(const CameraData* camera);
		void ApplyCameraTransformsWithReflection(const CameraData* camera, ReflectionState* reflection);
		void SetEffectMode(int32_t effectMode);
		void UnprojectPointsFromCamera(Vector3F* output, const Vector3F* input, int32_t count);
		void RebuildTransformPipeline();
		void BuildPerspectiveProjectionLH(D3DMATRIX* output, float fieldOfView, float aspectRatio, float nearClip, float farClip);
		void CreateInverseMatrix_T(D3DMATRIX* output, const D3DMATRIX* input);
		void ScaleMatrix(D3DMATRIX* matrix);
		void SetMatrixScaleVector(const Vector3F* scale);
		void GetMatrixScaleVector(Vector3F* scale);

		STATIC_ASSERT(sizeof(ActiveCameraTransform) == 0x14);
		STATIC_ASSERT(sizeof(FixedViewTransform) == 0x20);
		STATIC_ASSERT(offsetof(FixedViewTransform, position) == 0x14);
		STATIC_ASSERT(sizeof(ObjectViewTransform) == 0x40);
		STATIC_ASSERT(offsetof(ObjectViewTransform, position) == 0x30);
		STATIC_ASSERT(sizeof(CameraData) == 0x64);
		STATIC_ASSERT(sizeof(ReflectionState) == 0x84);
	}
}

#pragma once

#include "Common.h"
#include "Numerics.h"
#include <directx6/d3d.h>

namespace Nu3D
{
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

		struct ActiveCameraTransform
		{
			Vector3I pos;
			Angles angles;
			int16_t roll;
			int16_t unkShort;
		};

		extern D3DMATRIX g_viewMatrix;
		extern D3DMATRIX g_projectionMatrix;
		extern D3DMATRIX g_clipNormMatrix;
		extern D3DMATRIX g_screenSpaceMatrix;

		extern int16_t g_cameraTintBlue;
		extern int16_t g_cameraTintGreen;
		extern int16_t g_cameraTintRed;

		void SetTint(uint8_t blue, uint8_t green, uint8_t red, uint8_t fadeSpeed);
		void InitViewMatrixGlobals();
		void FadeToTargetTint();
		void ApplyTransformToCamera(ActiveCameraTransform* camera);
		D3DMATRIX* GetViewMatrix();
		D3DMATRIX* GetProjectionMatrix();
		D3DMATRIX* GetClipNormMatrix();
		D3DMATRIX* GetScreenSpaceMatrix();
		CameraData* Build();
		void Destroy(CameraData* camera);
		void RebuildTransformPipeline();
		void BuildPerspectiveProjectionLH(D3DMATRIX* output, float fieldOfView, float aspectRatio, float nearClip, float farClip);
		void CreateInverseMatrix_T(D3DMATRIX* output, const D3DMATRIX* input);
		void ScaleMatrix(D3DMATRIX* matrix);
		void SetMatrixScaleVector(const Vector3F* scale);
		void GetMatrixScaleVector(Vector3F* scale);

		STATIC_ASSERT(sizeof(ActiveCameraTransform) == 0x14);
		STATIC_ASSERT(sizeof(CameraData) == 0x64);
	}
}

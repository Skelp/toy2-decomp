#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include <FLOAT.H>
#include <MATH.H>
#include <STDLIB.H>

namespace Nu3D
{
	namespace Camera
	{
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

		// FUNCTION: TOY2 0x004A1BB0
		void SetTint(uint8_t blue, uint8_t green, uint8_t red, uint8_t fadeSpeed)
		{
			g_targetTintBlue = blue;
			g_targetTintGreen = green;
			g_targetTintRed = red;
			g_targetTintFadeSpeed = fadeSpeed;
			g_tintBlend = -1;
		}

		// STUB: TOY2 0x0044DF90
		void InitViewMatrixGlobals() {}

		// FUNCTION: TOY2 0x004A1BE0
		void FadeToTargetTint()
		{
			int32_t anyChannelChanged = 0;
			int32_t fadeStep = Renderer::g_frameDelta * g_targetTintFadeSpeed / 2;

			if (! fadeStep)
			{
				g_targetTintFadeSpeed = 0;
				return;
			}

			if (g_cameraTintBlue != g_targetTintBlue)
			{
				anyChannelChanged = 1;

				if (g_cameraTintBlue >= g_targetTintBlue)
				{
					g_cameraTintBlue -= fadeStep;

					if (g_cameraTintBlue >= g_targetTintBlue)
						goto LBL_FADE_GREEN;
				}
				else
				{
					g_cameraTintBlue += fadeStep;

					if (g_cameraTintBlue <= g_targetTintBlue)
						goto LBL_FADE_GREEN;
				}
				g_cameraTintBlue = g_targetTintBlue;
			}

		LBL_FADE_GREEN:

			if (g_cameraTintGreen == g_targetTintGreen)
				goto LBL_FADE_RED;

			anyChannelChanged = 1;

			if (g_cameraTintGreen >= g_targetTintGreen)
			{
				g_cameraTintGreen -= fadeStep;
				if (g_cameraTintGreen >= g_targetTintGreen)
					goto LBL_FADE_RED;
			}
			else
			{
				g_cameraTintGreen += fadeStep;

				if (g_cameraTintGreen <= g_targetTintGreen)
					goto LBL_FADE_RED;
			}

			g_cameraTintGreen = g_targetTintGreen;

		LBL_FADE_RED:

			if (g_cameraTintRed == g_targetTintRed)
			{
				if (anyChannelChanged)
					return;

				g_targetTintFadeSpeed = 0;
				return;
			}

			if (g_cameraTintRed >= g_targetTintRed)
			{
				g_cameraTintRed -= fadeStep;
				if (g_cameraTintRed < g_targetTintRed)
					g_cameraTintRed = g_targetTintRed;
			}
			else
			{
				g_cameraTintRed += fadeStep;
				if (g_cameraTintRed > g_targetTintRed)
					g_cameraTintRed = g_targetTintRed;
			}
		}

		// STUB: TOY2 0x004CE050
		void ApplyTransformToCamera(ActiveCameraTransform* camera) {}

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
}

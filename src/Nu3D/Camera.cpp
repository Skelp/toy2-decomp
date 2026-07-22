#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Portal.h"
#include "Nu3D/Viewport.h"
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

		// GLOBAL: TOY2 0x00E4D880
		CameraData g_activeCamera;

		// GLOBAL: TOY2 0x00B623FC
		CameraData* g_currentCamera;

		// GLOBAL: TOY2 0x00B223C8
		ActiveCameraTransform g_activeCameraTransform;

		// GLOBAL: TOY2 0x00A4C410
		int32_t g_effectMode;

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

		// FUNCTION: TOY2 0x004BA420
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

		// FUNCTION: TOY2 0x004BACF0
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

		// FUNCTION: TOY2 0x004BBAB0
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

		// FUNCTION: TOY2 0x004BC080
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

				float depth = (projectionOffset + output->z) / projectionZ;
				output->z = depth;
				output->x = depth * output->x / projectionX;
				output->y = depth * output->y / projectionY;

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

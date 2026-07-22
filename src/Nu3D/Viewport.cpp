#include "Nu3D/Viewport.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "SoftwareRenderer.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

#include <STDIO.H>

namespace Nu3D
{
	namespace Viewport
	{
		// GLOBAL: TOY2 0x00884640
		D3DVIEWPORT2 g_pendingViewport;

		// GLOBAL: TOY2 0x00884680
		D3DVIEWPORT2 g_currentViewport;

		// GLOBAL: TOY2 0x00884638
		int32_t g_renderWidth;

		// GLOBAL: TOY2 0x00884634
		int32_t g_renderHeight;

		// GLOBAL: TOY2 0x008846AC
		float g_clipOffsetX;

		// GLOBAL: TOY2 0x008846B0
		float g_clipOffsetY;

		// GLOBAL: TOY2 0x00884630
		int32_t g_clipNormMatrixDirty;

		// GLOBAL: TOY2 0x008846B4
		int32_t g_screenSpaceMatrixDirty;

		// GLOBAL: TOY2 0x00884670
		float g_currentViewportX;

		// GLOBAL: TOY2 0x00884674
		float g_currentViewportY;

		// GLOBAL: TOY2 0x008845A0
		float g_minViewportY;

		// GLOBAL: TOY2 0x008845A4
		float g_minViewportX;

		// GLOBAL: TOY2 0x00884678
		float g_currentViewportWidth;

		// GLOBAL: TOY2 0x0088466C
		float g_currentViewportHeight;

		// GLOBAL: TOY2 0x00E4D958
		int32_t g_viewportChangeCount;

		// GLOBAL: TOY2 0x008845A8
		float g_drawDeviceWidth;

		// GLOBAL: TOY2 0x008845AC
		float g_drawDeviceHeight;

		// GLOBAL: TOY2 0x008845B0
		D3DMATRIX g_clipNormMatrix;

		// GLOBAL: TOY2 0x008845F0
		D3DMATRIX g_screenSpaceMatrix;

		// GLOBAL: TOY2 0x009F6030
		Plane g_frustumPlanes[32];

		// GLOBAL: TOY2 0x009F6224
		// This address is part of g_frustumPlanes[31], not a separate global.

		// GLOBAL: TOY2 0x009F6230
		int32_t g_frustumPlaneCount;

		// GLOBAL: TOY2 0x009F6234
		int32_t g_reverseFrustumWinding;

		// GLOBAL: TOY2 0x009F6238
		int32_t g_drawPortalOutlines;

		// GLOBAL: TOY2 0x009F623C
		int32_t g_viewportClippingEnabled;

		// GLOBAL: TOY2 0x00E4D8E8
		ViewportRectAlt g_viewClipRect;

		// FUNCTION: TOY2 0x004B5590
		void GetViewportRect(ViewportRectAlt* output)
		{
			output->left = g_currentViewportX;
			output->top = g_currentViewportY;
			output->right = g_currentViewportX + g_currentViewportWidth - 1.0f;
			output->bottom = g_currentViewportY + g_currentViewportHeight - 1.0f;
		}

		// FUNCTION: TOY2 0x004B55D0
		void Init()
		{
			DrawingDevice::BuildFreshViewport(&g_pendingViewport);
			memcpy(&g_currentViewport, &g_pendingViewport, sizeof(g_currentViewport));

			g_renderWidth = DrawingDevice::GetWidth();
			g_renderHeight = DrawingDevice::GetHeight();
			g_clipOffsetY = 0.0f;
			g_clipOffsetX = 0.0f;
			g_clipNormMatrixDirty = 1;
			g_screenSpaceMatrixDirty = 1;
		}

		// FUNCTION: TOY2 0x004B5630
		void Reset()
		{
			DrawingDevice::SetViewport(&g_currentViewport);
			memcpy(&g_pendingViewport, &g_currentViewport, sizeof(g_pendingViewport));

			g_currentViewportX = (float)g_currentViewport.dwX;
			g_currentViewportY = (float)g_currentViewport.dwY;
			g_minViewportY = 0.0f;
			g_currentViewportWidth = (float)g_currentViewport.dwWidth;
			g_minViewportX = 0.0f;
			g_clipOffsetY = 0.0f;
			g_clipOffsetX = 0.0f;
			g_clipNormMatrixDirty = 1;
			g_currentViewportHeight = (float)g_currentViewport.dwHeight;
			g_screenSpaceMatrixDirty = 1;
			g_viewportChangeCount = 0;
			g_drawDeviceWidth = (float)g_renderWidth;
			g_drawDeviceHeight = (float)g_renderHeight;
		}

		// FUNCTION: TOY2 0x004B5700
		void SetClipRect(float left, float top, float right, float bottom)
		{
			float width = right - left + 1.0f;
			float height = bottom - top + 1.0f;

			if (left == g_currentViewportX && top == g_currentViewportY && width == g_currentViewportWidth && height == g_currentViewportHeight)
				return;

			if (left < g_minViewportX)
				left = g_minViewportX;
			if (top < g_minViewportY)
				top = g_minViewportY;
			if (width > g_drawDeviceWidth - 1.0f)
				width = g_drawDeviceWidth - 1.0f;
			if (height > g_drawDeviceHeight - 1.0f)
				height = g_drawDeviceHeight - 1.0f;

			if (width < 0.0f || height < 0.0f)
				return;

			g_currentViewportX = left;
			g_currentViewportY = top;
			g_currentViewportWidth = width;
			g_currentViewportHeight = height;

			g_pendingViewport.dwX = (int32_t)left;
			g_pendingViewport.dwY = (int32_t)top;
			g_pendingViewport.dwWidth = (int32_t)width;
			g_pendingViewport.dwHeight = (int32_t)height;
			g_pendingViewport.dvClipX = ((left - g_minViewportX) / g_drawDeviceWidth) * 2.0f - 1.0f - g_clipOffsetX;
			g_pendingViewport.dvClipY = 1.0f - ((top - g_minViewportY) / g_drawDeviceHeight) * 2.0f - g_clipOffsetY;
			g_pendingViewport.dvClipWidth = (width / g_drawDeviceWidth) * 2.0f;
			g_pendingViewport.dvClipHeight = (height / g_drawDeviceHeight) * 2.0f;

			DrawingDevice::SetViewport(&g_pendingViewport);
			g_clipNormMatrixDirty = 1;
			g_screenSpaceMatrixDirty = 1;
			++g_viewportChangeCount;
		}

		// FUNCTION: TOY2 0x004B5900
		void SetViewportRect(float left, float top, float right, float bottom, float centreX, float centreY)
		{
			float width = right - left + 1.0f;
			float height = bottom - top + 1.0f;

			g_minViewportX = left;
			g_minViewportY = top;
			g_currentViewportX = left;
			g_currentViewportY = top;
			g_currentViewportWidth = width;
			g_currentViewportHeight = height;
			g_drawDeviceWidth = width;
			g_drawDeviceHeight = height;
			g_clipOffsetX = centreX;
			g_clipOffsetY = centreY;

			g_pendingViewport.dwX = (int32_t)left;
			g_pendingViewport.dwY = (int32_t)top;
			g_pendingViewport.dwWidth = (int32_t)width;
			g_pendingViewport.dwHeight = (int32_t)height;
			g_pendingViewport.dvClipX = -1.0f - centreX;
			g_pendingViewport.dvClipY = 1.0f - centreY;
			g_pendingViewport.dvClipWidth = 2.0f;
			g_pendingViewport.dvClipHeight = 2.0f;

			DrawingDevice::SetViewport(&g_pendingViewport);
			g_clipNormMatrixDirty = 1;
			g_screenSpaceMatrixDirty = 1;
		}

		// FUNCTION: TOY2 0x004B5A00
		void SetZRange(float minZ, float maxZ)
		{
			g_pendingViewport.dvMinZ = minZ;
			g_pendingViewport.dvMaxZ = maxZ;
			DrawingDevice::SetViewport(&g_pendingViewport);
			g_clipNormMatrixDirty = 1;
			g_screenSpaceMatrixDirty = 1;
		}

		// FUNCTION: TOY2 0x004B5A30
		void SetCentre(float centreX, float centreY)
		{
			float left = g_currentViewportX;
			float top = g_currentViewportY;

			g_clipOffsetX = centreX;
			g_clipOffsetY = centreY;

			// Force SetClipRect past its unchanged-rectangle fast path so the new
			// centre offsets are submitted to the drawing device.
			g_currentViewportX += 1.0f;
			SetClipRect(left, top, left + g_currentViewportWidth - 1.0f, top + g_currentViewportHeight - 1.0f);
		}

		// FUNCTION: TOY2 0x004B5AA0
		void GetClipNormMatrix(D3DMATRIX* output)
		{
			if (g_clipNormMatrixDirty)
				BuildClipNormMatrix();
			memcpy(output, &g_clipNormMatrix, sizeof(*output));
		}

		// FUNCTION: TOY2 0x004B5AD0
		void BuildClipNormMatrix()
		{
			float depth = g_pendingViewport.dvMaxZ - g_pendingViewport.dvMinZ;

			memset(&g_clipNormMatrix, 0, sizeof(g_clipNormMatrix));
			g_clipNormMatrix._11 = 2.0f / g_pendingViewport.dvClipWidth;
			g_clipNormMatrix._22 = 2.0f / g_pendingViewport.dvClipHeight;
			g_clipNormMatrix._33 = 1.0f / depth;
			g_clipNormMatrix._41 = -1.0f - (2.0f * g_pendingViewport.dvClipX) / g_pendingViewport.dvClipWidth;
			g_clipNormMatrix._42 = 1.0f - (2.0f * g_pendingViewport.dvClipY) / g_pendingViewport.dvClipHeight;
			g_clipNormMatrix._43 = -g_pendingViewport.dvMinZ / depth;
			g_clipNormMatrix._44 = 1.0f;
			g_clipNormMatrixDirty = 0;
		}

		// FUNCTION: TOY2 0x004B5BD0
		void GetScreenSpaceMatrix(D3DMATRIX* output)
		{
			if (g_screenSpaceMatrixDirty)
				BuildScreenSpaceMatrix();
			memcpy(output, &g_screenSpaceMatrix, sizeof(*output));
		}

		// FUNCTION: TOY2 0x004B5C00
		void BuildScreenSpaceMatrix()
		{
			float width = (float)g_pendingViewport.dwWidth;
			float height = (float)g_pendingViewport.dwHeight;

			memset(&g_screenSpaceMatrix, 0, sizeof(g_screenSpaceMatrix));
			g_screenSpaceMatrix._11 = width * 0.5f;
			g_screenSpaceMatrix._22 = height * -0.5f;
			g_screenSpaceMatrix._33 = 1.0f;
			g_screenSpaceMatrix._41 = (float)g_pendingViewport.dwX + width * 0.5f;
			g_screenSpaceMatrix._42 = (float)g_pendingViewport.dwY + height * 0.5f;
			g_screenSpaceMatrix._44 = 1.0f;
			g_screenSpaceMatrixDirty = 0;
		}

		// FUNCTION: TOY2 0x004BA3A0
		void SetViewClipRect()
		{
			ViewportRectAlt viewportRect;

			if (Renderer::g_isSoftwareRendering)
			{
				g_viewClipRect.top = (float)SoftwareRenderer::g_topOffset;
				g_viewClipRect.left = (float)SoftwareRenderer::g_leftOffset;
				g_viewClipRect.bottom = (float)SoftwareRenderer::g_bottomOffset;
				g_viewClipRect.right = (float)SoftwareRenderer::g_rightOffset;
			}
			else
			{
				GetViewportRect(&viewportRect);
				g_viewClipRect.top = viewportRect.left;
				g_viewClipRect.left = viewportRect.top;
				g_viewClipRect.bottom = viewportRect.right;
				g_viewClipRect.right = viewportRect.bottom;
			}
		}

		// FUNCTION: TOY2 0x004BABE0
		void CacheViewport(ViewportCache* cache)
		{
			memcpy(cache->frustumPlanes, g_frustumPlanes, sizeof(cache->frustumPlanes));
			cache->clipTop = g_viewClipRect.top;
			cache->clipLeft = g_viewClipRect.bottom;
			cache->clipBottom = g_viewClipRect.left;
			cache->clipRight = g_viewClipRect.right;
			cache->frustumPlaneCount = g_frustumPlaneCount;
		}

		// FUNCTION: TOY2 0x004BAC40
		void GetViewClipRect(ViewportRect* output)
		{
			output->top = g_viewClipRect.top;
			output->bottom = g_viewClipRect.left;
			output->left = g_viewClipRect.bottom;
			output->right = g_viewClipRect.right;
		}

		// FUNCTION: TOY2 0x004BAC70
		void RestoreViewportCache(const ViewportCache* cache)
		{
			memcpy(g_frustumPlanes, cache->frustumPlanes, sizeof(cache->frustumPlanes));
			g_viewClipRect.top = cache->clipTop;
			g_viewClipRect.bottom = cache->clipLeft;
			g_viewClipRect.left = cache->clipBottom;
			g_viewClipRect.right = cache->clipRight;
			g_frustumPlaneCount = cache->frustumPlaneCount;

			if (g_viewportClippingEnabled)
			{
				SetClipRect(g_viewClipRect.bottom, g_viewClipRect.top, g_viewClipRect.right, g_viewClipRect.left);
			}

			Camera::RebuildTransformPipeline();
		}
	}

	namespace Camera
	{
		// GLOBAL: TOY2 0x00A4C190
		D3DMATRIX g_clipToScreenMatrix;

		// GLOBAL: TOY2 0x00A4C1D0
		D3DMATRIX g_screenToWorldMatrix;

		// GLOBAL: TOY2 0x00A4C210
		D3DMATRIX g_screenToClipMatrix;

		// GLOBAL: TOY2 0x00A4C250
		D3DMATRIX g_screenViewProjectionMatrix;

		// GLOBAL: TOY2 0x00A4C290
		D3DMATRIX g_projectionMatrix;

		// GLOBAL: TOY2 0x00A4C2D0
		D3DMATRIX g_clipNormMatrix;

		// GLOBAL: TOY2 0x00A4C310
		D3DMATRIX g_screenSpaceMatrix;

		// GLOBAL: TOY2 0x00A4C350
		D3DMATRIX g_normalizedViewProjectionMatrix;

		// GLOBAL: TOY2 0x00A4C390
		D3DMATRIX g_viewProjectionMatrix;

		// GLOBAL: TOY2 0x00A4C3D0
		D3DMATRIX g_viewMatrix;

		// GLOBAL: TOY2 0x00E4D7E0
		ReflectionState g_reflectionState;

		// FUNCTION: TOY2 0x004BBA10 [MATCHED]
		void RebuildTransformPipeline()
		{
			Viewport::GetClipNormMatrix(&g_clipNormMatrix);
			Viewport::GetScreenSpaceMatrix(&g_screenSpaceMatrix);
			Math::FullMatrixMultiply(&g_viewProjectionMatrix, &g_viewMatrix, &g_projectionMatrix);
			Math::FullMatrixMultiply(&g_normalizedViewProjectionMatrix, &g_viewProjectionMatrix, &g_clipNormMatrix);
			Math::FullMatrixMultiply(&g_screenViewProjectionMatrix, &g_normalizedViewProjectionMatrix, &g_screenSpaceMatrix);
			Math::FullMatrixMultiply(&g_clipToScreenMatrix, &g_clipNormMatrix, &g_screenSpaceMatrix);
			Math::InvertAffineMatrix(&g_screenToClipMatrix, &g_clipToScreenMatrix);
			Math::InvertAffineMatrix(&g_screenToWorldMatrix, &g_screenViewProjectionMatrix);
			memcpy(&g_reflectionState.uvTransform, &g_normalizedViewProjectionMatrix, sizeof(g_reflectionState.uvTransform));
		}
	}
}

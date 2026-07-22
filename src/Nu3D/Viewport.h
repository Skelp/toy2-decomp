#pragma once

#include "Common.h"
#include "Numerics.h"
#include <directx6/d3d.h>

namespace Nu3D
{
	namespace Viewport
	{
		struct ViewportRect
		{
			float top;
			float bottom;
			float left;
			float right;
		};

		struct ViewportRectAlt
		{
			float left;
			float top;
			float right;
			float bottom;
		};

		struct ViewportCache
		{
			Plane frustumPlanes[32];
			float clipTop;
			float clipLeft;
			float clipBottom;
			float clipRight;
			int32_t frustumPlaneCount;
		};

		void GetViewportRect(ViewportRectAlt* output);
		void Init();
		void Reset();
		void SetClipRect(float left, float top, float right, float bottom);
		void SetViewportRect(float left, float top, float right, float bottom, float centreX, float centreY);
		void SetZRange(float minZ, float maxZ);
		void SetCentre(float centreX, float centreY);
		void GetClipNormMatrix(D3DMATRIX* output);
		void BuildClipNormMatrix();
		void GetScreenSpaceMatrix(D3DMATRIX* output);
		void BuildScreenSpaceMatrix();
		void SetViewClipRect();
		void CacheViewport(ViewportCache* cache);
		void GetViewClipRect(ViewportRect* output);
		void RestoreViewportCache(const ViewportCache* cache);
	}

	STATIC_ASSERT(sizeof(Viewport::ViewportRect) == 0x10);
	STATIC_ASSERT(sizeof(Viewport::ViewportCache) == 0x214);
}

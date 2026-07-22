#pragma once

#include "Common.h"
#include "Numerics.h"
#include <directx6/d3d.h>

namespace Nu3D
{
	struct LightColor
	{
		float b;
		float g;
		float r;
		float a;
	};

	struct Light
	{
		enum Type
		{
			TYPE_AMBIENT,
			TYPE_POINT,
			TYPE_DIRECTIONAL,
			TYPE_SPOT
		};

		D3DMATRIX transform;
		LightColor color;
		Type type;
		float range;
		int32_t theta;
		int32_t phi;
		int32_t enabled;
		Light* previous;
		Light* next;
		LPDIRECT3DLIGHT direct3DLight;
		D3DLIGHT2 direct3DDescription;

		static void Destroy(Light* light);
		static void Free(Light* light);
		static Light* BuildAmbient(const LightColor* color, int32_t enabled);
		static Light* Alloc();
		static Light* BuildPoint(const D3DMATRIX* transform, const LightColor* color, float range, int32_t enabled);
		static Light* BuildDirectional(const D3DMATRIX* transform, const LightColor* color, int32_t enabled);
		static Light* BuildSpot(const D3DMATRIX* transform, const LightColor* color, float range,
			int32_t theta, int32_t phi, int32_t enabled);
		static void Update(Light* light);
		static int32_t InitPool(int32_t poolSize);
		static void DestroyAllLights();
		static void SetDirectionalLight(int32_t directionX, int32_t directionY, int32_t directionZ,
			int32_t red, int32_t green, int32_t blue);
		static void BuildGlobalLights();
		static void Cleanup();
	};

	STATIC_ASSERT(sizeof(LightColor) == 0x10);
	STATIC_ASSERT(sizeof(Light) == 0xC0);
}

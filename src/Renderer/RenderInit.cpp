#include "Renderer/Renderer.h"
#include "Renderer/RendererInternal.h"
#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "Nu3D/Light.h"
#include "Nu3D/Font.h"
#include "Nu3D/Camera.h"
#include "Nu3D/BmpDataNode.h"
#include "Nu3D/Material.h"
#include "Nu3D/Math.h"
#include "NGNLoader/NGNLoader.h"
#include "Nu3D/Patch.h"
#include "Nu3D/Primitive.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Scene.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Toy2/Toy2.h"
#include "Toy2/Weather.h"
#include "Renderer/Glue.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Direct6.h"
#include "Logger.h"
#include <cstdarg>
#include <cstring>
#include <cstdio>

// The renderer set-up, the gamma table and the shutdown: retail holds 0x004B3630
// through 0x004B3870 as one object, in the order of this file.
namespace Renderer
{
	// GLOBAL: TOY2 0x00E4D964
	LPDIRECT3D3 g_drawDeviceD3D;

	// GLOBAL: TOY2 0x005084F8
	int32_t g_gammaFixedPoint = 0x10000;

	// GLOBAL: TOY2 0x0050851C
	uint8_t g_gammaLUT[256] = {
		// clang-format off
		0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
		16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
		32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
		64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
		80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
		96,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
		112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
		128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
		144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
		160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
		176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
		192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
		208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
		224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
		240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
		// clang-format on
	};

	// GLOBAL: TOY2 0x00884488
	int32_t g_rendererValid;

	// FUNCTION: TOY2 0x004B3630 [PROVISIONAL]
	void Init()
	{
		if (g_rendererValid)
			Cleanup();

		g_drawDeviceD3D = DrawingDevice::GetD3D();
		g_drawDeviceD3DDevice = DrawingDevice::GetD3DDevice();

		Nu3D::g_primListHead = 0;
		Nu3D::g_patchListHead = 0;

		Nu3D::SetIsSoftwareRendering(g_isSoftwareRendering);
		Nu3D::SetMinTexSize(g_isSoftwareRendering != 0 ? 256 : 0);

		Nu3D::Viewport::Init();
		Nu3D::Light::InitPool(16);
		NGNLoader::Init();
		Nu3D::Material::Init();

		InitResources();
		g_rendererValid = 1;
		Nu3D::Font::BuildFontTextures();
		DisableFog();

		g_deviceBlendShadeCaps = 0;

		D3DDEVICEDESC outSurfaceDesc;
		memcpy(&outSurfaceDesc, DrawingDevice::CopySurfaceDesc(&outSurfaceDesc), sizeof(outSurfaceDesc));

		if ((outSurfaceDesc.dpcTriCaps.dwShadeCaps & D3DPSHADECAPS_ALPHAGOURAUDBLEND) != 0)
			g_deviceBlendShadeCaps |= 1;

		if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & D3DPBLENDCAPS_ONE) != 0)
			g_deviceBlendShadeCaps |= 2;

		if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & D3DPBLENDCAPS_INVSRCCOLOR) != 0)
			g_deviceBlendShadeCaps |= 4;

		if ((outSurfaceDesc.dpcTriCaps.dwTextureBlendCaps & D3DPTEXTURECAPS_TRANSPARENCY) != 0)
			g_deviceBlendShadeCaps |= 8;
	}

	// FUNCTION: TOY2 0x004B3740 [MATCHED]
	float BuildGammaCorrectionLUT(float gammaCorrection)
	{
		float result = g_gammaCorrection;

		g_gammaCorrection = gammaCorrection;
		g_gammaFixedPoint = (gammaCorrection * 65536.0f);

		int32_t curGammaIdx = 0;
		int32_t count = 0;
		int32_t step = g_gammaFixedPoint;

		do
		{
			int32_t outputValue;

			if ((int32_t)(count & 0xFFFF0000) > 0xFF0000)
				outputValue = 0xFF;
			else
				outputValue = count >> 16;

			g_gammaLUT[curGammaIdx++] = outputValue;
			count += step;

		} while (curGammaIdx < 256);

		return result;
	}

	// FUNCTION: TOY2 0x004B37B0 [MATCHED]
	RGBA ApplyGammaCorrection(RGBA color)
	{
		color.r = g_gammaLUT[color.r];
		color.g = g_gammaLUT[color.g];
		color.b = g_gammaLUT[color.b];

		return color;
	}

	// FUNCTION: TOY2 0x004B37F0 [MATCHED]
	void Cleanup()
	{
		while (Nu3D::g_patchListHead != NULL)
		{
			Nu3D::g_patchListHead->listNext = NULL;
			Nu3D::Patch::Destroy(Nu3D::g_patchListHead);
		}
		while (Nu3D::g_primListHead != NULL)
		{
			Nu3D::g_primListHead->listNext = NULL;
			Nu3D::Primitive::Destroy(Nu3D::g_primListHead);
		}
		Nu3D::Material::Close();
		NGNLoader::ReleaseAllTextures();
		Nu3D::Light::DestroyAllLights();
		g_rendererValid = 0;
	}

	// FUNCTION: TOY2 0x004B3860 [MATCHED]
	void SetIsSoftwareRendering(int32_t value) { g_isSoftwareRendering = value; }

	// FUNCTION: TOY2 0x004B3870 [MATCHED]
	int32_t GetIsSoftwareRendering() { return g_isSoftwareRendering; }
}

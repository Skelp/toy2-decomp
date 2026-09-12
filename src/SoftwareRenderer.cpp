#include "SoftwareRenderer.h"
#include "SoftwareRendererInternal.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Camera.h"
#include "Toy2/Toy2.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Toy2/Direct6.h"
#include "Toy2/Weather.h"
#include "Nu3D/BmpDataNode.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Logger.h"
#include <stdlib.h>
#include <math.h>

// The state that every rasterizer of the software renderer shares, the three
// dispatch tables that name the rasterizers of each pixel format, and the five
// small entry points that each lie in a retail object of their own.
namespace SoftwareRenderer
{
	// GLOBAL: TOY2 0x00559C40
	int32_t g_skipOddSoftwareFrames;

	// GLOBAL: TOY2 0x00839278
	int32_t g_displayMaxX;

	// Active base of the 4096 software-render depth buckets.
	// GLOBAL: TOY2 0x0087E50C
	SoftwareRenderItem* g_softwareRenderBucketStorage[4096];

	// GLOBAL: TOY2 0x00839280
	int32_t g_softwareRenderItemCount;

	// GLOBAL: TOY2 0x008393E0
	ScanlineScratch g_scanlineScratch[1024];

	// GLOBAL: TOY2 0x00A4CC74
	int32_t g_levelFileIndex;

	// GLOBAL: TOY2 0x0084D0E8
	int32_t g_softwareRendererBufferBlockCount;

	// GLOBAL: TOY2 0x00882900
	int32_t g_backBufferPitchBytes;

	// GLOBAL: TOY2 0x00882908
	int32_t g_softWindowScaleX;

	// GLOBAL: TOY2 0x0088290C
	int32_t g_softWindowScaleY;

	// When this flag is set, polygon edge setup skips odd-numbered scanlines.
	// GLOBAL: TOY2 0x008828B0
	int32_t g_skipOddScanlines;

	// GLOBAL: TOY2 0x0088273C
	int32_t g_unusedSoftwareRendererConfigA;

	// GLOBAL: TOY2 0x0083917C
	int32_t g_unusedSoftwareRendererConfigB;

	// GLOBAL: TOY2 0x0084D0F4
	int32_t g_unusedSoftwareRendererConfigC;

	// GLOBAL: TOY2 0x00500C00
	int32_t g_unusedSoftwareRendererConfigD;

	// GLOBAL: TOY2 0x008827A0
	void* g_softwareTextureData[64];

	// GLOBAL: TOY2 0x004FC880
	uint8_t g_defaultSoftwarePalette[768] = {
#include "SoftwareRendererDefaultPalette.inc"
	};

	// GLOBAL: TOY2 0x004FCB80
	SoftwareRenderDispatchTable g_softwareRenderDispatch555 = {
		RasterizeTexturedRect555,
		RasterizeSolidQuad16,
		RasterizeAdditiveTexturedPolygon555,
		RasterizeSubtractiveTexturedPolygon555,
		{ UnkRenderAPI5, UnkRenderAPI6, UnkRenderAPI7, UnkRenderAPI8 },
		{ RasterizeTexturedPolygon555, RasterizeBlend25TexturedPolygon555, RasterizeBlend50TexturedPolygon555, RasterizeBlend75TexturedPolygon555 },
		UnkRenderAPI13,
	};

	// GLOBAL: TOY2 0x004FCBB8
	SoftwareRenderDispatchTable g_softwareRenderDispatch565 = {
		RasterizeTexturedRect565,
		RasterizeSolidQuad16,
		RasterizeAdditiveTexturedPolygon565,
		RasterizeSubtractiveTexturedPolygon565,
		{ UnkRenderAPI17, UnkRenderAPI18, UnkRenderAPI19, UnkRenderAPI20 },
		{ RasterizeTexturedPolygon565, RasterizeBlend25TexturedPolygon565, RasterizeBlend50TexturedPolygon565, UnkRenderAPI24 },
		UnkRenderAPI25,
	};

	// GLOBAL: TOY2 0x004FCBF0
	SoftwareRenderDispatchTable g_softwareRenderDispatchPalettized = {
		RasterizeTexturedRect8,
		RasterizeSolidQuad8,
		RasterizeAdditiveTexturedPolygon8,
		RasterizeSubtractiveTexturedPolygon8,
		{ UnkRenderAPI30, UnkRenderAPI31, UnkRenderAPI31, UnkRenderAPI31 },
		{ RasterizeTexturedPolygon8, UnkRenderAPI33, UnkRenderAPI33, UnkRenderAPI33 },
		UnkRenderAPI34,
	};

	// GLOBAL: TOY2 0x00704E68
	SoftwareRenderDispatchTable* g_softwareRenderDispatch;

	// Render-buffer double-buffering state. SwapRenderBuffer toggles
	// g_currentRenderBuffer between the two contiguous render buffers
	// (g_renderBufferB follows g_renderBufferA at +0x3C420) and derives
	// g_renderBufferPixels at a fixed +0x4020 offset into the active buffer.
	// GLOBAL: TOY2 0x00559DF8
	uint8_t* g_currentRenderBuffer;

	// GLOBAL: TOY2 0x0055A118
	uint8_t* g_renderBufferPixels;

	// GLOBAL: TOY2 0x0055A148
	uint8_t g_renderBufferA[0x3C420];

	// GLOBAL: TOY2 0x00596568
	uint8_t g_renderBufferB[0x3C420];

	// FUNCTION: TOY2 0x00452130 [MATCHED]
	void SwapRenderBuffer()
	{
		Toy2::MainMenu::g_menuClearColor.b = 0;
		Toy2::MainMenu::g_menuClearColor.g = 0;
		Toy2::MainMenu::g_menuClearColor.r = 0;
		g_currentRenderBuffer = (g_currentRenderBuffer == g_renderBufferA) ? g_renderBufferB : g_renderBufferA;
		g_renderBufferPixels = g_currentRenderBuffer + 0x4020;
	}

	// FUNCTION: TOY2 0x004AC1F0 [MATCHED]
	int32_t GetCurrentTextureData(TextureData* out)
	{
		Nu3D::BmpDataNode* node = Nu3D::g_currentBmpDataNode;
		if (node != NULL)
		{
			out->texData = node->texData;
			out->surfaceDesc = &node->surfaceDesc;
			return 0;
		}
		return 1;
	}

	// FUNCTION: TOY2 0x004BC430 [MATCHED]
	void GetRenderDistances(float* primaryDistance, float* secondaryDistance)
	{
		if (primaryDistance != NULL)
		{
			*primaryDistance = sqrt(Renderer::g_primaryRenderDistanceSquared);
		}
		if (secondaryDistance != NULL)
		{
			*secondaryDistance = sqrt(Renderer::g_secondaryRenderDistanceSquared);
		}
	}

	// FUNCTION: TOY2 0x004C20E0 [MATCHED]
	void SetLevelFileIndex(int32_t index) { g_levelFileIndex = index; }
}

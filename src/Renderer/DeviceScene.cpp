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

// The scene and the fog of the drawing device: retail holds 0x004B2BF0 through
// 0x004B2DE0 as one object, in the order of this file.
namespace DrawingAPI
{
	// FUNCTION: TOY2 0x004B2BF0 [MATCHED]
	void SetVertexAPIs(int32_t isSoftwareRendering)
	{
		if (isSoftwareRendering)
		{
			ReleaseVertexBuffer = SoftwareDevice::ReleaseVertexBuffer;
			CreateVertexBuffer = SoftwareDevice::CreateVertexBuffer;
			LockVertexBuffer = SoftwareDevice::LockVertexBuffer;
			UnlockVertexBuffer = SoftwareDevice::UnlockVertexBuffer;
			OptimizeVertexBuffer = SoftwareDevice::OptimizeVertexBuffer;
			ProcessVerticesOnBuffer = SoftwareDevice::ProcessVerticesOnBuffer;
		}
		else
		{
			ReleaseVertexBuffer = HardwareDevice::ReleaseVertexBuffer;
			CreateVertexBuffer = HardwareDevice::CreateVertexBuffer;
			LockVertexBuffer = HardwareDevice::LockVertexBuffer;
			UnlockVertexBuffer = HardwareDevice::UnlockVertexBuffer;
			OptimizeVertexBuffer = HardwareDevice::OptimizeVertexBuffer;
			ProcessVerticesOnBuffer = HardwareDevice::ProcessVerticesOnBuffer;
		}
	}

}

namespace Renderer
{
	void ApplyFogSettings();

	// GLOBAL: TOY2 0x00E4D95C
	int32_t g_deviceBlendShadeCaps;

	// GLOBAL: TOY2 0x00884478
	DWORD g_fogColor;

	// GLOBAL: TOY2 0x0088448C
	int32_t g_fogEnabled;

	// GLOBAL: TOY2 0x00508518
	float g_fogEnd = 100.0;

	// GLOBAL: TOY2 0x00508514
	float g_fogStart = 1.0;

	// FUNCTION: TOY2 0x004B2C80 [EFFECTIVE]
	void ClearScreen(RGBA clearColor, int32_t clearFlags)
	{
		RGBA color = ApplyGammaCorrection(clearColor);

		if (! g_isSoftwareRendering)
		{
			DrawingDevice::ClearScreen(clearFlags, color.value);
			return;
		}

		uint16_t convertedColor = Renderer::ConvertRGBATo16Bit(color);
		SoftwareRenderer::g_softwareClearColor = (convertedColor << 16) | convertedColor;
	}

	// FUNCTION: TOY2 0x004B2CC0 [MATCHED]
	void GetBlendShadeCaps(int32_t* capsOut)
	{
		if (capsOut)
			*capsOut = Renderer::g_deviceBlendShadeCaps;
	}

	// FUNCTION: TOY2 0x004B2CE0 [MATCHED]
	void DisableFog() { g_fogEnabled = 0; }

	// FUNCTION: TOY2 0x004B2CF0 [MATCHED]
	void ConfigureFog(float start, float end, RGBA color)
	{
		g_fogEnabled = 1;
		g_fogStart = start;
		g_fogEnd = end;
		g_fogColor = ApplyGammaCorrection(color).value;
	}

	// FUNCTION: TOY2 0x004B2D20 [MATCHED]
	void SetFogEnable(int32_t enable)
	{
		if (g_fogEnabled)
		{
			if (enable)
				DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
			else
				DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
		}
	}

	// FUNCTION: TOY2 0x004B2D50 [MATCHED]
	int32_t BeginScene()
	{
		if (! DrawingDevice::BeginScene())
		{
			Renderer::ApplyFogSettings();
			Nu3D::Viewport::Reset();
			Nu3D::Viewport::SetViewClipRect();

			if (g_isSoftwareRendering)
				SoftwareRenderer::ResetRenderCommands();

			return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004B2D80 [MATCHED]
	void ApplyFogSettings()
	{
		if (g_fogEnabled)
		{
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGCOLOR, g_fogColor & 0xFFFFFF);

			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGMODE, 3);
			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGSTART, *(DWORD*)&g_fogStart);
			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGEND, *(DWORD*)&g_fogEnd);
		}
		else
		{
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
		}
	}

	// FUNCTION: TOY2 0x004B2DE0 [MATCHED]
	void EndScene(int32_t presentFrame)
	{
		DrawingDevice::EndScene();

		if (presentFrame)
		{
			if (g_isSoftwareRendering)
				SoftwareRenderer::PresentFrame();
			else
				DrawingDevice::PresentFrameAndRestore();
		}
	}
}

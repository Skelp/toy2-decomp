#include "Toy2/Toy2.h"
#include "Toy2/Gadget.h"
#include "Toy2/Ini.h"
#include "Toy2/Screens.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Toy2/Direct6.h"
#include "Toy2/Win95.h"
#include "Logger.h"
#include "FileUtils.h"
#include "InputManager.h"
#include "DrawingDevice.h"
#include "ModeSelect.h"
#include "Nullsub.h"
#include "SoftwareRenderer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Renderer/Glue.h"
#include "Renderer/Shadows.h"
#include "Toy2/KiteTail.h"
#include "SaveManager.h"
#include "Random.h"
#include "Toy2/LevelSelect.h"
#include "Toy2/LevelLogic.h"
#include "Toy2/Buzz.h"
#include "Toy2/Levels.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Actor.h"
#include "Toy2/Animation.h"
#include "Toy2/PoleRecord.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Collectables.h"
#include "Toy2/Weather.h"
#include "Nu3D/Font.h"
#include "Nu3D/FMV.h"
#include "Nu3D/Link.h"
#include "Nu3D/Viewport.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Light.h"
#include "Nu3D/Scene.h"
#include "Nu3D/SoftwareProjectionPoint.h"
#include "Renderer/Renderer.h"
#include "AudioManager/AudioManager.h"
#include "NGNLoader/NGNLoader.h"
#include <WINDOWS.H>
#include <STDIO.H>
#include <STRING.H>
#include <DINPUT.H>
#include <LIMITS.H>
#include <MATH.H>
#include <STDLIB.H>
#include <Numerics.h>
#include "Toy2/Toy2Internal.h"
#include "Toy2/Toy2Internal.h"

// The software window and the software renderer set-up: retail holds
// 0x0047CBA0 through 0x0047D4E0 as one object, in the order of this file.
namespace Toy2
{
	// GLOBAL: TOY2 0x0053CA58
	int32_t g_perspectiveTableHalfWidth;

	// FUNCTION: TOY2 0x0047CBA0 [PROVISIONAL]
	void InitSoftWindow(int32_t width, int32_t height)
	{
		Logger::Log("InitSoftWindow(%i,%i)\n", width, height);
		g_softWindowHeight = height;
		g_softWindowWidth = width;
		g_screenClipTop = (g_destRectHeight - height) / 2;
		g_screenClipBottom = g_screenClipTop + height - 1;
		g_softWindowHalfWidth = g_destRectWidth / 2;
		g_screenClipLeft = (g_destRectWidth - width) / 2;
		g_screenClipRight = g_screenClipLeft + width - 1;
		g_screenClipLeftFixed = g_screenClipLeft << 10;
		g_destRectHalfHeight = g_destRectHeight / 2;
		SoftwareRenderer::g_softWindowScaleX = (width << 7) / 320;
		SoftwareRenderer::g_softWindowScaleY = (height * 128) / 240;
		g_screenClipRightFixed = g_screenClipRight * 1024 + 1023;
		g_destRectWidthScaled = (width * 256) / 320;
	}

	// FUNCTION: TOY2 0x0047CC90 [PROVISIONAL]
	void InitSoftwareRenderer()
	{
		Logger::Log("INITSOFTRENDER : Start.\n");
		int32_t width = d3dappi.szClient.cx;
		int32_t height = d3dappi.szClient.cy;
		g_destRectWidth = width;
		g_destRectHeight = height;
		g_destRectHalfWidth = width / 2;
		Logger::Log("SOFTRENDER : Screen set to %dx%d.\n", width, height);

		if (g_destRectHalfWidth != g_perspectiveTableHalfWidth)
		{
			g_perspectiveTableHalfWidth = g_destRectHalfWidth;
			BuildPerspectiveDivideTable(g_destRectHalfWidth);
		}

		g_softWindowWidth = g_destRectWidth;
		g_screenClipBottom = g_destRectHeight - 1;
		g_screenClipRight = g_destRectWidth - 1;
		g_destRectWidthScaled = (g_destRectWidth << 8) / 320;
		g_screenClipLeft = 0;
		g_screenClipTop = 0;
		g_softWindowHeight = g_destRectHeight;
		g_softWindowHalfWidth = g_destRectWidth / 2;
		g_screenClipLeftFixed = 0;
		g_destRectHalfHeight = g_destRectHeight / 2;
		g_screenClipRightFixed = (g_destRectWidth << 10) - 1;

		SoftwareRenderer::g_unusedSoftwareRendererConfigA = 639;
		SoftwareRenderer::g_displayMaxX = 639;
		SoftwareRenderer::g_unusedSoftwareRendererConfigB = 255;
		SoftwareRenderer::g_unusedSoftwareRendererConfigC = 1023;
		SoftwareRenderer::g_softwareRendererBufferBlockCount = 10240;
		if (SoftwareRenderer::g_softwareRendererBuffer)
			free(SoftwareRenderer::g_softwareRendererBuffer);
		SoftwareRenderer::g_softwareRendererBuffer = malloc(SoftwareRenderer::g_softwareRendererBufferBlockCount << 7);

		DDSURFACEDESC surfaceDesc;
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		HRESULT lockResult;
		do
		{
			lockResult = d3dappi.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (lockResult == DDERR_WASSTILLDRAWING || lockResult == DDERR_SURFACEBUSY);

		if (surfaceDesc.ddpfPixelFormat.dwRBitMask == 0 || surfaceDesc.ddpfPixelFormat.dwGBitMask == 0 || surfaceDesc.ddpfPixelFormat.dwBBitMask == 0)
		{
			SoftwareRenderer::g_redMask = 0;
			SoftwareRenderer::g_greenMask = 0;
			SoftwareRenderer::g_blueMask = 0;
			SoftwareRenderer::g_blueShift = 0;
			SoftwareRenderer::g_redShift = 0;
			SoftwareRenderer::g_greenShift = 0;
			SoftwareRenderer::g_bitsPerPixel = 8;
			SaveManager::SetLightShadowEffects(0);
		}
		else
		{
			SoftwareRenderer::g_redMask = surfaceDesc.ddpfPixelFormat.dwRBitMask;
			SoftwareRenderer::g_greenMask = surfaceDesc.ddpfPixelFormat.dwGBitMask;
			SoftwareRenderer::g_blueMask = surfaceDesc.ddpfPixelFormat.dwBBitMask;

			uint32_t redMask = surfaceDesc.ddpfPixelFormat.dwRBitMask;
			while ((redMask & 1) == 0)
				redMask >>= 1;
			int32_t redBits = 0;
			while (redMask & 1)
			{
				redMask >>= 1;
				++redBits;
			}

			uint32_t greenMask = surfaceDesc.ddpfPixelFormat.dwGBitMask;
			while ((greenMask & 1) == 0)
				greenMask >>= 1;
			SoftwareRenderer::g_greenShift = 0;
			while (greenMask & 1)
			{
				greenMask >>= 1;
				++SoftwareRenderer::g_greenShift;
			}

			SoftwareRenderer::g_blueShift = 0;
			SoftwareRenderer::g_redShift = redBits + SoftwareRenderer::g_greenShift;
			SoftwareRenderer::g_bitsPerPixel = SoftwareRenderer::g_greenShift == 6 ? 16 : 15;
		}

		int32_t pitchPixels = surfaceDesc.lPitch;
		if (SoftwareRenderer::g_bitsPerPixel != 8)
			pitchPixels /= 2;
		SoftwareRenderer::g_backBufferPitchBytes = pitchPixels * 2;
		SoftwareRenderer::g_backBufferPitchPixels = pitchPixels;
		d3dappi.lpBackBuffer->Unlock(NULL);

		Logger::Log("SOFTRENDER : Bit's per pixel set to %d.\n", SoftwareRenderer::g_bitsPerPixel);
		Logger::Log(
			"SOFTRENDER : RGB shifts - R<<%d, G<<%d, B<<%d.\n", SoftwareRenderer::g_redShift, SoftwareRenderer::g_greenShift, SoftwareRenderer::g_blueShift);
		Logger::Log("SOFTRENDER : RGB masks - R&%x, G&%x, B&%x.\n", SoftwareRenderer::g_redMask, SoftwareRenderer::g_greenMask, SoftwareRenderer::g_blueMask);

		switch (SoftwareRenderer::g_bitsPerPixel)
		{
			case 15:
				SoftwareRenderer::g_softwareRenderDispatch = &SoftwareRenderer::g_softwareRenderDispatch555;
				break;
			case 16:
				SoftwareRenderer::g_softwareRenderDispatch = &SoftwareRenderer::g_softwareRenderDispatch565;
				break;
			default:
				SoftwareRenderer::g_softwareRenderDispatch = &SoftwareRenderer::g_softwareRenderDispatchPalettized;
				SoftwareRenderer::SetNewPalette(SoftwareRenderer::g_defaultSoftwarePalette, 0x11111);
				SoftwareRenderer::SetPaletteOnAPI();
				break;
		}

		SoftwareRenderer::g_softwareRenderItemCount = 0;
		memset(SoftwareRenderer::g_softwareRenderBuckets, 0, sizeof(SoftwareRenderer::g_softwareRenderBucketStorage));
		SoftwareRenderer::g_unusedSoftwareRendererConfigD = 4;

		int32_t softHeight = (g_destRectHeight * 8) / 8;
		int32_t softWidth = (g_destRectWidth * 8) / 8;
		InitSoftWindow(softWidth, softHeight);
		SoftwareRenderer::g_pendingBackBufferClears = 2;
		SoftwareRenderer::BuildColourRampTables();
		Logger::Log("INITSOFTRENDER : End.\n");
		Logger::Log("\n");
	}

	// FUNCTION: TOY2 0x0047D4E0 [PROVISIONAL]
	int32_t* BuildPerspectiveDivideTable(int32_t scale)
	{
		g_perspectiveHalfScale = scale >> 1;
		int32_t fixedScale = scale << 12;
		g_perspectiveScaleFixed = fixedScale;

		int32_t divisor = 0x7fff;
		do
		{
			g_perspectiveDivideTable[divisor] = fixedScale / divisor;
		} while (--divisor != 0);

		return g_perspectiveDivideTable;
	}
}

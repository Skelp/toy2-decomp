#include "SoftwareRenderer.h"
#include "SoftwareRendererInternal.h"
#include "Toy2/Toy2.h"
#include "Toy2/Camera.h"
#include "Toy2/Direct6.h"
#include "Toy2/Weather.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Logger.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"

// The backdrop of the software renderer: the colour bands that stand in for a
// sky and a ground, and the scroll state that follows the camera yaw.
//
// Retail band 0x0048FB70-0x00490410.
namespace SoftwareRenderer
{
	struct BackdropBandColour
	{
		int16_t red;
		int16_t green;
		int16_t blue;
		int16_t reserved;
	};

	STATIC_ASSERT(sizeof(BackdropDimensions) == 0xC);
	STATIC_ASSERT(sizeof(BackdropBandColour) == 8);

	// GLOBAL: TOY2 0x004F7400
	PointI g_backdropScrollOverride = { -32768, -32768 };

	// GLOBAL: TOY2 0x004F73A8
	BackdropDimensions g_backdropDimensions = { 256, 128, 32 };

	// GLOBAL: TOY2 0x004F73D4
	BackdropDimensions g_staticBackdropDimensions = { 128, 160, 32 };

	// GLOBAL: TOY2 0x00500A1C
	int32_t g_backdropTextureColumn = 0xFFFFFFFF;

	// The backdrop scroll state follows the camera yaw. The draw setup computes
	// the horizon and view depth; UpdateBackdropScroll consumes and clamps them.
	// GLOBAL: TOY2 0x0072EFC0
	int32_t g_previousBackdropYaw;

	// GLOBAL: TOY2 0x00731DF0
	int32_t g_backdropYawAccumulator;

	// GLOBAL: TOY2 0x00731D6C
	int32_t g_backdropScrollX;

	// GLOBAL: TOY2 0x00731C14
	int32_t g_backdropHorizon;

	// GLOBAL: TOY2 0x00731C18
	int32_t g_backdropViewDepth;

	// GLOBAL: TOY2 0x00731C10
	int32_t g_backdropViewX;

	// GLOBAL: TOY2 0x0072EB58
	D3DRECT clr[2];

	// GLOBAL: TOY2 0x0072EF80
	BackdropBandColour g_backdropBandColours[2];

	// GLOBAL: TOY2 0x00830C24
	int32_t g_backdropBandCount;

	// GLOBAL: TOY2 0x0055A124
	int32_t g_backdropClearEnabled;

	// GLOBAL: TOY2 0x00731CCC
	uint32_t g_skyFillColourPair;

	// GLOBAL: TOY2 0x00830C18
	uint32_t g_groundFillColourPair;

	// GLOBAL: TOY2 0x00731EFC
	uint32_t g_redMask;

	// GLOBAL: TOY2 0x00830BCC
	uint32_t g_greenMask;

	// GLOBAL: TOY2 0x00731EF8
	uint32_t g_blueMask;

	// GLOBAL: TOY2 0x00732FB4
	int32_t g_redShift;

	// GLOBAL: TOY2 0x00731F20
	int32_t g_greenShift;

	// GLOBAL: TOY2 0x00731F14
	int32_t g_blueShift;

	// FUNCTION: TOY2 0x00490410 [MATCHED]
	void SetBackdropScrollOverride(int32_t x, int32_t y)
	{
		BackdropDimensions* dimensions = Toy2::g_hasStaticBackdrop ? &g_staticBackdropDimensions : &g_backdropDimensions;
		int32_t pitch = dimensions->width;
		int32_t quotient = x / pitch;
		int32_t remainder = x - quotient * pitch;
		if (remainder < 0)
		{
			g_backdropScrollOverride.x = (1 - quotient) * pitch + x;
			g_backdropScrollOverride.y = y;
			return;
		}
		g_backdropScrollOverride.x = remainder;
		g_backdropScrollOverride.y = y;
	}

	// FUNCTION: TOY2 0x0048FB70 [PROVISIONAL]
	void RenderBackdropColourBands()
	{
		g_backdropBandCount = 0;
		if (Toy2::g_hasStaticBackdrop + Toy2::g_hasBackdrop == 0)
		{
			if (g_backdropClearEnabled == 0)
				return;

			g_backdropBandColours[0].red = 0;
			g_backdropBandColours[0].green = 0;
			g_backdropBandColours[0].blue = 0;
			clr[0].x1 = 0;
			clr[0].y1 = 0;
			clr[0].x2 = 0x200;
			clr[0].y2 = 0x100;
			g_backdropBandCount = 1;
		}
		else
		{
			BackdropDimensions* dimensions = Toy2::g_hasStaticBackdrop ? &g_staticBackdropDimensions : &g_backdropDimensions;
			int32_t yaw = (int16_t)Toy2::Camera::g_renderCameraTransform.rotation.euler.angles.yaw;
			int32_t sinYaw = Numerics::g_sinCosLUT[-yaw & 0xFFF];
			int32_t cosYaw = Numerics::g_sinCosLUT[0x400 - yaw & 0xFFF];

			Matrix3x3I16 rotation;
			Nu3D::Math::SetRotationXYZ(&Toy2::Camera::g_renderCameraTransform.rotation.vector, &rotation);

			int32_t transformed = rotation.m02 * cosYaw + rotation.m00 * sinYaw;
			g_backdropViewX = (transformed + ((transformed >> 31) & 0xFFF)) >> 12;
			transformed = rotation.m12 * cosYaw + rotation.m10 * sinYaw;
			int32_t viewY = (transformed + ((transformed >> 31) & 0xFFF)) >> 12;
			transformed = rotation.m22 * cosYaw + rotation.m20 * sinYaw;
			g_backdropViewDepth = (transformed + ((transformed >> 31) & 0x3FF)) >> 10;

			if (g_backdropViewDepth > 0x800)
				g_backdropHorizon =
					(((Toy2::g_destRectHalfWidth * viewY * 4) / g_backdropViewDepth + dimensions->verticalOffset) * 0xF0) / Toy2::g_destRectHeight;
			else
				g_backdropHorizon = viewY > 0 ? 0x800 : -0x800;

			if (g_backdropScrollOverride.y != -0x8000)
			{
				g_backdropHorizon = g_backdropScrollOverride.y;
				g_backdropViewDepth = 0x4000;
			}

			int32_t groundBlue = Toy2::g_groundColorBlue;
			int32_t groundGreen = Toy2::g_groundColorGreen;
			int32_t groundRed = Toy2::g_groundColorRed;
			int32_t skyBlue = Toy2::g_skyColorBlue;
			int32_t skyGreen = Toy2::g_skyColorGreen;
			if (g_renderMode == RENDERMODE_SOFTWARE)
			{
				switch (g_bitsPerPixel)
				{
					case 8: {
						uint32_t skyColour = g_rgbToPaletteIndex[((Toy2::g_skyColorRed & ~7) * 0x20 + (skyGreen & ~7)) * 4 + (skyBlue >> 3)];
						g_skyFillColourPair = skyColour | skyColour << 8 | skyColour << 16 | skyColour << 24;
						uint32_t groundColour = g_rgbToPaletteIndex[((groundRed & ~7) * 0x20 + (groundGreen & ~7)) * 4 + (groundBlue >> 3)];
						g_groundFillColourPair = groundColour | groundColour << 8 | groundColour << 16 | groundColour << 24;
						break;
					}
					case 15: {
						uint32_t skyColour = (skyBlue >> 3) + ((Toy2::g_skyColorRed & ~7) * 0x20 + (skyGreen & ~7)) * 4;
						g_skyFillColourPair = skyColour | skyColour << 16;
						uint32_t groundColour = (groundBlue >> 3) + ((groundRed & ~7) * 0x20 + (groundGreen & ~7)) * 4;
						g_groundFillColourPair = groundColour | groundColour << 16;
						break;
					}
					case 16: {
						uint32_t skyColour = (skyBlue >> 3) + ((Toy2::g_skyColorRed & ~7) * 0x20 + (skyGreen & ~3)) * 8;
						g_skyFillColourPair = skyColour | skyColour << 16;
						uint32_t groundColour = (groundBlue >> 3) + ((groundRed & ~7) * 0x20 + (groundGreen & ~3)) * 8;
						g_groundFillColourPair = groundColour | groundColour << 16;
						break;
					}
				}
			}

			if (g_backdropHorizon > 0)
			{
				int32_t band = g_backdropBandCount;
				g_backdropBandColours[band].red = (int16_t)Toy2::g_skyColorRed;
				g_backdropBandColours[band].green = (int16_t)skyGreen;
				g_backdropBandColours[band].blue = (int16_t)skyBlue;
				clr[band].x1 = 0;
				clr[band].y1 = 0;
				clr[band].x2 = 0x200;
				clr[band].y2 = 0x100;
				g_backdropBandCount = ++band;

				if (0xF0 - dimensions->height - g_backdropHorizon > 0)
				{
					g_backdropBandColours[band].red = (int16_t)groundRed;
					clr[band - 1].y2 = ((dimensions->height + g_backdropHorizon) * 0x100) / 0xF0;
					g_backdropBandColours[band].green = (int16_t)groundGreen;
					g_backdropBandColours[band].blue = (int16_t)groundBlue;
					clr[band].x1 = 0;
					clr[band].y1 = clr[band - 1].y2;
					clr[band].x2 = 0x200;
					clr[band].y2 = 0x100;
					g_backdropBandCount = band + 1;
				}
			}
			else
			{
				if (g_backdropClearEnabled == 0)
					return;

				int32_t band = g_backdropBandCount;
				g_backdropBandColours[band].red = (int16_t)groundRed;
				g_backdropBandColours[band].green = (int16_t)groundGreen;
				g_backdropBandColours[band].blue = (int16_t)groundBlue;
				clr[band].x1 = 0;
				clr[band].y1 = 0;
				clr[band].x2 = 0x200;
				clr[band].y2 = 0x100;
				g_backdropBandCount = band + 1;
			}
		}

		if (g_renderMode == RENDERMODE_SOFTWARE)
		{
			LockBackBuffer();
			if (g_pendingBackBufferClears != 0)
				ClearBackBufferOnce();
		}

		for (int32_t band = 0; band < g_backdropBandCount; band++)
		{
			BackdropBandColour* colour = &g_backdropBandColours[band];
			D3DRECT* rect = &clr[band];
			switch (g_renderMode)
			{
				case RENDERMODE_SOFTWARE: {
					if (g_lockedBackBuffer != NULL)
					{
						int32_t top = rect->y1 * Toy2::g_softWindowHeight / Toy2::g_destRectHeight - 3 + Toy2::g_screenClipTop;
						int32_t bottom = rect->y2 * Toy2::g_softWindowHeight / Toy2::g_destRectHeight + 3 + Toy2::g_screenClipTop;
						if (top < Toy2::g_screenClipTop)
							top = Toy2::g_screenClipTop;
						if (bottom > Toy2::g_screenClipBottom)
							bottom = Toy2::g_screenClipBottom;

						uint32_t fillColour;
						uint32_t* destination;
						int32_t rowPadding;
						int32_t rowWidth;
						if (g_bitsPerPixel >= 15 && g_bitsPerPixel <= 16)
						{
							uint16_t colour16 =
								((colour->red >> 3) << g_redShift & 0xFFFF) | ((colour->green >> 3) << g_greenShift & 0xFFFF) | (uint16_t)(colour->blue >> 3);
							fillColour = colour16 | colour16 << 16;
							rowPadding = (g_backBufferPitchPixels - Toy2::g_softWindowWidth) * 2;
							rowWidth = Toy2::g_softWindowWidth / 2;
							destination = (uint32_t*)((uint8_t*)g_lockedBackBuffer + (top * g_backBufferPitchPixels + Toy2::g_screenClipLeft) * 2);
						}
						else
						{
							uint32_t paletteColour = g_rgbToPaletteIndex[((colour->red & ~7) * 0x20 + (colour->green & ~7)) * 4 + (colour->blue >> 3)];
							fillColour = paletteColour | paletteColour << 8 | paletteColour << 16 | paletteColour << 24;
							rowPadding = g_backBufferPitchPixels - Toy2::g_softWindowWidth;
							rowWidth = Toy2::g_softWindowWidth / 4;
							destination = (uint32_t*)((uint8_t*)g_lockedBackBuffer + top * g_backBufferPitchPixels + Toy2::g_screenClipLeft);
						}
						FillRect32(destination, rowWidth, bottom - top + 1, rowPadding, fillColour);
					}
					break;
				}
				case RENDERMODE_D3D: {
					if (d3dappi.lpSkyMat != NULL)
					{
						D3DMATERIAL mat;
						memset(&mat, 0, sizeof(mat));
						mat.dwSize = sizeof(mat);
						HRESULT result = d3dappi.lpSkyMat->GetMaterial(&mat);
						if (result < 0)
							Logger::LogDDError("d3dappi.lpSkyMat->GetMaterial(&mat)", result);

						int32_t component = colour->red * Nu3D::Camera::g_cameraTintRed;
						mat.diffuse.r = ((component + ((component >> 31) & 0x7F)) >> 7) * (1.0 / 255.0);
						component = colour->green * Nu3D::Camera::g_cameraTintGreen;
						mat.diffuse.g = ((component + ((component >> 31) & 0x7F)) >> 7) * (1.0 / 255.0);
						component = colour->blue * Nu3D::Camera::g_cameraTintBlue;
						mat.diffuse.b = ((component + ((component >> 31) & 0x7F)) >> 7) * (1.0 / 255.0);
						result = d3dappi.lpSkyMat->SetMaterial(&mat);
						if (result < 0)
							Logger::LogDDError("d3dappi.lpSkyMat->SetMaterial(&mat)", result);
					}

					clr[band].y1 -= 3;
					clr[band].y2 += 3;
					HRESULT result = d3dappi.lpD3DViewport->SetBackground(d3dappi.lpSkyMatHandle);
					if (result < 0)
						Logger::LogDDError("d3dappi.lpD3DViewport->SetBackground(d3dappi.lpSkyMatHandle)", result);
					result = d3dappi.lpD3DViewport->Clear(1, &clr[band], D3DCLEAR_TARGET);
					if (result < 0)
						Logger::LogDDError("d3dappi.lpD3DViewport->Clear(1, &clr[i], clearflags)", result);
					break;
				}
			}
		}

		if (g_renderMode == RENDERMODE_SOFTWARE)
			UnlockBackBuffer();
	}

	// FUNCTION: TOY2 0x00490290 [PROVISIONAL]
	int16_t UpdateBackdropScroll()
	{
		volatile int32_t clampedVisibleHeight;
		int32_t hasStaticBackdrop = Toy2::g_hasStaticBackdrop;
		BackdropDimensions* dimensions = hasStaticBackdrop ? &g_staticBackdropDimensions : &g_backdropDimensions;
		int32_t cameraYaw = Toy2::Sector::g_viewRotation.y;
		int32_t textureColumn;

		if (g_backdropTextureColumn == -1)
		{
			cameraYaw = -cameraYaw;
			g_previousBackdropYaw = cameraYaw;
			g_backdropYawAccumulator = cameraYaw;
			textureColumn = (cameraYaw * 2240 / 8192) & 0xff;
			g_backdropScrollX = textureColumn % dimensions->width;
		}
		else
		{
			int32_t yawDelta = -(g_previousBackdropYaw + cameraYaw);
			if (yawDelta < -0x800)
				yawDelta += 0x1000;
			else if (yawDelta > 0x800)
				yawDelta -= 0x1000;

			g_backdropYawAccumulator += yawDelta;
			g_previousBackdropYaw = -cameraYaw;
			textureColumn = (g_backdropYawAccumulator * 2240 / 8192) & 0xff;

			int32_t scrollDelta = textureColumn - g_backdropTextureColumn;
			if (scrollDelta < -0x80)
				scrollDelta += 0x100;
			else if (scrollDelta > 0x80)
				scrollDelta -= 0x100;

			g_backdropScrollX = (g_backdropScrollX + scrollDelta) % dimensions->width;
			while (g_backdropScrollX < 0)
				g_backdropScrollX += dimensions->width;
		}

		int32_t clippedTop;
		int32_t backdropHorizon = g_backdropHorizon;
		g_backdropTextureColumn = textureColumn;

		if (backdropHorizon < 0)
		{
			clippedTop = -backdropHorizon;
			backdropHorizon = 0;
			g_backdropHorizon = backdropHorizon;
		}
		else
		{
			clippedTop = 0;
		}

		if (g_backdropScrollOverride.x != -0x8000)
			g_backdropScrollX = g_backdropScrollOverride.x;

		int32_t backdropHeight = dimensions->height;
		if (clippedTop <= backdropHeight && clippedTop >= 0 && g_backdropViewDepth > 0x800)
		{
			backdropHeight -= clippedTop;
			if (Toy2::g_levelFileIndex != 0 && ! hasStaticBackdrop && backdropHorizon + backdropHeight > 0xf0)
				clampedVisibleHeight = 0xf0 - backdropHorizon;
		}

		return 1;
	}
}

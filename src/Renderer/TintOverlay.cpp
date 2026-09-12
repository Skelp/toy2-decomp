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

// The screen tint overlay: retail holds 0x0044DD80 as one object.
namespace Renderer
{
	// FUNCTION: TOY2 0x0044DD80 [PROVISIONAL]
	void DrawTintOverlay()
	{
		Vector2F uvTopLeft;
		Vector2F uvBottomRight;
		RGBA overlayColor;

		if (Nu3D::Camera::g_cameraTintRed == 128 && Nu3D::Camera::g_cameraTintGreen == 128 && Nu3D::Camera::g_cameraTintBlue == 128)
			return;

		if (Nu3D::Camera::g_cameraTintRed > 128 || Nu3D::Camera::g_cameraTintGreen > 128 || Nu3D::Camera::g_cameraTintBlue > 128)
		{
			int32_t brightenGreen = 2 * Nu3D::Camera::g_cameraTintGreen - 256;
			int32_t brightenRed = 2 * Nu3D::Camera::g_cameraTintRed - 256;
			int32_t brightenBlue = 2 * Nu3D::Camera::g_cameraTintBlue - 256;

			if (brightenRed > 255)
			{
				brightenRed = 255;
			}
			else if (brightenRed < 0)
			{
				brightenRed = 0;
			}

			if (brightenGreen > 255)
			{
				brightenGreen = 255;
			}
			else if (brightenGreen < 0)
			{
				brightenGreen = 0;
			}

			if (brightenBlue > 255)
			{
				brightenBlue = 255;
			}
			else if (brightenBlue < 0)
			{
				brightenBlue = 0;
			}

			overlayColor.b = brightenBlue;
			overlayColor.g = brightenGreen;
			overlayColor.a = -1 - brightenGreen;
			overlayColor.r = brightenRed;

			uvTopLeft.x = 0.0;
			uvTopLeft.y = 0.0;

			uvBottomRight.x = 1.0;
			uvBottomRight.y = 1.0;

			Sprite::Queue2DSprite(0.0, 0.0, 1.0, 1.0, &uvTopLeft, &uvBottomRight, 0, overlayColor, RENDER_PRESET_COLOR_OVERLAY);
		}
		else
		{
			int32_t darkenRed = 2 * (128 - Nu3D::Camera::g_cameraTintRed);
			int32_t darkenGreen = 2 * (128 - Nu3D::Camera::g_cameraTintGreen);
			int32_t darkenBlue = 2 * (128 - Nu3D::Camera::g_cameraTintBlue);

			if (darkenRed > 255)
			{
				darkenRed = 255;
			}
			else if (darkenRed < 0)
			{
				darkenRed = 0;
			}

			if (darkenGreen > 255)
			{
				darkenGreen = 255;
			}
			else if (darkenGreen < 0)
			{
				darkenGreen = 0;
			}

			if (darkenBlue > 255)
			{
				darkenBlue = 255;
			}
			else if (darkenBlue < 0)
			{
				darkenBlue = 0;
			}

			uvTopLeft.x = 0.0;
			uvTopLeft.y = 0.0;

			uvBottomRight.x = 1.0;
			uvBottomRight.y = 1.0;

			overlayColor.a = -1 - darkenGreen;
			overlayColor.b = darkenBlue;
			overlayColor.g = darkenGreen;
			overlayColor.r = darkenRed;

			int32_t textureDataIndex;

			if (darkenRed == darkenGreen && darkenRed == darkenBlue)
			{
				textureDataIndex = NGNLoader::GetTextureDataIndex(14);

				if (! textureDataIndex)
				{
					textureDataIndex = NGNLoader::GetTextureDataIndex(36);

					if (! textureDataIndex)
						textureDataIndex = NGNLoader::GetTextureDataIndex(37);
				}
			}
			else
			{
				textureDataIndex = 0;
			}

			Sprite::Queue2DSprite(0.0, 0.0, 1.0, 1.0, &uvTopLeft, &uvBottomRight, textureDataIndex, overlayColor, RENDER_PRESET_FADE_OVERLAY);
		}

		if (g_renderMode == RENDERMODE_SOFTWARE && SoftwareRenderer::g_bitsPerPixel == 8)
			SoftwareRenderer::UpdatePaletteTint();
	}
}

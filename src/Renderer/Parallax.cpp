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

// The parallax background: retail holds 0x0048F230 through 0x0048F410 as one
// object, in the order of this file.
namespace Renderer
{
	// GLOBAL: TOY2 0x00500A54
	int32_t g_drawParallaxTexture = 1;

	// GLOBAL: TOY2 0x0072EF90
	float g_parallaxHorizOffset;

	// GLOBAL: TOY2 0x00731F24
	float g_parallaxScrollStep;

	// GLOBAL: TOY2 0x00731D68
	RGBA g_parallaxTexFirstPixel;

	// GLOBAL: TOY2 0x00731CD0
	float g_parallaxTexHeightRatio;

	// GLOBAL: TOY2 0x00731CC8
	RGBA g_parallaxTexLastPixel;

	// GLOBAL: TOY2 0x00731CD4
	float g_parallaxTexWidthRatio;

	// GLOBAL: TOY2 0x00830C4C
	int32_t g_previousParallaxYaw;

	const float kCameraAngleScale = 0.0000152587890625f;

	const float kParallaxVerticalScale = 0.238732412457466f;

	const float kParallaxYawScale = 4.1887903213501f;

	// FUNCTION: TOY2 0x0048F230 [PROVISIONAL]
	void UpdateBackgroundScroll(int16_t cameraPitch, int32_t cameraYaw)
	{
		int32_t textureDataIndex;
		if (Toy2::g_hasStaticBackdrop)
			textureDataIndex = NGNLoader::GetTextureDataIndex(37);
		else
			textureDataIndex = NGNLoader::GetTextureDataIndex(Toy2::g_nextBackdropId);

		if (textureDataIndex && g_drawParallaxTexture)
		{
			uint32_t bitmapWidth;
			uint32_t bitmapHeight;
			uint32_t textureWidth;
			uint32_t textureHeight;
			uint32_t* textureData;
			NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, &textureWidth, &textureHeight, &textureData);

			g_parallaxTexWidthRatio = (float)(int32_t)bitmapWidth / g_virtualScreenWidth;
			g_parallaxTexFirstPixel.value = textureData[0];
			g_parallaxTexLastPixel.value = textureData[textureWidth * textureHeight - 1];
			g_parallaxTexHeightRatio = (float)(int32_t)bitmapHeight / g_virtualScreenHeight;

			float currentYaw = (float)(cameraYaw & 0xFFFF);
			int32_t yawDelta = (int32_t)(currentYaw - g_previousParallaxYaw);
			g_previousParallaxYaw = (int32_t)currentYaw;
			if (yawDelta > 0x8000)
				yawDelta -= 0x10000;
			if (yawDelta < -0x8000)
				yawDelta += 0x10000;

			g_parallaxScrollStep = (float)yawDelta * kParallaxYawScale * kCameraAngleScale;
			g_parallaxCurHorizScroll += g_parallaxScrollStep;
			while (g_parallaxCurHorizScroll <= -g_parallaxTexWidthRatio)
				g_parallaxCurHorizScroll += g_parallaxTexWidthRatio;
			while (g_parallaxCurHorizScroll > 0.0f)
				g_parallaxCurHorizScroll -= g_parallaxTexWidthRatio;

			g_parallaxHorizOffset = (float)cameraPitch * kCameraAngleScale;
			g_parallaxHorizOffset = (float)SoftwareRenderer::g_backdropDimensions.verticalOffset / g_virtualScreenHeight
				+ g_parallaxHorizOffset / (kParallaxVerticalScale * Nu3D::Camera::g_currentCamera->aspectRatio);
		}
	}

	// FUNCTION: TOY2 0x0048F3E0 [MATCHED]
	void ResetParallax()
	{
		g_parallaxCurHorizScroll = 0.0;
		g_parallaxHorizOffset = 0.0;
		g_parallaxTexHeightRatio = 1.0;
		g_parallaxTexWidthRatio = 1.0;
	}

	// FUNCTION: TOY2 0x0048F410 [MATCHED]
	void RenderParallaxBackground(int32_t forceRender)
	{
		if (g_drawParallaxTexture)
		{
			if (! Toy2::g_hasStaticBackdrop)
			{
				if (! Toy2::g_hasBackdrop)
					return;
			}
			else
			{
				g_parallaxHorizOffset = 0.0f;
				g_parallaxCurHorizScroll = 0.0f;
				g_parallaxTexHeightRatio = 1.0f;
				g_parallaxTexWidthRatio = 1.0f;
			}

			if (forceRender || GetIsSoftwareRendering() || (Glue::SetBackdrop(Toy2::g_nextBackdropId), ! Glue::BackdropBltFast()))
			{
				int32_t texIndex = NGNLoader::GetTextureDataIndex(Toy2::g_nextBackdropId);

				if (texIndex)
				{
					Vector2F uvMax;
					Vector2F uvMin;

					uvMin.y = 0.0;
					uvMin.x = 0.0;

					uvMax.y = 1.0;
					uvMax.x = 1.0;

					RGBA color;
					color.a = 0xFF;
					color.r = 0xFF;
					color.g = 0xFF;
					color.b = 0xFF;

					if (g_parallaxHorizOffset > 0.0f)
					{
						float topFillHeight = 1.0f / g_virtualScreenHeight + g_parallaxHorizOffset;
						Sprite::Queue2DSprite(0.0f, 0.0f, 1.0f, topFillHeight, &uvMin, &uvMax, 0, g_parallaxTexLastPixel, RENDER_PARALLAX_BG);
					}

					float verticalExtent = g_parallaxTexHeightRatio + g_parallaxHorizOffset;

					if (verticalExtent < 1.0f)
					{
						float bottomFillHeight = 1.0f - g_parallaxHorizOffset - g_parallaxTexHeightRatio + 1.0f / g_virtualScreenHeight;

						Sprite::Queue2DSprite(0.0f, verticalExtent, 1.0f, bottomFillHeight, &uvMin, &uvMax, 0, g_parallaxTexFirstPixel, RENDER_PARALLAX_BG);
					}

					float nextHorizontalPos;

					do
					{
						Sprite::Queue2DSprite(g_parallaxCurHorizScroll,
							g_parallaxHorizOffset,
							g_parallaxTexWidthRatio,
							g_parallaxTexHeightRatio,
							&uvMin,
							&uvMax,
							texIndex,
							color,
							RENDER_PARALLAX_BG);

						nextHorizontalPos = g_parallaxTexWidthRatio + g_parallaxCurHorizScroll;

						g_parallaxCurHorizScroll = nextHorizontalPos;

					} while (nextHorizontalPos < 1.0f);
				}
			}
		}
	}
}

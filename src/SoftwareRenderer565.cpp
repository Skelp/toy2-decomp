#include "SoftwareRendererSpans.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Camera.h"
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

// The RGB565 rasterizers: retail holds 0x00461D20 through 0x0046D7B0 as one
// object, in the order of this file.
namespace SoftwareRenderer
{
// Adds two RGB565 pixels and clamps each channel at its maximum.
#define ADD_SATURATED_565(first, second, result)                                           \
	do                                                                                     \
	{                                                                                      \
		int32_t redSum = ((first) & RGB565_RED_MASK) + ((second) & RGB565_RED_MASK);       \
		if (redSum > RGB565_RED_MASK)                                                      \
			redSum = RGB565_RED_MASK;                                                      \
		int32_t greenSum = ((first) & RGB565_GREEN_MASK) + ((second) & RGB565_GREEN_MASK); \
		if (greenSum > RGB565_GREEN_MASK)                                                  \
			greenSum = RGB565_GREEN_MASK;                                                  \
		int32_t blueSum = ((first) & RGB565_BLUE_MASK) + ((second) & RGB565_BLUE_MASK);    \
		if (blueSum > RGB565_BLUE_MASK)                                                    \
			blueSum = RGB565_BLUE_MASK;                                                    \
		(result) = (uint16_t)(blueSum | greenSum | redSum);                                \
	} while (0)

	// The RGB565 green ramp holds two entries for each green level of a texel, so the green
	// interpolant keeps one more fraction bit than the red and the blue interpolants.
	const int32_t k_green565RampShift = 15;
	// Texel value that the colour key makes transparent in RGB565 (pure green).
	const uint16_t COLOUR_KEY_TEXEL_565 = 0x7C0;

// Combines one texel with the interpolated light of a span into an RGB565 pixel. Each ramp
// table holds the lit value of every level of one channel, so one table read lights a channel.
#define LIT_TEXEL_565(texel, red, green, blue)                                                                                                   \
	(g_greenRampFull[(((texel) >> 5) & 0x3F) + ((green) >> k_green565RampShift)] + g_redRampFull[((texel) >> 11) + ((red) >> k_fixedPointShift)] \
		+ g_blueRampFull[((texel) & k_fiveBitChannelMask) + ((blue) >> k_fixedPointShift)])

	// FUNCTION: TOY2 0x00461D20 [PROVISIONAL]
	void RasterizeTexturedRect565(SoftwareRenderItem* item)
	{
		uint16_t flags = item->renderFlags;
		uint16_t useColourOffset = flags & SOFTWARE_RENDER_COLOUR_OFFSET;
		int32_t rightU = --item->vertices[2].u;
		int32_t bottomV = --item->vertices[2].v;

		int32_t redRampOffset;
		int32_t greenRampOffset;
		int32_t blueRampOffset;
		if (useColourOffset != 0)
		{
			redRampOffset = item->vertices[0].blue >> k_fixedPointShift;
			greenRampOffset = item->vertices[0].green >> 15;
			blueRampOffset = item->vertices[0].red >> k_fixedPointShift;
		}

		int32_t right = item->vertices[2].x;
		int32_t left = item->vertices[0].x;
		int32_t top = item->vertices[0].y;
		int32_t width = right - left;
		if (width == 0)
			width = 1;

		int32_t bottom = item->vertices[2].y;
		int32_t height = bottom - top;
		if (height == 0)
			height = 1;

		int32_t u = item->vertices[0].u;
		int32_t v = item->vertices[0].v;
		int32_t uStep = (rightU - u) / width;
		int32_t vStep = (bottomV - v) / height;
		width++;
		height++;

		uint16_t* destination = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * Toy2::g_screenClipTop + Toy2::g_screenClipLeft;
		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		if (top < Toy2::g_screenClipTop)
		{
			v += (Toy2::g_screenClipTop - top) * vStep;
			height += top - Toy2::g_screenClipTop;
		}
		else
		{
			destination += (top - Toy2::g_screenClipTop) * g_backBufferPitchPixels;
		}
		if (bottom > Toy2::g_screenClipBottom)
			height += Toy2::g_screenClipBottom - bottom;
		if (left < Toy2::g_screenClipLeft)
		{
			u += (Toy2::g_screenClipLeft - left) * uStep;
			width += left - Toy2::g_screenClipLeft;
		}
		else
		{
			destination += left - Toy2::g_screenClipLeft;
		}
		if (right >= Toy2::g_screenClipRight)
			width += Toy2::g_screenClipRight - right;

		if (flags & SOFTWARE_RENDER_ADDITIVE)
		{
			if (useColourOffset != 0)
			{
				do
				{
					int32_t rowU = u;
					int32_t remaining = width;
					do
					{
						uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0x7C0)
						{
							uint16_t destinationPixel = *destination;
							uint16_t litTexel = g_redRampFull[(texel >> 11) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x3F) + greenRampOffset]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + blueRampOffset];
							uint32_t red = (destinationPixel & 0xF800) + (litTexel & 0xF800);
							if (red > 0xF800)
								red = 0xF800;
							uint32_t green = (destinationPixel & 0x7E0) + (litTexel & 0x7E0);
							if (green > 0x7E0)
								green = 0x7E0;
							uint32_t blue = (destinationPixel & 0x1F) + (litTexel & 0x1F);
							if (blue > 0x1F)
								blue = 0x1F;
							*destination = red | green | blue;
						}
						destination++;
						rowU += uStep;
						remaining--;
					} while (remaining != 0);
					destination += g_backBufferPitchPixels - width;
					v += vStep;
					height--;
				} while (height != 0);
				return;
			}

			do
			{
				int32_t rowU = u;
				int32_t remaining = width;
				do
				{
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x7C0)
					{
						uint16_t destinationPixel = *destination;
						uint32_t red = (destinationPixel & 0xF800) + (texel & 0xF800);
						if (red > 0xF800)
							red = 0xF800;
						uint32_t green = (destinationPixel & 0x7E0) + (texel & 0x7E0);
						if (green > 0x7E0)
							green = 0x7E0;
						uint32_t blue = (destinationPixel & 0x1F) + (texel & 0x1F);
						if (blue > 0x1F)
							blue = 0x1F;
						*destination = red | green | blue;
					}
					destination++;
					rowU += uStep;
					remaining--;
				} while (remaining != 0);
				destination += g_backBufferPitchPixels - width;
				v += vStep;
				height--;
			} while (height != 0);
			return;
		}

		if (flags & SOFTWARE_RENDER_SUBTRACTIVE)
		{
			if (useColourOffset != 0)
			{
				do
				{
					int32_t rowU = u;
					int32_t remaining = width;
					do
					{
						uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0x7C0)
						{
							uint32_t destinationPixel = *destination;
							uint32_t sourcePixel = g_redRampFull[(texel >> 11) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x3F) + greenRampOffset]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + blueRampOffset];
							int32_t red = (destinationPixel & 0xF800) - (sourcePixel & 0xF800);
							if (red < 0x800)
								red = 0;
							int32_t green = (destinationPixel & 0x7E0) - (sourcePixel & 0x7E0);
							if (green < 0x20)
								green = 0;
							int32_t blue = (destinationPixel & 0x1F) - (sourcePixel & 0x1F);
							if (blue < 1)
								blue = 0;
							*destination = (uint16_t)(red | green | blue);
						}
						destination++;
						rowU += uStep;
						remaining--;
					} while (remaining != 0);
					destination += g_backBufferPitchPixels - width;
					v += vStep;
					height--;
				} while (height != 0);
				return;
			}

			do
			{
				int32_t rowU = u;
				int32_t remaining = width;
				do
				{
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x7C0)
					{
						uint32_t destinationPixel = *destination;
						uint32_t sourcePixel = texel;
						int32_t red = (destinationPixel & 0xF800) - (sourcePixel & 0xF800);
						if (red < 0x800)
							red = 0;
						int32_t green = (destinationPixel & 0x7E0) - (sourcePixel & 0x7E0);
						if (green < 0x20)
							green = 0;
						int32_t blue = (destinationPixel & 0x1F) - (sourcePixel & 0x1F);
						if (blue < 1)
							blue = 0;
						*destination = (uint16_t)(red | green | blue);
					}
					destination++;
					rowU += uStep;
					remaining--;
				} while (remaining != 0);
				destination += g_backBufferPitchPixels - width;
				v += vStep;
				height--;
			} while (height != 0);
			return;
		}

		if (flags & SOFTWARE_RENDER_BLEND_50)
		{
			if (useColourOffset != 0)
			{
				do
				{
					int32_t rowU = u;
					int32_t remaining = width;
					do
					{
						uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0x7C0)
						{
							uint16_t litTexel = g_redRampFull[(texel >> 11) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x3F) + greenRampOffset]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + blueRampOffset];
							*destination = (*destination >> 1 & RGB565_HALF_MASK) + (litTexel >> 1 & RGB565_HALF_MASK);
						}
						destination++;
						rowU += uStep;
						remaining--;
					} while (remaining != 0);
					destination += g_backBufferPitchPixels - width;
					v += vStep;
					height--;
				} while (height != 0);
				return;
			}

			do
			{
				int32_t rowU = u;
				int32_t remaining = width;
				do
				{
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x7C0)
						*destination = (*destination >> 1 & RGB565_HALF_MASK) + (texel >> 1 & RGB565_HALF_MASK);
					destination++;
					rowU += uStep;
					remaining--;
				} while (remaining != 0);
				destination += g_backBufferPitchPixels - width;
				v += vStep;
				height--;
			} while (height != 0);
			return;
		}

		if (useColourOffset != 0)
		{
			do
			{
				int32_t rowU = u;
				int32_t remaining = width;
				do
				{
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x7C0)
					{
						*destination = g_redRampFull[(texel >> 11) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x3F) + greenRampOffset]
							+ g_blueRampFull[(texel & k_fiveBitChannelMask) + blueRampOffset];
					}
					destination++;
					rowU += uStep;
					remaining--;
				} while (remaining != 0);
				destination += g_backBufferPitchPixels - width;
				v += vStep;
				height--;
			} while (height != 0);
			return;
		}

		do
		{
			int32_t rowU = u;
			int32_t remaining = width;
			do
			{
				uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
				if (texel != 0x7C0)
					*destination = texel;
				destination++;
				rowU += uStep;
				remaining--;
			} while (remaining != 0);
			destination += g_backBufferPitchPixels - width;
			v += vStep;
			height--;
		} while (height != 0);
	}

	// FUNCTION: TOY2 0x00463090 [PROVISIONAL]
	void RasterizeTexturedPolygon565(SoftwareRenderItem* item) {
#define POLYGON_COLOUR_KEY 0x7C0
#include "SoftwareRendererTexturedPoly.inc"
#undef POLYGON_COLOUR_KEY
	}

	// FUNCTION: TOY2 0x00463B80 [PROVISIONAL]
	void RasterizeBlend25TexturedPolygon565(SoftwareRenderItem* item)
	{
		int32_t bottomY = item->vertices[0].y;
		int32_t topY = bottomY;
		if (item->vertices[1].y < topY)
			topY = item->vertices[1].y;
		else if (item->vertices[1].y > bottomY)
			bottomY = item->vertices[1].y;

		if (item->vertices[2].y < topY)
			topY = item->vertices[2].y;
		else if (item->vertices[2].y > bottomY)
			bottomY = item->vertices[2].y;

		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			if (item->vertices[3].y < topY)
				topY = item->vertices[3].y;
			else if (item->vertices[3].y > bottomY)
				bottomY = item->vertices[3].y;
		}

		if (topY < Toy2::g_screenClipTop)
			topY = Toy2::g_screenClipTop;
		if (bottomY > Toy2::g_screenClipBottom)
			bottomY = Toy2::g_screenClipBottom;

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[0], &item->vertices[1], edge01DoneBlend25_565);
		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[1], &item->vertices[2], edge12DoneBlend25_565);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[0], edge20DoneBlend25_565);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[3], edge23DoneBlend25_565);
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[3], &item->vertices[0], edge30DoneBlend25_565);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX != rightX)
					{
						int32_t width = rightX - leftX;
						int32_t u = scanline->leftInterpolants[0];
						int32_t v = scanline->leftInterpolants[1];
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						CLIP_SPAN_TO_SCREEN(uint16_t* pixel, u += clippedPixels * uStep; v += clippedPixels * vStep; pixel = rowStart);

						for (; pixelCount > 0; pixelCount--)
						{
							uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x7C0)
							{
								*pixel = (*pixel >> 2 & 0x39E7) + (texel & 0xE79C) / 4 + (*pixel & 0xF7DE) / 2;
							}
							u += uStep;
							v += vStep;
							pixel++;
						}
					}
				}
				rowStart += g_backBufferPitchPixels;
				scanline++;
				scanlineCount--;
			} while (scanlineCount != 0);
			return;
		}

		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX != rightX)
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					CLIP_SPAN_TO_SCREEN(uint16_t* pixel, u += clippedPixels * uStep; v += clippedPixels * vStep; pixel = rowStart);

					for (; pixelCount > 0; pixelCount--)
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						*pixel = (*pixel >> 2 & 0x39E7) + (texel & 0xE79C) / 4 + (*pixel & 0xF7DE) / 2;
						u += uStep;
						v += vStep;
						pixel++;
					}
				}
			}
			rowStart += g_backBufferPitchPixels;
			scanline++;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x00464690 [PROVISIONAL]
	void RasterizeBlend50TexturedPolygon565(SoftwareRenderItem* item)
	{
		int32_t bottomY = item->vertices[0].y;
		int32_t topY = bottomY;
		if (item->vertices[1].y < topY)
			topY = item->vertices[1].y;
		else if (item->vertices[1].y > bottomY)
			bottomY = item->vertices[1].y;

		if (item->vertices[2].y < topY)
			topY = item->vertices[2].y;
		else if (item->vertices[2].y > bottomY)
			bottomY = item->vertices[2].y;

		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			if (item->vertices[3].y < topY)
				topY = item->vertices[3].y;
			else if (item->vertices[3].y > bottomY)
				bottomY = item->vertices[3].y;
		}

		if (topY < Toy2::g_screenClipTop)
			topY = Toy2::g_screenClipTop;
		if (bottomY > Toy2::g_screenClipBottom)
			bottomY = Toy2::g_screenClipBottom;

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[0], &item->vertices[1], edge01DoneBlend50_565);
		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[1], &item->vertices[2], edge12DoneBlend50_565);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[0], edge20DoneBlend50_565);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[3], edge23DoneBlend50_565);
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[3], &item->vertices[0], edge30DoneBlend50_565);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX != rightX)
					{
						int32_t width = rightX - leftX;
						int32_t u = scanline->leftInterpolants[0];
						int32_t v = scanline->leftInterpolants[1];
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						CLIP_SPAN_TO_SCREEN(uint16_t* pixel, u += clippedPixels * uStep; v += clippedPixels * vStep; pixel = rowStart);

						for (; pixelCount > 0; pixelCount--)
						{
							uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x7C0)
								*pixel = (*pixel >> 1 & RGB565_HALF_MASK) + (texel >> 1 & RGB565_HALF_MASK);
							u += uStep;
							v += vStep;
							pixel++;
						}
					}
				}
				rowStart += g_backBufferPitchPixels;
				scanline++;
				scanlineCount--;
			} while (scanlineCount != 0);
			return;
		}

		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX != rightX)
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					CLIP_SPAN_TO_SCREEN(uint16_t* pixel, v += clippedPixels * vStep; u += clippedPixels * uStep; pixel = rowStart);

					for (; pixelCount > 0; pixelCount--)
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						*pixel = (*pixel >> 1 & 0x7BCF) + (texel >> 1 & 0x7BCF);
						u += uStep;
						v += vStep;
						pixel++;
					}
				}
			}
			rowStart += g_backBufferPitchPixels;
			scanline++;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// STUB: TOY2 0x00465180
	void UnkRenderAPI24(SoftwareRenderItem* item) {}

	// Draws a Gouraud lit, textured triangle or quad into the RGB565 back buffer.
	// FUNCTION: TOY2 0x00467080 [PROVISIONAL]
	void UnkRenderAPI17(SoftwareRenderItem* item)
	{
		// Find the rows that the polygon covers.
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ClearScanlineFlags(&g_scanlineScratch[topY], scanlineCount);

		// Walk the edges into the scanline table.
		RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[0], &item->vertices[1], litEdge01DoneTextured565);
		RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[1], &item->vertices[2], litEdge12DoneTextured565);
		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[2], &item->vertices[3], litEdge23DoneTextured565);
			RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[3], &item->vertices[0], litEdge30DoneTextured565);
		}
		else
		{
			RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[2], &item->vertices[0], litEdge20DoneTextured565);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		ScanlineScratch* spanRow = &g_scanlineScratch[topY];
		int32_t rowsLeft = scanlineCount;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			// Hold the back buffer where the texel is the colour key.
			DRAW_LIT_TEXTURED_COLOUR_KEY_ROWS(LIT_TEXEL_565, COLOUR_KEY_TEXEL_565);
			return;
		}

		// Write every texel of the span.
		DRAW_LIT_TEXTURED_ROWS(LIT_TEXEL_565);
	}

	// FUNCTION: TOY2 0x00468430 [PROVISIONAL]
	void RasterizeSubtractiveTexturedPolygon565(SoftwareRenderItem* item) {
#define POLYGON_COLOUR_KEY 0x7C0
#define POLYGON_RED_MASK 0xF800
#define POLYGON_GREEN_MASK 0x7E0
#define POLYGON_RED_FLOOR 0x800
#include "SoftwareRendererSubtractiveTexturedPoly.inc"
#undef POLYGON_RED_FLOOR
#undef POLYGON_GREEN_MASK
#undef POLYGON_RED_MASK
#undef POLYGON_COLOUR_KEY
	}

	// FUNCTION: TOY2 0x00468E90 [PROVISIONAL]
	void RasterizeAdditiveTexturedPolygon565(SoftwareRenderItem* item) {
#define POLYGON_COLOUR_KEY 0x7C0
#define POLYGON_RED_MASK 0xF800
#define POLYGON_GREEN_MASK 0x7E0
#include "SoftwareRendererAdditiveTexturedPoly.inc"
#undef POLYGON_GREEN_MASK
#undef POLYGON_RED_MASK
#undef POLYGON_COLOUR_KEY
	}

	// STUB: TOY2 0x00469900
	void UnkRenderAPI18(SoftwareRenderItem* item)
	{}

	// STUB: TOY2 0x0046AC60
	void UnkRenderAPI19(SoftwareRenderItem* item) {}

	// STUB: TOY2 0x0046BF90
	void UnkRenderAPI20(SoftwareRenderItem* item) {}

	// FUNCTION: TOY2 0x0046D2C0 [PROVISIONAL]
	void RasterizeSolidQuad16(const PointI* point0, const PointI* point1, const PointI* point2, const PointI* point3, uint32_t colourPair)
	{
		int32_t bottomY = point0->y;
		int32_t topY = bottomY;
		if (point1->y < topY)
			topY = point1->y;
		else if (point1->y > bottomY)
			bottomY = point1->y;

		if (point2->y < topY)
			topY = point2->y;
		else if (point2->y > bottomY)
			bottomY = point2->y;

		if (point3->y < topY)
			topY = point3->y;
		else if (point3->y > bottomY)
			bottomY = point3->y;

		if (topY < Toy2::g_screenClipTop)
			topY = Toy2::g_screenClipTop;
		if (bottomY > Toy2::g_screenClipBottom)
			bottomY = Toy2::g_screenClipBottom;

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		RASTERIZE_SOLID_EDGE(point0, point1, edge01Done);
		RASTERIZE_SOLID_EDGE(point1, point2, edge12Done);
		RASTERIZE_SOLID_EDGE(point2, point3, edge23Done);
		RASTERIZE_SOLID_EDGE(point3, point0, edge30Done);

		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX != rightX)
				{
					if (leftX < Toy2::g_screenClipLeft)
						leftX = Toy2::g_screenClipLeft;
					if (rightX > Toy2::g_screenClipRight)
						rightX = Toy2::g_screenClipRight;

					uint16_t* pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					int32_t pixelCount = rightX - leftX + 1;
					if (((uint32_t)pixel & 3) != 0)
					{
						*pixel++ = (uint16_t)colourPair;
						pixelCount--;
					}
					if (pixelCount > 1)
					{
						Nu3D::MemSet32Util(pixel, pixelCount / 2, colourPair);
						int32_t filledPixels = pixelCount & ~1;
						pixelCount -= filledPixels;
						pixel += filledPixels;
					}
					if (pixelCount != 0)
						*pixel = (uint16_t)colourPair;
				}
			}

			scanline++;
			scanlineCount--;
			rowStart += g_backBufferPitchPixels;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x0046D7B0 [PROVISIONAL]
	void UnkRenderAPI25(SoftwareRenderItem* item)
	{
		// Select how the overlay texture combines with this polygon.
		int32_t overlayMode;
		if (item->renderFlags & SOFTWARE_RENDER_NO_OVERLAY)
			overlayMode = OVERLAY_MODE_NONE;
		else if (Toy2::g_levelFileIndex == 15 || Toy2::g_levelFileIndex == 11)
			overlayMode = OVERLAY_MODE_ADDITIVE;
		else if (Toy2::g_levelFileIndex == 7 || Toy2::g_levelFileIndex == 8)
			overlayMode = OVERLAY_MODE_BLEND_50;
		else if (Toy2::g_levelFileIndex == 9)
			overlayMode =
				((uint16_t*)g_softwareTextureData[item->textureIndex])[((item->vertices[0].v >> 8) & k_upperByteMask)
					+ (item->vertices[0].u >> k_fixedPointShift)]
					== OVERLAY_MARKER_TEXEL
				? OVERLAY_MODE_ADDITIVE
				: OVERLAY_MODE_LIT;
		else
			overlayMode = OVERLAY_MODE_LIT;

		// Find the rows that the polygon covers.
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		// Walk the edges into the scanline table.
		const SoftwareOverlayItem* overlayItem = (const SoftwareOverlayItem*)item;
		if (overlayMode == OVERLAY_MODE_NONE)
		{
			RASTERIZE_LIT_EDGE(&item->vertices[0], &item->vertices[1], litEdge01Done);
			RASTERIZE_LIT_EDGE(&item->vertices[1], &item->vertices[2], litEdge12Done);
			if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
			{
				RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[0], litEdge20Done);
			}
			else
			{
				RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[3], litEdge23Done);
				RASTERIZE_LIT_EDGE(&item->vertices[3], &item->vertices[0], litEdge30Done);
			}
		}
		else if (overlayMode > OVERLAY_MODE_LIT)
		{
			RASTERIZE_OVERLAY_EDGE(&item->vertices[0], &item->vertices[1], overlayItem->overlayUV[0], overlayItem->overlayUV[1], overlayEdge01Done);
			RASTERIZE_OVERLAY_EDGE(&item->vertices[1], &item->vertices[2], overlayItem->overlayUV[1], overlayItem->overlayUV[2], overlayEdge12Done);
			if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
			{
				RASTERIZE_OVERLAY_EDGE(&item->vertices[2], &item->vertices[0], overlayItem->overlayUV[2], overlayItem->overlayUV[0], overlayEdge20Done);
			}
			else
			{
				RASTERIZE_OVERLAY_EDGE(&item->vertices[2], &item->vertices[3], overlayItem->overlayUV[2], overlayItem->overlayUV[3], overlayEdge23Done);
				RASTERIZE_OVERLAY_EDGE(&item->vertices[3], &item->vertices[0], overlayItem->overlayUV[3], overlayItem->overlayUV[0], overlayEdge30Done);
			}
		}
		else
		{
			RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[0], &item->vertices[1], overlayItem->overlayUV[0], overlayItem->overlayUV[1], litOverlayEdge01Done);
			RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[1], &item->vertices[2], overlayItem->overlayUV[1], overlayItem->overlayUV[2], litOverlayEdge12Done);
			if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
			{
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[2], &item->vertices[0], overlayItem->overlayUV[2], overlayItem->overlayUV[0], litOverlayEdge20Done);
			}
			else
			{
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[2], &item->vertices[3], overlayItem->overlayUV[2], overlayItem->overlayUV[3], litOverlayEdge23Done);
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[3], &item->vertices[0], overlayItem->overlayUV[3], overlayItem->overlayUV[0], litOverlayEdge30Done);
			}
		}

		uint16_t* overlay = (uint16_t*)g_softwareTextureData[OVERLAY_TEXTURE_INDEX];
		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (overlayMode == OVERLAY_MODE_NONE)
		{
			// Add the lit texture to the back buffer.
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX != rightX)
					{
						int32_t width = rightX - leftX;
						int32_t u = scanline->leftInterpolants[0];
						int32_t v = scanline->leftInterpolants[1];
						int32_t blue = scanline->leftInterpolants[2];
						int32_t green = scanline->leftInterpolants[3];
						int32_t red = scanline->leftInterpolants[4];
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						int32_t blueStep = (scanline->rightInterpolants[2] - blue) / width;
						int32_t greenStep = (scanline->rightInterpolants[3] - green) / width;
						int32_t redStep = (scanline->rightInterpolants[4] - red) / width;
						CLIP_SPAN_TO_SCREEN(uint16_t* pixel = rowStart, u += clippedPixels * uStep; v += clippedPixels * vStep;
							blue += clippedPixels * blueStep;
							red += clippedPixels * redStep;
							green += clippedPixels * greenStep);

						for (; pixelCount > 0; pixelCount--)
						{
							uint16_t texel = texture[((v >> 8) & k_upperByteMask) + (u >> k_fixedPointShift)];
							uint32_t destinationPixel = *pixel;
							uint32_t litTexel = (uint16_t)(g_greenRampFull[((texel >> 5) & 0x3F) + (green >> 15)]
								+ g_redRampFull[(texel >> 11) + (blue >> k_fixedPointShift)]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + (red >> k_fixedPointShift)]);
							ADD_SATURATED_565(destinationPixel, litTexel, *pixel);
							v += vStep;
							u += uStep;
							blue += blueStep;
							green += greenStep;
							red += redStep;
							pixel++;
						}
					}
				}
				scanline++;
				rowStart += g_backBufferPitchPixels;
				scanlineCount--;
			} while (scanlineCount != 0);
			return;
		}

		if (overlayMode == OVERLAY_MODE_ADDITIVE)
		{
			// Add the overlay texture to the back buffer.
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX != rightX)
					{
						CLIP_OVERLAY_BLEND_SPAN(uint16_t)

						for (; pixelCount > 0; pixelCount--)
						{
							uint32_t overlayTexel = overlay[((overlayV >> 8) & k_upperByteMask) + (overlayU >> k_fixedPointShift)];
							uint32_t destinationPixel = *pixel;
							ADD_SATURATED_565(destinationPixel, overlayTexel, *pixel);
							overlayU += overlayUStep;
							overlayV += overlayVStep;
							pixel++;
						}
					}
				}
				scanline++;
				rowStart += g_backBufferPitchPixels;
				scanlineCount--;
			} while (scanlineCount != 0);
			return;
		}

		if (overlayMode == OVERLAY_MODE_BLEND_50)
		{
			// Average the overlay texture with the back buffer.
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX != rightX)
					{
						CLIP_OVERLAY_BLEND_SPAN(uint16_t)

						for (; pixelCount > 0; pixelCount--)
						{
							*pixel = (overlay[((overlayV >> 8) & k_upperByteMask) + (overlayU >> k_fixedPointShift)] >> 1 & RGB565_HALF_MASK)
								+ (*pixel >> 1 & RGB565_HALF_MASK);
							overlayU += overlayUStep;
							overlayV += overlayVStep;
							pixel++;
						}
					}
				}
				scanline++;
				rowStart += g_backBufferPitchPixels;
				scanlineCount--;
			} while (scanlineCount != 0);
			return;
		}

		// Write the lit texture plus the overlay texture.
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint16_t texel = texture[((scanline->leftInterpolants[1] >> 8) & k_upperByteMask) + (scanline->leftInterpolants[0] >> k_fixedPointShift)];
					uint32_t litTexel = (uint16_t)(g_greenRampFull[((texel >> 5) & 0x3F) + (scanline->leftInterpolants[3] >> 15)]
						+ g_redRampFull[(texel >> 11) + (scanline->leftInterpolants[2] >> k_fixedPointShift)]
						+ g_blueRampFull[(texel & k_fiveBitChannelMask) + (scanline->leftInterpolants[4] >> k_fixedPointShift)]);
					uint32_t overlayTexel = overlay[((scanline->rightInterpolants[OVERLAY_LEFT_V] >> 8) & k_upperByteMask)
						+ (scanline->rightInterpolants[OVERLAY_LEFT_U] >> k_fixedPointShift)];
					ADD_SATURATED_565(overlayTexel, litTexel, rowStart[leftX - Toy2::g_screenClipLeft]);
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t blue = scanline->leftInterpolants[2];
					int32_t green = scanline->leftInterpolants[3];
					int32_t red = scanline->leftInterpolants[4];
					int32_t overlayU = scanline->rightInterpolants[OVERLAY_LEFT_U];
					int32_t overlayV = scanline->rightInterpolants[OVERLAY_LEFT_V];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t blueStep = (scanline->rightInterpolants[2] - blue) / width;
					int32_t greenStep = (scanline->rightInterpolants[3] - green) / width;
					int32_t redStep = (scanline->rightInterpolants[4] - red) / width;
					int32_t overlayUStep = (scanline->rightInterpolants[OVERLAY_RIGHT_U] - overlayU) / width;
					int32_t overlayVStep = (scanline->rightInterpolants[OVERLAY_RIGHT_V] - overlayV) / width;
					int32_t pixelCount;
					uint16_t* pixel = rowStart;
					if (leftX < Toy2::g_screenClipLeft)
					{
						int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
						u += clippedPixels * uStep;
						v += clippedPixels * vStep;
						blue += clippedPixels * blueStep;
						green += clippedPixels * greenStep;
						red += clippedPixels * redStep;
						overlayU += overlayUStep * clippedPixels;
						overlayV += overlayVStep * clippedPixels;
						pixelCount = Toy2::g_softWindowWidth;
						if (rightX <= Toy2::g_screenClipRight)
							pixelCount = rightX - Toy2::g_screenClipLeft + 1;
					}
					else
					{
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
						if (rightX <= Toy2::g_screenClipRight)
							pixelCount = width + 1;
						else
							pixelCount = Toy2::g_screenClipRight - leftX + 1;
					}

					do
					{
						uint16_t texel = texture[((v >> 8) & k_upperByteMask) + (u >> k_fixedPointShift)];
						uint32_t litTexel = (uint16_t)(g_greenRampFull[((texel >> 5) & 0x3F) + (green >> 15)]
							+ g_redRampFull[(texel >> 11) + (blue >> k_fixedPointShift)]
							+ g_blueRampFull[(texel & k_fiveBitChannelMask) + (red >> k_fixedPointShift)]);
						uint32_t overlayTexel = overlay[((overlayV >> 8) & k_upperByteMask) + (overlayU >> k_fixedPointShift)];
						ADD_SATURATED_565(overlayTexel, litTexel, *pixel);
						pixel++;
						v += vStep;
						u += uStep;
						green += greenStep;
						blue += blueStep;
						red += redStep;
						overlayU += overlayUStep;
						overlayV += overlayVStep;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
	}
}

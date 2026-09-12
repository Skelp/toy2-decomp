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

// The 8-bit palette rasterizers: retail holds 0x00471520 through 0x00477EB0 as
// one object, in the order of this file.
namespace SoftwareRenderer
{
// Index into g_paletteColourOffsetTable of one interpolated colour, before the texel row.
#define PALETTE_COLOUR_OFFSET(blue, green, red) (((blue) >> 12 & ~0x3F) + ((green) >> 15 & ~7) + ((red) >> 18))

// Sets up one textured span of the 8-bit back buffer: the texture UV at the left end of
// the span with its step across the span, and the first pixel with the pixel count that the
// left clip and the right clip leave. It reads scanline, rowStart, leftX and rightX of the
// span loop, and declares width, u, v, uStep, vStep, pixel and pixelCount for the loop that
// writes the pixels.
#define UV_SPAN_SETUP_8()                                         \
	int32_t width = rightX - leftX;                               \
	int32_t u = scanline->leftInterpolants[0];                    \
	int32_t v = scanline->leftInterpolants[1];                    \
	int32_t uStep = (scanline->rightInterpolants[0] - u) / width; \
	int32_t vStep = (scanline->rightInterpolants[1] - v) / width; \
	int32_t pixelCount = width;                                   \
	uint8_t* pixel;                                               \
	if (leftX < Toy2::g_screenClipLeft)                           \
	{                                                             \
		int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;   \
		u += clippedPixels * uStep;                               \
		v += clippedPixels * vStep;                               \
		pixelCount = Toy2::g_softWindowWidth;                     \
		pixel = rowStart;                                         \
		if (rightX <= Toy2::g_screenClipRight)                    \
			pixelCount = rightX - Toy2::g_screenClipLeft + 1;     \
	}                                                             \
	else                                                          \
	{                                                             \
		if (rightX > Toy2::g_screenClipRight)                     \
			pixelCount = Toy2::g_screenClipRight - leftX;         \
		pixelCount++;                                             \
		pixel = rowStart + leftX - Toy2::g_screenClipLeft;        \
	}

	// FUNCTION: TOY2 0x00471520 [PROVISIONAL]
	void RasterizeTexturedRect8(SoftwareRenderItem* item)
	{
		uint16_t flags = item->renderFlags;
		int32_t useColourOffset = flags & SOFTWARE_RENDER_COLOUR_OFFSET;
		item->vertices[2].u--;
		item->vertices[2].v--;

		int32_t colourOffsetIndex;
		if (useColourOffset != 0)
		{
			colourOffsetIndex = (item->vertices[0].blue >> 12 & ~0x3F) + (item->vertices[0].green >> 15 & ~7) + (item->vertices[0].red >> 18);
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

		int32_t v = item->vertices[0].v;
		int32_t u = item->vertices[0].u;
		int32_t uStep = (item->vertices[2].u - u) / width;
		int32_t vStep = (item->vertices[2].v - v) / height;
		width++;
		height++;

		uint8_t* destination = (uint8_t*)g_lockedBackBuffer + g_backBufferPitchPixels * Toy2::g_screenClipTop + Toy2::g_screenClipLeft;
		uint8_t* texture = (uint8_t*)g_softwareTextureData[item->textureIndex];
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

		int32_t rowWidth = width;
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
						uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0)
						{
							uint8_t litTexel = g_paletteColourOffsetTable[colourOffsetIndex + texel * PALETTE_COLOUR_OFFSET_STRIDE];
							*destination = g_additivePaletteTable[*destination + litTexel * PALETTE_COMBINE_TABLE_STRIDE];
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
				int32_t remaining = rowWidth;
				do
				{
					uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0)
						*destination = g_additivePaletteTable[*destination + texel * PALETTE_COMBINE_TABLE_STRIDE];
					destination++;
					rowU += uStep;
					remaining--;
				} while (remaining != 0);
				destination += g_backBufferPitchPixels - rowWidth;
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
						uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0)
						{
							uint8_t litTexel = g_paletteColourOffsetTable[colourOffsetIndex + texel * PALETTE_COLOUR_OFFSET_STRIDE];
							*destination = g_subtractivePaletteTable[*destination * PALETTE_COMBINE_TABLE_STRIDE + litTexel];
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
				int32_t remaining = rowWidth;
				do
				{
					uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0)
						*destination = g_subtractivePaletteTable[texel + *destination * PALETTE_COMBINE_TABLE_STRIDE];
					destination++;
					rowU += uStep;
					remaining--;
				} while (remaining != 0);
				destination += g_backBufferPitchPixels - rowWidth;
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
						uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0)
						{
							uint8_t litTexel = g_paletteColourOffsetTable[colourOffsetIndex + texel * PALETTE_COLOUR_OFFSET_STRIDE];
							*destination = g_paletteBlend50Table[*destination + litTexel * PALETTE_COMBINE_TABLE_STRIDE];
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
				int32_t remaining = rowWidth;
				do
				{
					uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0)
						*destination = g_paletteBlend50Table[*destination + texel * PALETTE_COMBINE_TABLE_STRIDE];
					destination++;
					rowU += uStep;
					remaining--;
				} while (remaining != 0);
				destination += g_backBufferPitchPixels - rowWidth;
				v += vStep;
				height--;
			} while (height != 0);
			return;
		}

		if (useColourOffset != 0)
		{
			do
			{
				int32_t remaining = width;
				int32_t rowU = u;
				do
				{
					uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0)
						*destination = g_paletteColourOffsetTable[colourOffsetIndex + texel * PALETTE_COLOUR_OFFSET_STRIDE];
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
			int32_t remaining = width;
			int32_t rowU = u;
			do
			{
				uint8_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
				if (texel != 0)
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

	// STUB: TOY2 0x00471B30
	void UnkRenderAPI30(SoftwareRenderItem* item) {}

	// STUB: TOY2 0x00472E70
	void UnkRenderAPI31(SoftwareRenderItem* item) {}

	// FUNCTION: TOY2 0x00474190 [PROVISIONAL]
	void RasterizeTexturedPolygon8(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneTextured8);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneTextured8);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneTextured8);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneTextured8);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneTextured8);
		}

		uint8_t* texture = (uint8_t*)g_softwareTextureData[item->textureIndex];
		uint8_t* rowStart = (uint8_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX == rightX)
					{
						uint8_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
						if (texel != 0)
							rowStart[leftX - Toy2::g_screenClipLeft] = texel;
					}
					else
					{
						UV_SPAN_SETUP_8();

						do
						{
							uint8_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0)
								*pixel = texel;
							u += uStep;
							v += vStep;
							pixelCount--;
							pixel++;
						} while (pixelCount > 0);
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
				if (leftX == rightX)
				{
					rowStart[leftX - Toy2::g_screenClipLeft] =
						texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount;
					uint8_t* pixel;
					if (leftX < Toy2::g_screenClipLeft)
					{
						int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
						u += clippedPixels * uStep;
						v += clippedPixels * vStep;
						pixelCount = Toy2::g_softWindowWidth;
						pixel = rowStart;
						if (rightX <= Toy2::g_screenClipRight)
							pixelCount = rightX - Toy2::g_screenClipLeft + 1;
					}
					else
					{
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
						if (rightX <= Toy2::g_screenClipRight)
							pixelCount = rightX - leftX + 1;
						else
							pixelCount = Toy2::g_screenClipRight - leftX + 1;
					}

					do
					{
						*pixel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						u += uStep;
						v += vStep;
						pixelCount--;
						pixel++;
					} while (pixelCount > 0);
				}
			}
			rowStart += g_backBufferPitchPixels;
			scanline++;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// STUB: TOY2 0x00474C80
	void UnkRenderAPI33(SoftwareRenderItem* item) {}

	// FUNCTION: TOY2 0x00476340 [PROVISIONAL]
	void RasterizeSubtractiveTexturedPolygon8(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneSubtractive8);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneSubtractive8);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneSubtractive8);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneSubtractive8);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneSubtractive8);
		}

		uint8_t* texture = (uint8_t*)g_softwareTextureData[item->textureIndex];
		uint8_t* rowStart = (uint8_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint8_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0)
					{
						uint8_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						pixel = g_subtractivePaletteTable[texel + pixel * PALETTE_COMBINE_TABLE_STRIDE];
					}
				}
				else
				{
					UV_SPAN_SETUP_8();

					do
					{
						uint8_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0)
							*pixel = g_subtractivePaletteTable[texel + *pixel * PALETTE_COMBINE_TABLE_STRIDE];
						u += uStep;
						v += vStep;
						pixel++;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			rowStart += g_backBufferPitchPixels;
			scanline++;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x00476D00 [PROVISIONAL]
	void RasterizeAdditiveTexturedPolygon8(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneAdditive8);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneAdditive8);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneAdditive8);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneAdditive8);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneAdditive8);
		}

		uint8_t* texture = (uint8_t*)g_softwareTextureData[item->textureIndex];
		uint8_t* rowStart = (uint8_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint8_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0)
					{
						uint8_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						pixel = g_additivePaletteTable[texel + pixel * PALETTE_COMBINE_TABLE_STRIDE];
					}
				}
				else
				{
					UV_SPAN_SETUP_8();

					do
					{
						uint8_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0)
							*pixel = g_additivePaletteTable[texel + *pixel * PALETTE_COMBINE_TABLE_STRIDE];
						u += uStep;
						v += vStep;
						pixel++;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			rowStart += g_backBufferPitchPixels;
			scanline++;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x004776C0 [PROVISIONAL]
	void RasterizeSolidQuad8(const PointI* point0, const PointI* point1, const PointI* point2, const PointI* point3, uint32_t colourPair)
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

		RASTERIZE_SOLID_EDGE(point0, point1, edge01Done8);
		RASTERIZE_SOLID_EDGE(point1, point2, edge12Done8);
		RASTERIZE_SOLID_EDGE(point2, point3, edge23Done8);
		{
			int32_t edgeY;
			int32_t edgeEndY;
			int32_t edgeStartX;
			int32_t edgeEndX;
			bool rasterizeFinalEdge = false;
			if (point3->y < point0->y)
			{
				if (point3->y <= Toy2::g_screenClipBottom && point0->y >= Toy2::g_screenClipTop)
				{
					edgeY = point3->y;
					edgeEndY = point0->y;
					edgeStartX = point3->x;
					edgeEndX = point0->x;
					rasterizeFinalEdge = true;
				}
			}
			else if (point0->y < point3->y && point0->y <= Toy2::g_screenClipBottom && point3->y >= Toy2::g_screenClipTop)
			{
				edgeY = point0->y;
				edgeEndY = point3->y;
				edgeStartX = point0->x;
				edgeEndX = point3->x;
				rasterizeFinalEdge = true;
			}

			if (rasterizeFinalEdge)
			{
				int32_t edgeXStep = (edgeEndX - edgeStartX) * SOLID_EDGE_X_ONE / (edgeEndY - edgeY);
				int32_t edgeXFixed = edgeStartX * SOLID_EDGE_X_ONE + SOLID_EDGE_X_HALF;
				if (edgeY < Toy2::g_screenClipTop)
				{
					edgeXFixed += (Toy2::g_screenClipTop - edgeY) * edgeXStep;
					edgeY = Toy2::g_screenClipTop;
				}

				ScanlineScratch* edgeScanline = &g_scanlineScratch[edgeY];
				do
				{
					if (g_skipOddScanlines == 0 || (edgeY & 1) == 0)
					{
						if (edgeScanline->populated == 0)
						{
							edgeScanline->rightXFixed = edgeXFixed;
							edgeScanline->leftXFixed = edgeXFixed;
							edgeScanline->populated = 1;
						}
						else if (edgeXFixed < edgeScanline->leftXFixed)
						{
							edgeScanline->leftXFixed = edgeXFixed;
						}
						else if (edgeXFixed > edgeScanline->rightXFixed)
						{
							edgeScanline->rightXFixed = edgeXFixed;
						}
					}
					edgeScanline++;
					edgeXFixed += edgeXStep;
					edgeY++;
				} while (edgeY <= edgeEndY && edgeY <= Toy2::g_screenClipBottom);
				return;
			}
		}

		uint8_t* rowStart = (uint8_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		ScanlineScratch* span = scanline;
		int32_t rowsRemaining = scanlineCount;
		do
		{
			if (span->populated != 0 && span->leftXFixed <= Toy2::g_screenClipRightFixed && span->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = span->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = span->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX != rightX)
				{
					if (leftX < Toy2::g_screenClipLeft)
						leftX = Toy2::g_screenClipLeft;
					if (rightX > Toy2::g_screenClipRight)
						rightX = Toy2::g_screenClipRight;

					uint8_t* pixel = rowStart - Toy2::g_screenClipLeft + leftX;
					int32_t pixelCount = rightX - leftX + 1;
					if (((uint32_t)pixel & 3) != 0)
					{
						*pixel++ = (uint8_t)colourPair;
						pixelCount--;
					}
					if (pixelCount > 3)
					{
						Nu3D::MemSet32Util(pixel, pixelCount / 4, colourPair);
						int32_t filledPixels = pixelCount & ~3;
						pixel += filledPixels;
						pixelCount -= filledPixels;
					}
					if (pixelCount != 0)
						*pixel = (uint8_t)colourPair;
				}
			}

			rowStart += g_backBufferPitchPixels;
			span++;
			rowsRemaining--;
		} while (rowsRemaining != 0);
	}

	// The 8-bit form of UnkRenderAPI13 (RGB555) and UnkRenderAPI25 (RGB565). It lights the
	// texel through g_paletteColourOffsetTable and combines the overlay through the palette
	// blend tables, so the three edge walks and the four span modes are the same.
	// retail-duplicate: retail writes the overlay mode select and the row range scan into each renderer; UnkRenderAPI13 (0x0045E390) holds the RGB555 form.
	// retail-duplicate: retail writes the left clip span setup into each span loop; RasterizeBlend75TexturedPolygon555 (0x0045B0B0) holds the RGB555 form.
	// retail-duplicate: retail writes the row advance and the lit span setup into each renderer; UnkRenderAPI13 (0x0045E390) holds the RGB555 form.
	// retail-duplicate: retail writes the row advance and the additive span setup into each renderer; UnkRenderAPI13 (0x0045E390) holds the RGB555 form.
	// retail-duplicate: retail writes the blend 50 span setup into each renderer; UnkRenderAPI13 (0x0045E390) holds the RGB555 form.
	// FUNCTION: TOY2 0x00477EB0 [PROVISIONAL]
	void UnkRenderAPI34(SoftwareRenderItem* item)
	{
		// Select how the overlay texture combines with this polygon.
		int32_t overlayMode;
		// Untextured overlay: plain lit polygon.
		if (item->renderFlags & SOFTWARE_RENDER_NO_OVERLAY)
			overlayMode = OVERLAY_MODE_NONE;
		// These levels add the overlay.
		else if (Toy2::g_levelFileIndex == 15 || Toy2::g_levelFileIndex == 11)
			overlayMode = OVERLAY_MODE_ADDITIVE;
		// These levels blend the overlay at 50 percent.
		else if (Toy2::g_levelFileIndex == 7 || Toy2::g_levelFileIndex == 8)
			overlayMode = OVERLAY_MODE_BLEND_50;
		// This level adds the overlay only where the first texel is the transparent entry.
		else if (Toy2::g_levelFileIndex == 9)
			overlayMode =
				((uint8_t*)g_softwareTextureData[item->textureIndex])[((item->vertices[0].v >> 8) & k_upperByteMask)
					+ (item->vertices[0].u >> k_fixedPointShift)]
					== 0
				? OVERLAY_MODE_ADDITIVE
				: OVERLAY_MODE_LIT;
		// Default: light the overlay.
		else
			overlayMode = OVERLAY_MODE_LIT;

		// Find the rows that the polygon covers.
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		// Walk the edges into the scanline table.
		const SoftwareOverlayItem* overlayItem = (const SoftwareOverlayItem*)item;
		// Lit edges without overlay coordinates.
		if (overlayMode == OVERLAY_MODE_NONE)
		{
			RASTERIZE_LIT_EDGE(&item->vertices[0], &item->vertices[1], litEdge01Done8);
			RASTERIZE_LIT_EDGE(&item->vertices[1], &item->vertices[2], litEdge12Done8);
			if (item->renderFlags & SOFTWARE_RENDER_QUAD)
			{
				RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[3], litEdge23Done8);
				RASTERIZE_LIT_EDGE(&item->vertices[3], &item->vertices[0], litEdge30Done8);
			}
			else
			{
				RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[0], litEdge20Done8);
			}
		}
		// Overlay edges without lighting.
		else if (overlayMode > OVERLAY_MODE_LIT)
		{
			RASTERIZE_OVERLAY_EDGE(&item->vertices[0], &item->vertices[1], overlayItem->overlayUV[0], overlayItem->overlayUV[1], overlayEdge01Done8);
			RASTERIZE_OVERLAY_EDGE(&item->vertices[1], &item->vertices[2], overlayItem->overlayUV[1], overlayItem->overlayUV[2], overlayEdge12Done8);
			if (item->renderFlags & SOFTWARE_RENDER_QUAD)
			{
				RASTERIZE_OVERLAY_EDGE(&item->vertices[2], &item->vertices[3], overlayItem->overlayUV[2], overlayItem->overlayUV[3], overlayEdge23Done8);
				RASTERIZE_OVERLAY_EDGE(&item->vertices[3], &item->vertices[0], overlayItem->overlayUV[3], overlayItem->overlayUV[0], overlayEdge30Done8);
			}
			else
			{
				RASTERIZE_OVERLAY_EDGE(&item->vertices[2], &item->vertices[0], overlayItem->overlayUV[2], overlayItem->overlayUV[0], overlayEdge20Done8);
			}
		}
		// Lit edges with overlay coordinates.
		else
		{
			RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[0], &item->vertices[1], overlayItem->overlayUV[0], overlayItem->overlayUV[1], litOverlayEdge01Done8);
			RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[1], &item->vertices[2], overlayItem->overlayUV[1], overlayItem->overlayUV[2], litOverlayEdge12Done8);
			if (item->renderFlags & SOFTWARE_RENDER_QUAD)
			{
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[2], &item->vertices[3], overlayItem->overlayUV[2], overlayItem->overlayUV[3], litOverlayEdge23Done8);
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[3], &item->vertices[0], overlayItem->overlayUV[3], overlayItem->overlayUV[0], litOverlayEdge30Done8);
			}
			else
			{
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[2], &item->vertices[0], overlayItem->overlayUV[2], overlayItem->overlayUV[0], litOverlayEdge20Done8);
			}
		}

		uint8_t* overlay = (uint8_t*)g_softwareTextureData[OVERLAY_TEXTURE_INDEX];
		uint8_t* blend50Table = g_paletteBlend50Table;
		uint8_t* additiveTable = g_additivePaletteTable;
		uint8_t* texture = (uint8_t*)g_softwareTextureData[item->textureIndex];
		uint8_t* rowStart = (uint8_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
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
						int32_t u = scanline->leftInterpolants[0];
						int32_t blue = scanline->leftInterpolants[2];
						int32_t v = scanline->leftInterpolants[1];
						int32_t red = scanline->leftInterpolants[4];
						int32_t green = scanline->leftInterpolants[3];
						int32_t width = rightX - leftX;
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						int32_t blueStep = (scanline->rightInterpolants[2] - blue) / width;
						int32_t greenStep = (scanline->rightInterpolants[3] - green) / width;
						int32_t redStep = (scanline->rightInterpolants[4] - red) / width;
						CLIP_SPAN_TO_SCREEN(uint8_t* pixel, u += clippedPixels * uStep; v += clippedPixels * vStep; blue += clippedPixels * blueStep;
							green += clippedPixels * greenStep;
							red += clippedPixels * redStep;
							pixel = rowStart);

						for (; pixelCount > 0; pixelCount--)
						{
							uint8_t texel = texture[((v >> 8) & k_upperByteMask) + (u >> k_fixedPointShift)];
							*pixel = additiveTable[*pixel * PALETTE_COMBINE_TABLE_STRIDE
								+ g_paletteColourOffsetTable[PALETTE_COLOUR_OFFSET(blue, green, red) + texel * PALETTE_COLOUR_OFFSET_STRIDE]];
							u += uStep;
							v += vStep;
							blue += blueStep;
							green += greenStep;
							red += redStep;
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

		if (overlayMode == OVERLAY_MODE_ADDITIVE)
		{
			// Add the overlay texel to the back buffer.
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX != rightX)
					{
						int32_t overlayU = scanline->rightInterpolants[OVERLAY_LEFT_U];
						int32_t overlayV = scanline->rightInterpolants[OVERLAY_LEFT_V];
						int32_t width = rightX - leftX;
						int32_t overlayUStep = (scanline->rightInterpolants[OVERLAY_RIGHT_U] - overlayU) / width;
						int32_t overlayVStep = (scanline->rightInterpolants[OVERLAY_RIGHT_V] - overlayV) / width;
						int32_t pixelCount = width;
						uint8_t* pixel;
						if (leftX < Toy2::g_screenClipLeft)
						{
							overlayU += overlayUStep * (Toy2::g_screenClipLeft - leftX);
							overlayV += overlayVStep * (Toy2::g_screenClipLeft - leftX);
							pixel = rowStart;
							if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else
								pixelCount = rightX - Toy2::g_screenClipLeft;
						}
						else
						{
							pixel = rowStart + leftX - Toy2::g_screenClipLeft;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_screenClipRight - leftX;
							else if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_screenClipRight - leftX + 1;
						}

						for (; pixelCount > 0; pixelCount--)
						{
							int32_t overlayRow = overlayU >> k_fixedPointShift;
							overlayU += overlayUStep;
							*pixel = additiveTable[*pixel * PALETTE_COMBINE_TABLE_STRIDE + overlay[((overlayV >> 8) & k_upperByteMask) + overlayRow]];
							overlayV += overlayVStep;
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

		if (overlayMode == OVERLAY_MODE_BLEND_50)
		{
			// Average the overlay texel with the back buffer.
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
					if (leftX != rightX)
					{
						int32_t overlayU = scanline->rightInterpolants[OVERLAY_LEFT_U];
						int32_t overlayV = scanline->rightInterpolants[OVERLAY_LEFT_V];
						int32_t width = rightX - leftX;
						int32_t overlayUStep = (scanline->rightInterpolants[OVERLAY_RIGHT_U] - overlayU) / width;
						int32_t overlayVStep = (scanline->rightInterpolants[OVERLAY_RIGHT_V] - overlayV) / width;
						int32_t pixelCount = width;
						uint8_t* pixel;
						if (leftX < Toy2::g_screenClipLeft)
						{
							overlayU += overlayUStep * (Toy2::g_screenClipLeft - leftX);
							overlayV += overlayVStep * (Toy2::g_screenClipLeft - leftX);
							pixel = rowStart;
							if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else
								pixelCount = rightX - Toy2::g_screenClipLeft;
						}
						else
						{
							pixel = rowStart + leftX - Toy2::g_screenClipLeft;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_screenClipRight - leftX;
							else if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_screenClipRight - leftX + 1;
						}

						for (; pixelCount > 0; pixelCount--)
						{
							*pixel = blend50Table[*pixel * PALETTE_COMBINE_TABLE_STRIDE
								+ overlay[((overlayV >> 8) & k_upperByteMask) + (overlayU >> k_fixedPointShift)]];
							overlayV += overlayVStep;
							overlayU += overlayUStep;
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

		// Add the lit texel to the overlay texel.
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					// A one pixel span takes the texture coordinates from the left edge and the
					// colour from the right edge.
					uint8_t texel = texture[((scanline->leftInterpolants[1] >> 8) & k_upperByteMask) + (scanline->leftInterpolants[0] >> k_fixedPointShift)];
					uint8_t litTexel = g_paletteColourOffsetTable
						[PALETTE_COLOUR_OFFSET(scanline->rightInterpolants[2], scanline->rightInterpolants[3], scanline->rightInterpolants[4])
							+ texel * PALETTE_COLOUR_OFFSET_STRIDE];
					uint8_t overlayTexel = overlay[((scanline->rightInterpolants[OVERLAY_LEFT_V] >> 8) & k_upperByteMask)
						+ (scanline->rightInterpolants[OVERLAY_LEFT_U] >> k_fixedPointShift)];
					rowStart[leftX - Toy2::g_screenClipLeft] = additiveTable[overlayTexel * PALETTE_COMBINE_TABLE_STRIDE + litTexel];
				}
				else
				{
					int32_t v = scanline->leftInterpolants[1];
					int32_t red = scanline->leftInterpolants[4];
					int32_t overlayV = scanline->rightInterpolants[OVERLAY_LEFT_V];
					int32_t green = scanline->leftInterpolants[3];
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t overlayU = scanline->rightInterpolants[OVERLAY_LEFT_U];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t blue = scanline->leftInterpolants[2];
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t blueStep = (scanline->rightInterpolants[2] - blue) / width;
					int32_t greenStep = (scanline->rightInterpolants[3] - green) / width;
					int32_t redStep = (scanline->rightInterpolants[4] - red) / width;
					int32_t overlayUStep = (scanline->rightInterpolants[OVERLAY_RIGHT_U] - overlayU) / width;
					int32_t overlayVStep = (scanline->rightInterpolants[OVERLAY_RIGHT_V] - overlayV) / width;
					int32_t pixelCount;
					uint8_t* pixel;
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
						pixel = rowStart;
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
						uint8_t texel = texture[((v >> 8) & k_upperByteMask) + (u >> k_fixedPointShift)];
						uint8_t overlayTexel = overlay[((overlayV >> 8) & k_upperByteMask) + (overlayU >> k_fixedPointShift)];
						*pixel = additiveTable[overlayTexel * PALETTE_COMBINE_TABLE_STRIDE
							+ g_paletteColourOffsetTable[PALETTE_COLOUR_OFFSET(blue, green, red) + texel * PALETTE_COLOUR_OFFSET_STRIDE]];
						v += vStep;
						u += uStep;
						blue += blueStep;
						green += greenStep;
						red += redStep;
						overlayU += overlayUStep;
						overlayV += overlayVStep;
						pixelCount--;
						pixel++;
					} while (pixelCount > 0);
				}
			}
			rowStart += g_backBufferPitchPixels;
			scanline++;
			scanlineCount--;
		} while (scanlineCount != 0);
	}
}

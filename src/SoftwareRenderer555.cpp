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

// The RGB555 rasterizers: retail holds 0x00454D30 through 0x0045E390 as one
// object, in the order of this file.
namespace SoftwareRenderer
{
// Combines one texel with the interpolated light of a span into an RGB555 pixel. Each ramp
// table holds the lit value of every level of one channel, so one table read lights a channel.
#define LIT_TEXEL_555(texel, red, green, blue)                                                                                                                 \
	(g_greenRampFull[(((texel) >> 5) & k_fiveBitChannelMask) + ((green) >> k_fixedPointShift)] + g_redRampFull[((texel) >> 10) + ((red) >> k_fixedPointShift)] \
		+ g_blueRampFull[((texel) & k_fiveBitChannelMask) + ((blue) >> k_fixedPointShift)])

	// Texel value that the colour key makes transparent in RGB555 (pure green).
	const uint16_t COLOUR_KEY_TEXEL_555 = 0x3E0;

	// Masks that clear the low bits of every RGB555 channel, so a divide holds each channel
	// inside its own field instead of bleeding into the channel below it.
	const uint16_t k_rgb555HalfMask = 0x7BDE;
	const uint16_t k_rgb555QuarterMask = 0x739C;

// Combines one texel with the interpolated light of a span at a quarter of its intensity. The Low
// ramp tables hold every level of one channel at a quarter, so one table read lights a channel and
// scales it at the same time.
#define LIT_TEXEL_555_QUARTER(texel, red, green, blue)                                                                                                       \
	(g_greenRampLow[(((texel) >> 5) & k_fiveBitChannelMask) + ((green) >> k_fixedPointShift)] + g_redRampLow[((texel) >> 10) + ((red) >> k_fixedPointShift)] \
		+ g_blueRampLow[((texel) & k_fiveBitChannelMask) + ((blue) >> k_fixedPointShift)])

// Adds a quarter of the lit texel to three quarters of the pixel that the back buffer holds.
#define BLEND25_LIT_TEXEL_555(destination, texel, red, green, blue) \
	(LIT_TEXEL_555_QUARTER(texel, red, green, blue) + ((destination) & k_rgb555HalfMask) / 2 + ((destination) & k_rgb555QuarterMask) / 4)

// Sets up one lit span that blends with the back buffer: the interpolants at the left end of the
// span with their steps, then the first pixel with the pixel count that the clip leaves. It reads
// spanRow, rowStart, leftX and rightX of the span loop.
#define LIT_BLENDED_SPAN_SETUP()                                                                                                            \
	LIT_SPAN_INTERPOLANTS();                                                                                                                \
	CLIP_SPAN_TO_SCREEN(uint16_t* pixel = rowStart, u += clippedPixels * uStep; v += clippedPixels * vStep; red += clippedPixels * redStep; \
		green += clippedPixels * greenStep;                                                                                                 \
		blue += clippedPixels * blueStep)

// Steps the texture UV, the three colour interpolants and the destination of a lit, blended span
// on by one pixel. The loop header steps the count. It reads the names that
// LIT_BLENDED_SPAN_SETUP declares.
#define ADVANCE_LIT_BLENDED_SPAN() \
	u += uStep;                    \
	v += vStep;                    \
	red += redStep;                \
	green += greenStep;            \
	blue += blueStep;              \
	pixel++

// Blends one lit texel into the pixel that the back buffer holds.
#define WRITE_BLEND25_TEXEL_555() *pixel = BLEND25_LIT_TEXEL_555(*pixel, texel, red, green, blue)

// Blends one lit texel into the pixel that the back buffer holds, except where the texel is the
// colour key, which holds the pixel that the back buffer already states.
#define WRITE_BLEND25_TEXEL_555_COLOUR_KEY() \
	if (texel != COLOUR_KEY_TEXEL_555)       \
	WRITE_BLEND25_TEXEL_555()

// Draws every row of a lit polygon that the scanline table holds and blends the texels of each
// span with the back buffer. writeBlendedTexel names the statement that blends one texel into the
// pixel it holds; it reads texel, red, green and blue. A colour key mode wraps that statement in
// the test that holds the back buffer where the texel is the key.
#define DRAW_LIT_BLENDED_ROWS(writeBlendedTexel)                                                                                                   \
	do                                                                                                                                             \
	{                                                                                                                                              \
		if (spanRow->populated != 0 && spanRow->leftXFixed <= Toy2::g_screenClipRightFixed && spanRow->rightXFixed >= Toy2::g_screenClipLeftFixed) \
		{                                                                                                                                          \
			int32_t leftX = spanRow->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;                                                                     \
			int32_t rightX = spanRow->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;                                                                   \
			if (leftX != rightX)                                                                                                                   \
			{                                                                                                                                      \
				LIT_BLENDED_SPAN_SETUP();                                                                                                          \
				for (; pixelCount > 0; pixelCount--)                                                                                               \
				{                                                                                                                                  \
					uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];                                                    \
					writeBlendedTexel;                                                                                                             \
					ADVANCE_LIT_BLENDED_SPAN();                                                                                                    \
				}                                                                                                                                  \
			}                                                                                                                                      \
		}                                                                                                                                          \
		spanRow++;                                                                                                                                 \
		rowStart += g_backBufferPitchPixels;                                                                                                       \
		rowsLeft--;                                                                                                                                \
	} while (rowsLeft != 0)

	// Level 9 texel that selects OVERLAY_MODE_ADDITIVE in UnkRenderAPI13 (pure green in RGB555).
	const uint16_t OVERLAY_MARKER_TEXEL_555 = 0x3E0;

// Adds two RGB555 pixels and clamps each channel at its maximum.
#define ADD_SATURATED_555(first, second, result)                                           \
	do                                                                                     \
	{                                                                                      \
		int32_t redSum = ((first) & RGB555_RED_MASK) + ((second) & RGB555_RED_MASK);       \
		if (redSum > RGB555_RED_MASK)                                                      \
			redSum = RGB555_RED_MASK;                                                      \
		int32_t greenSum = ((first) & RGB555_GREEN_MASK) + ((second) & RGB555_GREEN_MASK); \
		if (greenSum > RGB555_GREEN_MASK)                                                  \
			greenSum = RGB555_GREEN_MASK;                                                  \
		int32_t blueSum = ((first) & RGB555_BLUE_MASK) + ((second) & RGB555_BLUE_MASK);    \
		if (blueSum > RGB555_BLUE_MASK)                                                    \
			blueSum = RGB555_BLUE_MASK;                                                    \
		(result) = (uint16_t)(blueSum | greenSum | redSum);                                \
	} while (0)

	// The blended polygons walk their own edges. RASTERIZE_LIT_EDGE_WITH_END carries a
	// third interpolant through the same slots, and folding this walk into the pointer
	// form of RASTERIZE_TEXTURED_EDGE_EXCLUSIVE moved 0x0045A5C0 from 48.19% to 27.86%,
	// so retail states the walk twice.
	// retail-duplicate: shared RASTERIZE_TEXTURED_EDGE_EXCLUSIVE form scored 27.86% at attempt 8, against 48.19% for this form
#define RASTERIZE_UV_EDGE(vertexA, vertexB, doneLabel)                                                                           \
	do                                                                                                                           \
	{                                                                                                                            \
		int32_t edgeY;                                                                                                           \
		int32_t edgeEndY;                                                                                                        \
		int32_t edgeX;                                                                                                           \
		int32_t edgeU;                                                                                                           \
		int32_t edgeV;                                                                                                           \
		int32_t edgeHeight;                                                                                                      \
		int32_t edgeXStep;                                                                                                       \
		int32_t edgeUStep;                                                                                                       \
		int32_t edgeVStep;                                                                                                       \
		int32_t edgeXFixed;                                                                                                      \
		int32_t clippedRows;                                                                                                     \
		ScanlineScratch* edgeScanline;                                                                                           \
		if ((vertexA)->y < (vertexB)->y)                                                                                         \
		{                                                                                                                        \
			if ((vertexA)->y > Toy2::g_screenClipBottom || (vertexB)->y < Toy2::g_screenClipTop)                                 \
				goto doneLabel;                                                                                                  \
			edgeY = (vertexA)->y;                                                                                                \
			edgeEndY = (vertexB)->y;                                                                                             \
			edgeX = (vertexA)->x;                                                                                                \
			edgeHeight = edgeEndY - edgeY;                                                                                       \
			edgeXStep = ((vertexB)->x - edgeX) * 0x400 / edgeHeight;                                                             \
			edgeU = (vertexA)->u;                                                                                                \
			edgeV = (vertexA)->v;                                                                                                \
			edgeUStep = ((vertexB)->u - edgeU) / edgeHeight;                                                                     \
			edgeVStep = ((vertexB)->v - edgeV) / edgeHeight;                                                                     \
		}                                                                                                                        \
		else                                                                                                                     \
		{                                                                                                                        \
			if ((vertexB)->y >= (vertexA)->y || (vertexB)->y > Toy2::g_screenClipBottom || (vertexA)->y < Toy2::g_screenClipTop) \
				goto doneLabel;                                                                                                  \
			edgeY = (vertexB)->y;                                                                                                \
			edgeEndY = (vertexA)->y;                                                                                             \
			edgeX = (vertexB)->x;                                                                                                \
			edgeHeight = edgeEndY - edgeY;                                                                                       \
			edgeXStep = ((vertexA)->x - edgeX) * 0x400 / edgeHeight;                                                             \
			edgeU = (vertexB)->u;                                                                                                \
			edgeV = (vertexB)->v;                                                                                                \
			edgeUStep = ((vertexA)->u - edgeU) / edgeHeight;                                                                     \
			edgeVStep = ((vertexA)->v - edgeV) / edgeHeight;                                                                     \
		}                                                                                                                        \
		edgeXFixed = edgeX * 0x400 + 0x200;                                                                                      \
		if (edgeY < Toy2::g_screenClipTop)                                                                                       \
		{                                                                                                                        \
			clippedRows = Toy2::g_screenClipTop - edgeY;                                                                         \
			edgeXFixed += edgeXStep * clippedRows;                                                                               \
			edgeU += edgeUStep * clippedRows;                                                                                    \
			edgeV += edgeVStep * clippedRows;                                                                                    \
			edgeY = Toy2::g_screenClipTop;                                                                                       \
		}                                                                                                                        \
		edgeScanline = &g_scanlineScratch[edgeY];                                                                                \
		do                                                                                                                       \
		{                                                                                                                        \
			if (g_skipOddScanlines == 0 || (edgeY & 1) == 0)                                                                     \
			{                                                                                                                    \
				if (edgeScanline->populated == 0)                                                                                \
				{                                                                                                                \
					edgeScanline->rightXFixed = edgeXFixed;                                                                      \
					edgeScanline->leftXFixed = edgeXFixed;                                                                       \
					edgeScanline->rightInterpolants[0] = edgeU;                                                                  \
					edgeScanline->leftInterpolants[0] = edgeU;                                                                   \
					edgeScanline->rightInterpolants[1] = edgeV;                                                                  \
					edgeScanline->leftInterpolants[1] = edgeV;                                                                   \
					edgeScanline->populated = 1;                                                                                 \
				}                                                                                                                \
				else if (edgeXFixed < edgeScanline->leftXFixed)                                                                  \
				{                                                                                                                \
					edgeScanline->leftXFixed = edgeXFixed;                                                                       \
					edgeScanline->leftInterpolants[0] = edgeU;                                                                   \
					edgeScanline->leftInterpolants[1] = edgeV;                                                                   \
				}                                                                                                                \
				else if (edgeXFixed > edgeScanline->rightXFixed)                                                                 \
				{                                                                                                                \
					edgeScanline->rightXFixed = edgeXFixed;                                                                      \
					edgeScanline->rightInterpolants[0] = edgeU;                                                                  \
					edgeScanline->rightInterpolants[1] = edgeV;                                                                  \
				}                                                                                                                \
			}                                                                                                                    \
			edgeScanline++;                                                                                                      \
			edgeXFixed += edgeXStep;                                                                                             \
			edgeU += edgeUStep;                                                                                                  \
			edgeV += edgeVStep;                                                                                                  \
			edgeY++;                                                                                                             \
		} while (edgeY < edgeEndY && edgeY <= Toy2::g_screenClipBottom);                                                         \
	doneLabel:;                                                                                                                  \
	} while (0)

	// Draws a Gouraud lit, textured triangle or quad into the RGB555 back buffer.
	// FUNCTION: TOY2 0x00454D30 [PROVISIONAL]
	void UnkRenderAPI5(SoftwareRenderItem* item)
	{
		// Find the rows that the polygon covers.
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ClearScanlineFlags(&g_scanlineScratch[topY], scanlineCount);

		// Walk the edges into the scanline table.
		RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[0], &item->vertices[1], litEdge01DoneTextured555);
		RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[1], &item->vertices[2], litEdge12DoneTextured555);
		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[2], &item->vertices[3], litEdge23DoneTextured555);
			RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[3], &item->vertices[0], litEdge30DoneTextured555);
		}
		else
		{
			RASTERIZE_LIT_EDGE_INCLUSIVE(&item->vertices[2], &item->vertices[0], litEdge20DoneTextured555);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		ScanlineScratch* spanRow = &g_scanlineScratch[topY];
		int32_t rowsLeft = scanlineCount;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			// Hold the back buffer where the texel is the colour key.
			DRAW_LIT_TEXTURED_COLOUR_KEY_ROWS(LIT_TEXEL_555, COLOUR_KEY_TEXEL_555);
			return;
		}

		// Write every texel of the span.
		DRAW_LIT_TEXTURED_ROWS(LIT_TEXEL_555);
	}

	// Draws a Gouraud lit, textured triangle or quad into the RGB555 back buffer at a quarter of its
	// intensity, over three quarters of the pixel that the back buffer holds.
	// FUNCTION: TOY2 0x004560E0 [PROVISIONAL]
	void UnkRenderAPI6(SoftwareRenderItem* item)
	{
		// Find the rows that the polygon covers.
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ClearScanlineFlags(&g_scanlineScratch[topY], scanlineCount);

		// Walk the edges into the scanline table.
		RASTERIZE_LIT_EDGE(&item->vertices[0], &item->vertices[1], litEdge01DoneBlend25_555);
		RASTERIZE_LIT_EDGE(&item->vertices[1], &item->vertices[2], litEdge12DoneBlend25_555);
		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[3], litEdge23DoneBlend25_555);
			RASTERIZE_LIT_EDGE(&item->vertices[3], &item->vertices[0], litEdge30DoneBlend25_555);
		}
		else
		{
			RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[0], litEdge20DoneBlend25_555);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		ScanlineScratch* spanRow = &g_scanlineScratch[topY];
		int32_t rowsLeft = scanlineCount;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			// Hold the back buffer where the texel is the colour key.
			DRAW_LIT_BLENDED_ROWS(WRITE_BLEND25_TEXEL_555_COLOUR_KEY());
			return;
		}

		// Blend every texel of the span.
		DRAW_LIT_BLENDED_ROWS(WRITE_BLEND25_TEXEL_555());
	}

	// STUB: TOY2 0x00457440
	void UnkRenderAPI7(SoftwareRenderItem* item) {}

	// STUB: TOY2 0x00458770
	void UnkRenderAPI8(SoftwareRenderItem* item) {}

	// FUNCTION: TOY2 0x00459AB0 [PROVISIONAL]
	void RasterizeBlend25TexturedPolygon555(SoftwareRenderItem* item)
	{
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		RASTERIZE_UV_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneBlend25_555);
		RASTERIZE_UV_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneBlend25_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_UV_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneBlend25_555);
		}
		else
		{
			RASTERIZE_UV_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneBlend25_555);
			RASTERIZE_UV_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneBlend25_555);
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
						CLIP_SPAN_TO_SCREEN(uint16_t* pixel = rowStart, u += clippedPixels * uStep; v += clippedPixels * vStep);

						for (; pixelCount > 0; pixelCount--)
						{
							uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x3E0)
							{
								*pixel = (*pixel & 0x7BDE) / 2 + (*pixel & 0x739C) / 4 + (texel & 0x739C) / 4;
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
					CLIP_SPAN_TO_SCREEN(uint16_t* pixel = rowStart, v += clippedPixels * vStep; u += clippedPixels * uStep);

					for (; pixelCount > 0; pixelCount--)
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						*pixel = (*pixel & 0x7BDE) / 2 + (*pixel & 0x739C) / 4 + (texel & 0x739C) / 4;
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

	// FUNCTION: TOY2 0x0045A5C0 [PROVISIONAL]
	void RasterizeBlend50TexturedPolygon555(SoftwareRenderItem* item)
	{
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		RASTERIZE_UV_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneBlend50_555);
		RASTERIZE_UV_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneBlend50_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_UV_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneBlend50_555);
		}
		else
		{
			RASTERIZE_UV_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneBlend50_555);
			RASTERIZE_UV_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneBlend50_555);
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
						uint16_t* pixel = rowStart;
						int32_t u = scanline->leftInterpolants[0];
						int32_t v = scanline->leftInterpolants[1];
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						int32_t pixelCount = width;
						if (leftX < Toy2::g_screenClipLeft)
						{
							int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
							u += clippedPixels * uStep;
							v += clippedPixels * vStep;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth;
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
							uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x3E0)
								*pixel = (*pixel >> 1 & RGB555_HALF_MASK) + (texel >> 1 & RGB555_HALF_MASK);
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
					uint16_t* pixel = rowStart;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
					if (leftX < Toy2::g_screenClipLeft)
					{
						int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
						v += clippedPixels * vStep;
						u += clippedPixels * uStep;
						if (rightX == Toy2::g_screenClipRight)
							pixelCount = Toy2::g_softWindowWidth - 1;
						else if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_softWindowWidth;
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
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						*pixel = (*pixel >> 1 & RGB555_HALF_MASK) + (texel >> 1 & RGB555_HALF_MASK);
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

	// FUNCTION: TOY2 0x0045B0B0 [PROVISIONAL]
	void RasterizeBlend75TexturedPolygon555(SoftwareRenderItem* item)
	{
		CLIP_POLYGON_ROW_RANGE(item, topY, bottomY);

		int32_t scanlineCount = bottomY - topY + 1;
		ScanlineScratch* scanline = &g_scanlineScratch[topY];
		ClearScanlineFlags(scanline, scanlineCount);

		RASTERIZE_UV_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneBlend75_555);
		RASTERIZE_UV_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneBlend75_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_UV_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneBlend75_555);
		}
		else
		{
			RASTERIZE_UV_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneBlend75_555);
			RASTERIZE_UV_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneBlend75_555);
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
							if (texel != 0x3E0)
							{
								*pixel = (texel & 0x7BDE) / 2 + (*pixel & 0x739C) / 4 + (texel & 0x739C) / 4;
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
						*pixel = (texel & 0x7BDE) / 2 + (*pixel & 0x739C) / 4 + (texel & 0x739C) / 4;
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

	// FUNCTION: TOY2 0x0045BBC0 [PROVISIONAL]
	void RasterizeTexturedPolygon555(SoftwareRenderItem* item) {
#define POLYGON_COLOUR_KEY 0x3E0
#include "SoftwareRendererTexturedPoly.inc"
#undef POLYGON_COLOUR_KEY
	}

	// FUNCTION: TOY2 0x0045C6B0 [PROVISIONAL]
	void RasterizeSubtractiveTexturedPolygon555(SoftwareRenderItem* item) {
#define POLYGON_COLOUR_KEY 0x3E0
#define POLYGON_RED_MASK 0x7C00
#define POLYGON_GREEN_MASK 0x3E0
#define POLYGON_RED_FLOOR 0x400
#include "SoftwareRendererSubtractiveTexturedPoly.inc"
#undef POLYGON_RED_FLOOR
#undef POLYGON_GREEN_MASK
#undef POLYGON_RED_MASK
#undef POLYGON_COLOUR_KEY
	}

	// FUNCTION: TOY2 0x0045D110 [PROVISIONAL]
	void RasterizeAdditiveTexturedPolygon555(SoftwareRenderItem* item) {
#define POLYGON_COLOUR_KEY 0x3E0
#define POLYGON_RED_MASK 0x7C00
#define POLYGON_GREEN_MASK 0x3E0
#include "SoftwareRendererAdditiveTexturedPoly.inc"
#undef POLYGON_GREEN_MASK
#undef POLYGON_RED_MASK
#undef POLYGON_COLOUR_KEY
	}

	// FUNCTION: TOY2 0x0045DB80 [PROVISIONAL]
	void RasterizeTexturedRect555(SoftwareRenderItem* item)
	{
		uint16_t flags = item->renderFlags;
		int32_t useColourOffset = flags & SOFTWARE_RENDER_COLOUR_OFFSET;
		int32_t rightU = --item->vertices[2].u;
		int32_t bottomV = --item->vertices[2].v;

		int32_t redRampOffset;
		int32_t greenRampOffset;
		int32_t blueRampOffset;
		if (useColourOffset != 0)
		{
			redRampOffset = item->vertices[0].blue >> k_fixedPointShift;
			greenRampOffset = item->vertices[0].green >> k_fixedPointShift;
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
						uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0x3E0)
						{
							uint16_t destinationPixel = *destination;
							uint16_t litTexel = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + blueRampOffset];
							uint16_t red = (destinationPixel & 0x7C00) + (litTexel & 0x7C00);
							if (red > 0x7C00)
								red = 0x7C00;
							uint16_t green = (destinationPixel & 0x3E0) + (litTexel & 0x3E0);
							if (green > 0x3E0)
								green = 0x3E0;
							uint16_t blue = (destinationPixel & 0x1F) + (litTexel & 0x1F);
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
				int32_t remaining = rowWidth;
				do
				{
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x3E0)
					{
						uint16_t destinationPixel = *destination;
						uint16_t red = (destinationPixel & 0x7C00) + (texel & 0x7C00);
						if (red > 0x7C00)
							red = 0x7C00;
						uint16_t green = (destinationPixel & 0x3E0) + (texel & 0x3E0);
						if (green > 0x3E0)
							green = 0x3E0;
						uint16_t blue = (destinationPixel & 0x1F) + (texel & 0x1F);
						if (blue > 0x1F)
							blue = 0x1F;
						*destination = red | green | blue;
					}
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
						uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0x3E0)
						{
							uint32_t destinationPixel = *destination;
							uint32_t sourcePixel = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + blueRampOffset];
							int32_t red = (destinationPixel & 0x7C00) - (sourcePixel & 0x7C00);
							if (red < 0x400)
								red = 0;
							int32_t green = (destinationPixel & 0x3E0) - (sourcePixel & 0x3E0);
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
				int32_t remaining = rowWidth;
				do
				{
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x3E0)
					{
						uint32_t destinationPixel = *destination;
						uint32_t sourcePixel = texel;
						int32_t red = (destinationPixel & 0x7C00) - (sourcePixel & 0x7C00);
						if (red < 0x400)
							red = 0;
						int32_t green = (destinationPixel & 0x3E0) - (sourcePixel & 0x3E0);
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
						uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
						if (texel != 0x3E0)
						{
							uint16_t litTexel = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + blueRampOffset];
							*destination = (*destination >> 1 & RGB555_HALF_MASK) + (litTexel >> 1 & RGB555_HALF_MASK);
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
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x3E0)
						*destination = (*destination >> 1 & RGB555_HALF_MASK) + (texel >> 1 & RGB555_HALF_MASK);
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
				int32_t rowU = u;
				int32_t remaining = width;
				do
				{
					uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
					if (texel != 0x3E0)
					{
						*destination = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
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
			int32_t remaining = rowWidth;
			do
			{
				uint16_t texel = texture[(rowU >> k_fixedPointShift) + ((v >> k_fixedPointShift) & k_textureCoordinateMax) * k_textureDimension];
				if (texel != 0x3E0)
					*destination = texel;
				destination++;
				rowU += uStep;
				remaining--;
			} while (remaining != 0);
			destination += g_backBufferPitchPixels - rowWidth;
			v += vStep;
			height--;
		} while (height != 0);
	}

	// FUNCTION: TOY2 0x0045E390 [PROVISIONAL]
	void UnkRenderAPI13(SoftwareRenderItem* item)
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
		// This level adds the overlay only where the marker texel is.
		else if (Toy2::g_levelFileIndex == 9)
			overlayMode =
				((uint16_t*)g_softwareTextureData[item->textureIndex])[((item->vertices[0].v >> 8) & k_upperByteMask)
					+ (item->vertices[0].u >> k_fixedPointShift)]
					== OVERLAY_MARKER_TEXEL_555
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
			RASTERIZE_LIT_EDGE(&item->vertices[0], &item->vertices[1], litEdge01Done);
			RASTERIZE_LIT_EDGE(&item->vertices[1], &item->vertices[2], litEdge12Done);
			if (item->renderFlags & SOFTWARE_RENDER_QUAD)
			{
				RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[3], litEdge23Done);
				RASTERIZE_LIT_EDGE(&item->vertices[3], &item->vertices[0], litEdge30Done);
			}
			else
			{
				RASTERIZE_LIT_EDGE(&item->vertices[2], &item->vertices[0], litEdge20Done);
			}
		}
		// Overlay edges without lighting.
		else if (overlayMode > OVERLAY_MODE_LIT)
		{
			RASTERIZE_OVERLAY_EDGE(&item->vertices[0], &item->vertices[1], overlayItem->overlayUV[0], overlayItem->overlayUV[1], overlayEdge01Done);
			RASTERIZE_OVERLAY_EDGE(&item->vertices[1], &item->vertices[2], overlayItem->overlayUV[1], overlayItem->overlayUV[2], overlayEdge12Done);
			if (item->renderFlags & SOFTWARE_RENDER_QUAD)
			{
				RASTERIZE_OVERLAY_EDGE(&item->vertices[2], &item->vertices[3], overlayItem->overlayUV[2], overlayItem->overlayUV[3], overlayEdge23Done);
				RASTERIZE_OVERLAY_EDGE(&item->vertices[3], &item->vertices[0], overlayItem->overlayUV[3], overlayItem->overlayUV[0], overlayEdge30Done);
			}
			else
			{
				RASTERIZE_OVERLAY_EDGE(&item->vertices[2], &item->vertices[0], overlayItem->overlayUV[2], overlayItem->overlayUV[0], overlayEdge20Done);
			}
		}
		// Lit edges with overlay coordinates.
		else
		{
			RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[0], &item->vertices[1], overlayItem->overlayUV[0], overlayItem->overlayUV[1], litOverlayEdge01Done);
			RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[1], &item->vertices[2], overlayItem->overlayUV[1], overlayItem->overlayUV[2], litOverlayEdge12Done);
			if (item->renderFlags & SOFTWARE_RENDER_QUAD)
			{
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[2], &item->vertices[3], overlayItem->overlayUV[2], overlayItem->overlayUV[3], litOverlayEdge23Done);
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[3], &item->vertices[0], overlayItem->overlayUV[3], overlayItem->overlayUV[0], litOverlayEdge30Done);
			}
			else
			{
				RASTERIZE_LIT_OVERLAY_EDGE(&item->vertices[2], &item->vertices[0], overlayItem->overlayUV[2], overlayItem->overlayUV[0], litOverlayEdge20Done);
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
							uint32_t litTexel = (uint16_t)(g_greenRampFull[((texel >> 5) & k_fiveBitChannelMask) + (green >> k_fixedPointShift)]
								+ g_redRampFull[(texel >> 10) + (blue >> k_fixedPointShift)]
								+ g_blueRampFull[(texel & k_fiveBitChannelMask) + (red >> k_fixedPointShift)]);
							ADD_SATURATED_555(destinationPixel, litTexel, *pixel);
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
							ADD_SATURATED_555(destinationPixel, overlayTexel, *pixel);
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
							*pixel = (overlay[((overlayV >> 8) & k_upperByteMask) + (overlayU >> k_fixedPointShift)] >> 1 & RGB555_HALF_MASK)
								+ (*pixel >> 1 & RGB555_HALF_MASK);
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
					uint32_t litTexel = (uint16_t)(g_greenRampFull[((texel >> 5) & k_fiveBitChannelMask) + (scanline->leftInterpolants[3] >> k_fixedPointShift)]
						+ g_redRampFull[(texel >> 10) + (scanline->leftInterpolants[2] >> k_fixedPointShift)]
						+ g_blueRampFull[(texel & k_fiveBitChannelMask) + (scanline->leftInterpolants[4] >> k_fixedPointShift)]);
					uint32_t overlayTexel = overlay[((scanline->rightInterpolants[OVERLAY_LEFT_V] >> 8) & k_upperByteMask)
						+ (scanline->rightInterpolants[OVERLAY_LEFT_U] >> k_fixedPointShift)];
					ADD_SATURATED_555(overlayTexel, litTexel, rowStart[leftX - Toy2::g_screenClipLeft]);
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
						uint32_t litTexel = (uint16_t)(g_greenRampFull[((texel >> 5) & k_fiveBitChannelMask) + (green >> k_fixedPointShift)]
							+ g_redRampFull[(texel >> 10) + (blue >> k_fixedPointShift)]
							+ g_blueRampFull[(texel & k_fiveBitChannelMask) + (red >> k_fixedPointShift)]);
						uint32_t overlayTexel = overlay[((overlayV >> 8) & k_upperByteMask) + (overlayU >> k_fixedPointShift)];
						ADD_SATURATED_555(overlayTexel, litTexel, *pixel);
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

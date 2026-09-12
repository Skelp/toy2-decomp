#ifndef SOFTWARERENDERERSPANS_H
#define SOFTWARERENDERERSPANS_H

#include "SoftwareRendererInternal.h"
#include "Toy2/Toy2.h"

// The edge walks and the span loops that every pixel format of the software
// rasterizer shares. Retail holds one object for each format, and each one
// compiles the same walks and loops for its own pixel size, so the macro
// bodies here are what the formats have in common and each format file adds
// only the texel arithmetic of its own pixels.
namespace SoftwareRenderer
{
	enum
	{
		k_textureCoordinateShift = 8,
		k_textureDimension = 0x100,
		k_textureCoordinateMax = k_textureDimension - 1,
		k_textureCoordinateFixedMax = k_textureCoordinateMax << k_textureCoordinateShift,
		k_lowerByteMask = 0x000000ff,
		k_upperByteMask = 0x0000ff00,
		k_rgbMask = 0x00ffffff,
		k_fiveBitChannelMask = 0x1f,
		k_fiveBitChannelLimit = k_fiveBitChannelMask + 1,
		k_colourScaleSubtableSize = 0x10000,
	};

	// Shift from 16.16 fixed point to the integer part.
	const int32_t k_fixedPointShift = 16;

	// Edge X values carry 10 fraction bits; the half unit centres a span on its pixel.
	enum
	{
		SOLID_EDGE_X_FRACTION_BITS = 10,
		SOLID_EDGE_X_ONE = 1 << SOLID_EDGE_X_FRACTION_BITS,
		SOLID_EDGE_X_HALF = SOLID_EDGE_X_ONE / 2
	};

	const int32_t RGB555_RED_MASK = 0x7C00;
	const int32_t RGB555_GREEN_MASK = 0x3E0;
	const int32_t RGB555_BLUE_MASK = 0x1F;
	// Channel mask of a pixel shifted right by one, for a 50 percent blend.
	const int32_t RGB555_HALF_MASK = 0x3DEF;

	const int32_t RGB565_RED_MASK = 0xF800;
	const int32_t RGB565_GREEN_MASK = 0x7E0;
	const int32_t RGB565_BLUE_MASK = 0x1F;
	// Channel mask of a pixel shifted right by one, for a 50 percent blend.
	const int32_t RGB565_HALF_MASK = 0x7BEF;

	// The palette combine tables are square: one row for each destination colour, and the
	// colour offset table holds a light level pair for each texel.
	enum
	{
		PALETTE_COMBINE_TABLE_STRIDE = 0x100,
		PALETTE_COLOUR_OFFSET_STRIDE = 0x200
	};

	// When this flag is set, polygon edge setup skips odd-numbered scanlines.
	// It stays out of the public header: a declaration there moves the stack
	// layout of Toy2::TarmacTrouble::Init (0x0042E600).
	extern int32_t g_skipOddScanlines;

	// The scanline table that every edge walk fills and every row loop reads.
	// SoftwareRenderer.cpp holds it, because every format writes through it.
	extern ScanlineScratch g_scanlineScratch[1024];

	void ClearScanlineFlags(ScanlineScratch* scanline, int32_t count);

	enum SoftwareOverlayMode
	{
		OVERLAY_MODE_NONE = -1, // add the lit texture to the back buffer
		OVERLAY_MODE_LIT = 0, // write the lit texture plus the overlay texel
		OVERLAY_MODE_BLEND_50 = 1, // average the overlay texel with the back buffer
		OVERLAY_MODE_ADDITIVE = 2, // add the overlay texel to the back buffer
	};

	// Render flag that selects OVERLAY_MODE_NONE.
	const uint16_t SOFTWARE_RENDER_NO_OVERLAY = 0x2000;
	// g_softwareTextureData slot of the shared overlay texture.
	const int32_t OVERLAY_TEXTURE_INDEX = 14;
	// Level 9 texel that selects OVERLAY_MODE_ADDITIVE (pure green in RGB565).
	const uint16_t OVERLAY_MARKER_TEXEL = 0x7C0;

	// rightInterpolants slots that hold the overlay UV at both ends of a span.
	enum OverlayScanlineSlot
	{
		OVERLAY_LEFT_U = 7,
		OVERLAY_LEFT_V = 8,
		OVERLAY_RIGHT_U = 9,
		OVERLAY_RIGHT_V = 10,
	};

	// UnkRenderAPI13 and UnkRenderAPI25 read the reserved bytes after SoftwareRenderItem::textureIndex as one overlay
	// texture UV byte pair per vertex. A rename of the shared field moves 0x0042E600, so this
	// layout stays file-local.
	struct SoftwareOverlayItem
	{
		SoftwareRasterVertex vertices[4];
		SoftwareRenderItem* next;
		uint16_t renderFlags;
		uint8_t textureIndex;
		uint8_t reserved77;
		uint8_t overlayUV[4][2];
	};
	STATIC_ASSERT(sizeof(SoftwareOverlayItem) == sizeof(SoftwareRenderItem));
	STATIC_ASSERT(offsetof(SoftwareOverlayItem, overlayUV) == 0x78);
}

#define RASTERIZE_SOLID_EDGE(pointA, pointB, doneLabel)                                                                      \
	do                                                                                                                       \
	{                                                                                                                        \
		int32_t edgeY;                                                                                                       \
		int32_t edgeEndY;                                                                                                    \
		int32_t edgeStartX;                                                                                                  \
		int32_t edgeEndX;                                                                                                    \
		int32_t edgeXStep;                                                                                                   \
		int32_t edgeXFixed;                                                                                                  \
		ScanlineScratch* edgeScanline;                                                                                       \
		if ((pointA)->y < (pointB)->y)                                                                                       \
		{                                                                                                                    \
			if ((pointA)->y > Toy2::g_screenClipBottom || (pointB)->y < Toy2::g_screenClipTop)                               \
				goto doneLabel;                                                                                              \
			edgeY = (pointA)->y;                                                                                             \
			edgeEndY = (pointB)->y;                                                                                          \
			edgeStartX = (pointA)->x;                                                                                        \
			edgeEndX = (pointB)->x;                                                                                          \
		}                                                                                                                    \
		else                                                                                                                 \
		{                                                                                                                    \
			if ((pointB)->y >= (pointA)->y || (pointB)->y > Toy2::g_screenClipBottom || (pointA)->y < Toy2::g_screenClipTop) \
				goto doneLabel;                                                                                              \
			edgeY = (pointB)->y;                                                                                             \
			edgeEndY = (pointA)->y;                                                                                          \
			edgeStartX = (pointB)->x;                                                                                        \
			edgeEndX = (pointA)->x;                                                                                          \
		}                                                                                                                    \
		edgeXStep = (edgeEndX - edgeStartX) * 0x400 / (edgeEndY - edgeY);                                                    \
		edgeXFixed = edgeStartX * 0x400 + 0x200;                                                                             \
		if (edgeY < Toy2::g_screenClipTop)                                                                                   \
		{                                                                                                                    \
			edgeXFixed += (Toy2::g_screenClipTop - edgeY) * edgeXStep;                                                       \
			edgeY = Toy2::g_screenClipTop;                                                                                   \
		}                                                                                                                    \
		edgeScanline = &g_scanlineScratch[edgeY];                                                                            \
		do                                                                                                                   \
		{                                                                                                                    \
			if (g_skipOddScanlines == 0 || (edgeY & 1) == 0)                                                                 \
			{                                                                                                                \
				if (edgeScanline->populated == 0)                                                                            \
				{                                                                                                            \
					edgeScanline->rightXFixed = edgeXFixed;                                                                  \
					edgeScanline->leftXFixed = edgeXFixed;                                                                   \
					edgeScanline->populated = 1;                                                                             \
				}                                                                                                            \
				else if (edgeXFixed < edgeScanline->leftXFixed)                                                              \
				{                                                                                                            \
					edgeScanline->leftXFixed = edgeXFixed;                                                                   \
				}                                                                                                            \
				else if (edgeXFixed > edgeScanline->rightXFixed)                                                             \
				{                                                                                                            \
					edgeScanline->rightXFixed = edgeXFixed;                                                                  \
				}                                                                                                            \
			}                                                                                                                \
			edgeScanline++;                                                                                                  \
			edgeXFixed += edgeXStep;                                                                                         \
			edgeY++;                                                                                                         \
		} while (edgeY <= edgeEndY && edgeY <= Toy2::g_screenClipBottom);                                                    \
	doneLabel:;                                                                                                              \
	} while (0)

// Walks one edge with the texture UV and the three colour interpolants. The end row is inclusive when endOperator is <=.
#define RASTERIZE_LIT_EDGE_WITH_END(vertexA, vertexB, doneLabel, endOperator)                                                    \
	do                                                                                                                           \
	{                                                                                                                            \
		int32_t edgeY;                                                                                                           \
		int32_t edgeEndY;                                                                                                        \
		int32_t edgeX;                                                                                                           \
		int32_t edgeHeight;                                                                                                      \
		int32_t edgeXStep;                                                                                                       \
		int32_t edgeU;                                                                                                           \
		int32_t edgeV;                                                                                                           \
		int32_t edgeBlue;                                                                                                        \
		int32_t edgeGreen;                                                                                                       \
		int32_t edgeRed;                                                                                                         \
		int32_t edgeUStep;                                                                                                       \
		int32_t edgeVStep;                                                                                                       \
		int32_t edgeBlueStep;                                                                                                    \
		int32_t edgeGreenStep;                                                                                                   \
		int32_t edgeRedStep;                                                                                                     \
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
			edgeBlue = (vertexA)->blue;                                                                                          \
			edgeGreen = (vertexA)->green;                                                                                        \
			edgeRed = (vertexA)->red;                                                                                            \
			edgeUStep = ((vertexB)->u - edgeU) / edgeHeight;                                                                     \
			edgeVStep = ((vertexB)->v - edgeV) / edgeHeight;                                                                     \
			edgeBlueStep = ((vertexB)->blue - edgeBlue) / edgeHeight;                                                            \
			edgeGreenStep = ((vertexB)->green - edgeGreen) / edgeHeight;                                                         \
			edgeRedStep = ((vertexB)->red - edgeRed) / edgeHeight;                                                               \
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
			edgeBlue = (vertexB)->blue;                                                                                          \
			edgeGreen = (vertexB)->green;                                                                                        \
			edgeRed = (vertexB)->red;                                                                                            \
			edgeUStep = ((vertexA)->u - edgeU) / edgeHeight;                                                                     \
			edgeVStep = ((vertexA)->v - edgeV) / edgeHeight;                                                                     \
			edgeBlueStep = ((vertexA)->blue - edgeBlue) / edgeHeight;                                                            \
			edgeGreenStep = ((vertexA)->green - edgeGreen) / edgeHeight;                                                         \
			edgeRedStep = ((vertexA)->red - edgeRed) / edgeHeight;                                                               \
		}                                                                                                                        \
		edgeXFixed = edgeX * 0x400 + 0x200;                                                                                      \
		if (edgeY < Toy2::g_screenClipTop)                                                                                       \
		{                                                                                                                        \
			clippedRows = Toy2::g_screenClipTop - edgeY;                                                                         \
			edgeXFixed += edgeXStep * clippedRows;                                                                               \
			edgeU += edgeUStep * clippedRows;                                                                                    \
			edgeV += edgeVStep * clippedRows;                                                                                    \
			edgeBlue += edgeBlueStep * clippedRows;                                                                              \
			edgeGreen += edgeGreenStep * clippedRows;                                                                            \
			edgeRed += edgeRedStep * clippedRows;                                                                                \
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
					edgeScanline->rightInterpolants[2] = edgeBlue;                                                               \
					edgeScanline->leftInterpolants[2] = edgeBlue;                                                                \
					edgeScanline->rightInterpolants[3] = edgeGreen;                                                              \
					edgeScanline->leftInterpolants[3] = edgeGreen;                                                               \
					edgeScanline->rightInterpolants[4] = edgeRed;                                                                \
					edgeScanline->leftInterpolants[4] = edgeRed;                                                                 \
					edgeScanline->populated = 1;                                                                                 \
				}                                                                                                                \
				else if (edgeXFixed < edgeScanline->leftXFixed)                                                                  \
				{                                                                                                                \
					edgeScanline->leftXFixed = edgeXFixed;                                                                       \
					edgeScanline->leftInterpolants[0] = edgeU;                                                                   \
					edgeScanline->leftInterpolants[1] = edgeV;                                                                   \
					edgeScanline->leftInterpolants[2] = edgeBlue;                                                                \
					edgeScanline->leftInterpolants[3] = edgeGreen;                                                               \
					edgeScanline->leftInterpolants[4] = edgeRed;                                                                 \
				}                                                                                                                \
				else if (edgeXFixed > edgeScanline->rightXFixed)                                                                 \
				{                                                                                                                \
					edgeScanline->rightXFixed = edgeXFixed;                                                                      \
					edgeScanline->rightInterpolants[0] = edgeU;                                                                  \
					edgeScanline->rightInterpolants[1] = edgeV;                                                                  \
					edgeScanline->rightInterpolants[2] = edgeBlue;                                                               \
					edgeScanline->rightInterpolants[3] = edgeGreen;                                                              \
					edgeScanline->rightInterpolants[4] = edgeRed;                                                                \
				}                                                                                                                \
			}                                                                                                                    \
			edgeScanline++;                                                                                                      \
			edgeXFixed += edgeXStep;                                                                                             \
			edgeU += edgeUStep;                                                                                                  \
			edgeV += edgeVStep;                                                                                                  \
			edgeBlue += edgeBlueStep;                                                                                            \
			edgeGreen += edgeGreenStep;                                                                                          \
			edgeRed += edgeRedStep;                                                                                              \
			edgeY++;                                                                                                             \
		} while (edgeY endOperator edgeEndY && edgeY <= Toy2::g_screenClipBottom);                                               \
	doneLabel:;                                                                                                                  \
	} while (0)
#define RASTERIZE_LIT_EDGE(vertexA, vertexB, doneLabel) RASTERIZE_LIT_EDGE_WITH_END(vertexA, vertexB, doneLabel, <)
#define RASTERIZE_LIT_EDGE_INCLUSIVE(vertexA, vertexB, doneLabel) RASTERIZE_LIT_EDGE_WITH_END(vertexA, vertexB, doneLabel, <=)

// Sets the inclusive row range that the polygon covers, clipped to the screen top and the
// screen bottom. The first three vertices and, for a quad, the fourth vertex extend the range.
#define CLIP_POLYGON_ROW_RANGE(item, topY, bottomY) \
	int32_t topY = (item)->vertices[0].y;           \
	int32_t bottomY = topY;                         \
	if ((item)->vertices[1].y < topY)               \
		topY = (item)->vertices[1].y;               \
	else if ((item)->vertices[1].y > bottomY)       \
		bottomY = (item)->vertices[1].y;            \
	if ((item)->vertices[2].y < topY)               \
		topY = (item)->vertices[2].y;               \
	else if ((item)->vertices[2].y > bottomY)       \
		bottomY = (item)->vertices[2].y;            \
	if ((item)->renderFlags & SOFTWARE_RENDER_QUAD) \
	{                                               \
		if ((item)->vertices[3].y < topY)           \
			topY = (item)->vertices[3].y;           \
		else if ((item)->vertices[3].y > bottomY)   \
			bottomY = (item)->vertices[3].y;        \
	}                                               \
	if (topY < Toy2::g_screenClipTop)               \
		topY = Toy2::g_screenClipTop;               \
	if (bottomY > Toy2::g_screenClipBottom)         \
	bottomY = Toy2::g_screenClipBottom

// Clips one span to the screen left and the screen right: the first pixel of the span with the
// count of the pixels that the row holds. It reads width, leftX, rightX and rowStart of the span
// loop. declareSpanPixel declares the pointer, named pixel, that the span writes, because the
// back buffer holds 16 bit pixels in the true colour modes and 8 bit pixels in the palette modes.
// advanceClippedSpan steps the interpolants over the pixels that the left clip drops, because
// each span mode holds its own set of them and steps them in the order that retail compiled.
// A span that the left clip cuts drops the last pixel of the row, so its count stays one below
// the width of the window.
#define CLIP_SPAN_TO_SCREEN(declareSpanPixel, advanceClippedSpan) \
	int32_t pixelCount = width;                                   \
	declareSpanPixel;                                             \
	if (leftX < Toy2::g_screenClipLeft)                           \
	{                                                             \
		int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;   \
		advanceClippedSpan;                                       \
		if (rightX == Toy2::g_screenClipRight)                    \
			pixelCount = Toy2::g_softWindowWidth - 1;             \
		else                                                      \
		{                                                         \
			pixelCount = Toy2::g_softWindowWidth;                 \
			if (rightX <= Toy2::g_screenClipRight)                \
				pixelCount = rightX - Toy2::g_screenClipLeft;     \
		}                                                         \
	}                                                             \
	else                                                          \
	{                                                             \
		pixel = rowStart + leftX - Toy2::g_screenClipLeft;        \
		if (rightX == Toy2::g_screenClipRight)                    \
			pixelCount = Toy2::g_screenClipRight - leftX;         \
		else if (rightX > Toy2::g_screenClipRight)                \
			pixelCount = Toy2::g_screenClipRight - leftX + 1;     \
	}

// Declares the texture UV and the three colour interpolants at the left end of one lit span with
// their steps across the span. It reads spanRow, leftX and rightX of the span loop, and declares
// width, u, v, red, green, blue and their steps for the loop that writes the pixels.
#define LIT_SPAN_INTERPOLANTS()                                          \
	int32_t width = rightX - leftX;                                      \
	int32_t u = spanRow->leftInterpolants[0];                            \
	int32_t v = spanRow->leftInterpolants[1];                            \
	int32_t red = spanRow->leftInterpolants[2];                          \
	int32_t green = spanRow->leftInterpolants[3];                        \
	int32_t blue = spanRow->leftInterpolants[4];                         \
	int32_t uStep = (spanRow->rightInterpolants[0] - u) / width;         \
	int32_t vStep = (spanRow->rightInterpolants[1] - v) / width;         \
	int32_t redStep = (spanRow->rightInterpolants[2] - red) / width;     \
	int32_t greenStep = (spanRow->rightInterpolants[3] - green) / width; \
	int32_t blueStep = (spanRow->rightInterpolants[4] - blue) / width

// Sets up one lit, textured span of the back buffer: the texture UV and the three
// colour interpolants at the left end of the span, their steps across the span, and the first
// pixel with the pixel count that the left clip and the right clip leave. It reads spanRow,
// rowStart, leftX and rightX of the span loop, and it declares width, u, v, red, green, blue,
// their steps, pixel and pixelCount for the loop that writes the pixels. rightClipTest with
// testTrueCount and testFalseCount hold the order in which each span mode tests the right
// clip, which is the order that retail compiled.
#define LIT_TEXTURED_SPAN_SETUP(rightClipTest, testTrueCount, testFalseCount) \
	LIT_SPAN_INTERPOLANTS();                                                  \
	int32_t pixelCount;                                                       \
	uint16_t* pixel;                                                          \
	if (leftX < Toy2::g_screenClipLeft)                                       \
	{                                                                         \
		int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;               \
		u += clippedPixels * uStep;                                           \
		v += clippedPixels * vStep;                                           \
		red += clippedPixels * redStep;                                       \
		green += clippedPixels * greenStep;                                   \
		blue += clippedPixels * blueStep;                                     \
		pixel = rowStart;                                                     \
		pixelCount = Toy2::g_softWindowWidth;                                 \
		if (rightX <= Toy2::g_screenClipRight)                                \
			pixelCount = rightX - Toy2::g_screenClipLeft + 1;                 \
	}                                                                         \
	else                                                                      \
	{                                                                         \
		pixel = rowStart + leftX - Toy2::g_screenClipLeft;                    \
		if (rightX rightClipTest Toy2::g_screenClipRight)                     \
			pixelCount = testTrueCount;                                       \
		else                                                                  \
			pixelCount = testFalseCount;                                      \
	}

// Steps the texture UV, the three colour interpolants and the destination of a lit, textured
// span on by one pixel. It reads the names that LIT_TEXTURED_SPAN_SETUP declares.
#define ADVANCE_LIT_TEXTURED_SPAN() \
	pixel++;                        \
	u += uStep;                     \
	v += vStep;                     \
	red += redStep;                 \
	green += greenStep;             \
	blue += blueStep;               \
	pixelCount--

// Draws every row of a lit, textured polygon that the scanline table holds, and holds the back
// buffer where the texel is the colour key. litTexel names the macro that lights one texel for
// the back buffer format and colourKeyTexel names the texel value that the format keeps.
#define DRAW_LIT_TEXTURED_COLOUR_KEY_ROWS(litTexel, colourKeyTexel)                                                                                \
	do                                                                                                                                             \
	{                                                                                                                                              \
		if (spanRow->populated != 0 && spanRow->leftXFixed <= Toy2::g_screenClipRightFixed && spanRow->rightXFixed >= Toy2::g_screenClipLeftFixed) \
		{                                                                                                                                          \
			int32_t leftX = spanRow->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;                                                                     \
			int32_t rightX = spanRow->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;                                                                   \
			if (leftX == rightX)                                                                                                                   \
			{                                                                                                                                      \
				uint16_t texel = texture[(spanRow->leftInterpolants[0] >> k_fixedPointShift) + (spanRow->leftInterpolants[1] >> 8 & 0xFFFFFF00)];  \
				if (texel != colourKeyTexel)                                                                                                       \
				{                                                                                                                                  \
					rowStart[leftX - Toy2::g_screenClipLeft] =                                                                                     \
						litTexel(texel, spanRow->leftInterpolants[2], spanRow->leftInterpolants[3], spanRow->leftInterpolants[4]);                 \
				}                                                                                                                                  \
			}                                                                                                                                      \
			else                                                                                                                                   \
			{                                                                                                                                      \
				LIT_TEXTURED_SPAN_SETUP(>, Toy2::g_screenClipRight - leftX + 1, width + 1);                                                        \
				do                                                                                                                                 \
				{                                                                                                                                  \
					uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];                                                    \
					if (texel != colourKeyTexel)                                                                                                   \
					{                                                                                                                              \
						*pixel = litTexel(texel, red, green, blue);                                                                                \
					}                                                                                                                              \
					ADVANCE_LIT_TEXTURED_SPAN();                                                                                                   \
				} while (pixelCount > 0);                                                                                                          \
			}                                                                                                                                      \
		}                                                                                                                                          \
		spanRow++;                                                                                                                                 \
		rowsLeft--;                                                                                                                                \
		rowStart += g_backBufferPitchPixels;                                                                                                       \
	} while (rowsLeft != 0)

// Draws every row of a lit, textured polygon that the scanline table holds, and writes every
// texel of each span. litTexel names the macro that lights one texel for the back buffer format.
#define DRAW_LIT_TEXTURED_ROWS(litTexel)                                                                                                           \
	do                                                                                                                                             \
	{                                                                                                                                              \
		if (spanRow->populated != 0 && spanRow->leftXFixed <= Toy2::g_screenClipRightFixed && spanRow->rightXFixed >= Toy2::g_screenClipLeftFixed) \
		{                                                                                                                                          \
			int32_t leftX = spanRow->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;                                                                     \
			int32_t rightX = spanRow->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;                                                                   \
			if (leftX == rightX)                                                                                                                   \
			{                                                                                                                                      \
				uint16_t texel = texture[(spanRow->leftInterpolants[0] >> k_fixedPointShift) + (spanRow->leftInterpolants[1] >> 8 & 0xFFFFFF00)];  \
				rowStart[leftX - Toy2::g_screenClipLeft] =                                                                                         \
					litTexel(texel, spanRow->leftInterpolants[2], spanRow->leftInterpolants[3], spanRow->leftInterpolants[4]);                     \
			}                                                                                                                                      \
			else                                                                                                                                   \
			{                                                                                                                                      \
				LIT_TEXTURED_SPAN_SETUP(<=, width + 1, Toy2::g_screenClipRight - leftX + 1);                                                       \
				do                                                                                                                                 \
				{                                                                                                                                  \
					uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];                                                    \
					*pixel = litTexel(texel, red, green, blue);                                                                                    \
					ADVANCE_LIT_TEXTURED_SPAN();                                                                                                   \
				} while (pixelCount > 0);                                                                                                          \
			}                                                                                                                                      \
		}                                                                                                                                          \
		spanRow++;                                                                                                                                 \
		rowStart += g_backBufferPitchPixels;                                                                                                       \
		rowsLeft--;                                                                                                                                \
	} while (rowsLeft != 0)

// Walks one edge with the lit texture interpolants plus the overlay UV; the end row is inclusive.
#define RASTERIZE_LIT_OVERLAY_EDGE(vertexA, vertexB, overlayA, overlayB, doneLabel)                                              \
	do                                                                                                                           \
	{                                                                                                                            \
		int32_t edgeY;                                                                                                           \
		int32_t edgeEndY;                                                                                                        \
		int32_t edgeX;                                                                                                           \
		int32_t edgeHeight;                                                                                                      \
		int32_t edgeXStep;                                                                                                       \
		int32_t edgeU;                                                                                                           \
		int32_t edgeV;                                                                                                           \
		int32_t edgeBlue;                                                                                                        \
		int32_t edgeGreen;                                                                                                       \
		int32_t edgeRed;                                                                                                         \
		int32_t edgeOverlayU;                                                                                                    \
		int32_t edgeOverlayV;                                                                                                    \
		int32_t edgeUStep;                                                                                                       \
		int32_t edgeVStep;                                                                                                       \
		int32_t edgeBlueStep;                                                                                                    \
		int32_t edgeGreenStep;                                                                                                   \
		int32_t edgeRedStep;                                                                                                     \
		int32_t edgeOverlayUStep;                                                                                                \
		int32_t edgeOverlayVStep;                                                                                                \
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
			edgeBlue = (vertexA)->blue;                                                                                          \
			edgeGreen = (vertexA)->green;                                                                                        \
			edgeRed = (vertexA)->red;                                                                                            \
			edgeUStep = ((vertexB)->u - edgeU) / edgeHeight;                                                                     \
			edgeVStep = ((vertexB)->v - edgeV) / edgeHeight;                                                                     \
			edgeBlueStep = ((vertexB)->blue - edgeBlue) / edgeHeight;                                                            \
			edgeGreenStep = ((vertexB)->green - edgeGreen) / edgeHeight;                                                         \
			edgeRedStep = ((vertexB)->red - edgeRed) / edgeHeight;                                                               \
			edgeOverlayU = (overlayA)[0];                                                                                        \
			edgeOverlayUStep = ((overlayB)[0] - edgeOverlayU) * 0x10000 / edgeHeight;                                            \
			edgeOverlayV = (overlayA)[1];                                                                                        \
			edgeOverlayVStep = ((overlayB)[1] - edgeOverlayV) * 0x10000 / edgeHeight;                                            \
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
			edgeBlue = (vertexB)->blue;                                                                                          \
			edgeGreen = (vertexB)->green;                                                                                        \
			edgeRed = (vertexB)->red;                                                                                            \
			edgeUStep = ((vertexA)->u - edgeU) / edgeHeight;                                                                     \
			edgeVStep = ((vertexA)->v - edgeV) / edgeHeight;                                                                     \
			edgeBlueStep = ((vertexA)->blue - edgeBlue) / edgeHeight;                                                            \
			edgeGreenStep = ((vertexA)->green - edgeGreen) / edgeHeight;                                                         \
			edgeRedStep = ((vertexA)->red - edgeRed) / edgeHeight;                                                               \
			edgeOverlayU = (overlayB)[0];                                                                                        \
			edgeOverlayUStep = ((overlayA)[0] - edgeOverlayU) * 0x10000 / edgeHeight;                                            \
			edgeOverlayV = (overlayB)[1];                                                                                        \
			edgeOverlayVStep = ((overlayA)[1] - edgeOverlayV) * 0x10000 / edgeHeight;                                            \
		}                                                                                                                        \
		edgeOverlayU = edgeOverlayU * 0x10000 + 0x8000;                                                                          \
		edgeOverlayV = edgeOverlayV * 0x10000 + 0x8000;                                                                          \
		edgeXFixed = edgeX * 0x400 + 0x200;                                                                                      \
		if (edgeY < Toy2::g_screenClipTop)                                                                                       \
		{                                                                                                                        \
			clippedRows = Toy2::g_screenClipTop - edgeY;                                                                         \
			edgeXFixed += edgeXStep * clippedRows;                                                                               \
			edgeU += edgeUStep * clippedRows;                                                                                    \
			edgeV += edgeVStep * clippedRows;                                                                                    \
			edgeBlue += edgeBlueStep * clippedRows;                                                                              \
			edgeGreen += edgeGreenStep * clippedRows;                                                                            \
			edgeRed += edgeRedStep * clippedRows;                                                                                \
			edgeOverlayU += edgeOverlayUStep * clippedRows;                                                                      \
			edgeOverlayV += edgeOverlayVStep * clippedRows;                                                                      \
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
					edgeScanline->rightInterpolants[2] = edgeBlue;                                                               \
					edgeScanline->leftInterpolants[2] = edgeBlue;                                                                \
					edgeScanline->rightInterpolants[3] = edgeGreen;                                                              \
					edgeScanline->leftInterpolants[3] = edgeGreen;                                                               \
					edgeScanline->rightInterpolants[4] = edgeRed;                                                                \
					edgeScanline->leftInterpolants[4] = edgeRed;                                                                 \
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_U] = edgeOverlayU;                                             \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_U] = edgeOverlayU;                                              \
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_V] = edgeOverlayV;                                             \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_V] = edgeOverlayV;                                              \
					edgeScanline->populated = 1;                                                                                 \
				}                                                                                                                \
				else if (edgeXFixed < edgeScanline->leftXFixed)                                                                  \
				{                                                                                                                \
					edgeScanline->leftXFixed = edgeXFixed;                                                                       \
					edgeScanline->leftInterpolants[0] = edgeU;                                                                   \
					edgeScanline->leftInterpolants[1] = edgeV;                                                                   \
					edgeScanline->leftInterpolants[2] = edgeBlue;                                                                \
					edgeScanline->leftInterpolants[3] = edgeGreen;                                                               \
					edgeScanline->leftInterpolants[4] = edgeRed;                                                                 \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_U] = edgeOverlayU;                                              \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_V] = edgeOverlayV;                                              \
				}                                                                                                                \
				else if (edgeXFixed > edgeScanline->rightXFixed)                                                                 \
				{                                                                                                                \
					edgeScanline->rightXFixed = edgeXFixed;                                                                      \
					edgeScanline->rightInterpolants[0] = edgeU;                                                                  \
					edgeScanline->rightInterpolants[1] = edgeV;                                                                  \
					edgeScanline->rightInterpolants[2] = edgeBlue;                                                               \
					edgeScanline->rightInterpolants[3] = edgeGreen;                                                              \
					edgeScanline->rightInterpolants[4] = edgeRed;                                                                \
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_U] = edgeOverlayU;                                             \
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_V] = edgeOverlayV;                                             \
				}                                                                                                                \
			}                                                                                                                    \
			edgeScanline++;                                                                                                      \
			edgeXFixed += edgeXStep;                                                                                             \
			edgeU += edgeUStep;                                                                                                  \
			edgeV += edgeVStep;                                                                                                  \
			edgeBlue += edgeBlueStep;                                                                                            \
			edgeGreen += edgeGreenStep;                                                                                          \
			edgeRed += edgeRedStep;                                                                                              \
			edgeOverlayU += edgeOverlayUStep;                                                                                    \
			edgeOverlayV += edgeOverlayVStep;                                                                                    \
			edgeY++;                                                                                                             \
		} while (edgeY <= edgeEndY && edgeY <= Toy2::g_screenClipBottom);                                                        \
	doneLabel:;                                                                                                                  \
	} while (0)

// Walks one edge with only the overlay UV.
#define RASTERIZE_OVERLAY_EDGE(vertexA, vertexB, overlayA, overlayB, doneLabel)                                                  \
	do                                                                                                                           \
	{                                                                                                                            \
		int32_t edgeY;                                                                                                           \
		int32_t edgeEndY;                                                                                                        \
		int32_t edgeX;                                                                                                           \
		int32_t edgeHeight;                                                                                                      \
		int32_t edgeXStep;                                                                                                       \
		int32_t edgeOverlayU;                                                                                                    \
		int32_t edgeOverlayV;                                                                                                    \
		int32_t edgeOverlayUStep;                                                                                                \
		int32_t edgeOverlayVStep;                                                                                                \
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
			edgeOverlayU = (overlayA)[0];                                                                                        \
			edgeOverlayUStep = ((overlayB)[0] - edgeOverlayU) * 0x10000 / edgeHeight;                                            \
			edgeOverlayV = (overlayA)[1];                                                                                        \
			edgeOverlayVStep = ((overlayB)[1] - edgeOverlayV) * 0x10000 / edgeHeight;                                            \
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
			edgeOverlayU = (overlayB)[0];                                                                                        \
			edgeOverlayUStep = ((overlayA)[0] - edgeOverlayU) * 0x10000 / edgeHeight;                                            \
			edgeOverlayV = (overlayB)[1];                                                                                        \
			edgeOverlayVStep = ((overlayA)[1] - edgeOverlayV) * 0x10000 / edgeHeight;                                            \
		}                                                                                                                        \
		edgeOverlayU = edgeOverlayU * 0x10000 + 0x8000;                                                                          \
		edgeXFixed = edgeX * 0x400 + 0x200;                                                                                      \
		edgeOverlayV = edgeOverlayV * 0x10000 + 0x8000;                                                                          \
		if (edgeY < Toy2::g_screenClipTop)                                                                                       \
		{                                                                                                                        \
			clippedRows = Toy2::g_screenClipTop - edgeY;                                                                         \
			edgeXFixed += edgeXStep * clippedRows;                                                                               \
			edgeOverlayU += edgeOverlayUStep * clippedRows;                                                                      \
			edgeOverlayV += edgeOverlayVStep * clippedRows;                                                                      \
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
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_U] = edgeOverlayU;                                             \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_U] = edgeOverlayU;                                              \
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_V] = edgeOverlayV;                                             \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_V] = edgeOverlayV;                                              \
					edgeScanline->populated = 1;                                                                                 \
				}                                                                                                                \
				else if (edgeXFixed < edgeScanline->leftXFixed)                                                                  \
				{                                                                                                                \
					edgeScanline->leftXFixed = edgeXFixed;                                                                       \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_U] = edgeOverlayU;                                              \
					edgeScanline->rightInterpolants[OVERLAY_LEFT_V] = edgeOverlayV;                                              \
				}                                                                                                                \
				else if (edgeXFixed > edgeScanline->rightXFixed)                                                                 \
				{                                                                                                                \
					edgeScanline->rightXFixed = edgeXFixed;                                                                      \
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_U] = edgeOverlayU;                                             \
					edgeScanline->rightInterpolants[OVERLAY_RIGHT_V] = edgeOverlayV;                                             \
				}                                                                                                                \
			}                                                                                                                    \
			edgeScanline++;                                                                                                      \
			edgeXFixed += edgeXStep;                                                                                             \
			edgeOverlayU += edgeOverlayUStep;                                                                                    \
			edgeOverlayV += edgeOverlayVStep;                                                                                    \
			edgeY++;                                                                                                             \
		} while (edgeY < edgeEndY && edgeY <= Toy2::g_screenClipBottom);                                                         \
	doneLabel:;                                                                                                                  \
	} while (0)

// Sets up one overlay blend span: the step per pixel, the first pixel and the pixel
// count after the left and the right clip.
#define CLIP_OVERLAY_BLEND_SPAN(pixelType)                                                    \
	int32_t overlayU = scanline->rightInterpolants[OVERLAY_LEFT_U];                           \
	int32_t overlayV = scanline->rightInterpolants[OVERLAY_LEFT_V];                           \
	int32_t width = rightX - leftX;                                                           \
	int32_t overlayUStep = (scanline->rightInterpolants[OVERLAY_RIGHT_U] - overlayU) / width; \
	int32_t overlayVStep = (scanline->rightInterpolants[OVERLAY_RIGHT_V] - overlayV) / width; \
	int32_t pixelCount = width;                                                               \
	pixelType* pixel = rowStart;                                                              \
	if (leftX < Toy2::g_screenClipLeft)                                                       \
	{                                                                                         \
		overlayU += overlayUStep * (Toy2::g_screenClipLeft - leftX);                          \
		overlayV += overlayVStep * (Toy2::g_screenClipLeft - leftX);                          \
		if (rightX > Toy2::g_screenClipRight)                                                 \
			pixelCount = Toy2::g_softWindowWidth - 1;                                         \
		else                                                                                  \
			pixelCount = rightX - Toy2::g_screenClipLeft;                                     \
	}                                                                                         \
	else                                                                                      \
	{                                                                                         \
		pixel = rowStart + leftX - Toy2::g_screenClipLeft;                                    \
		if (rightX == Toy2::g_screenClipRight)                                                \
			pixelCount = Toy2::g_screenClipRight - leftX;                                     \
		else if (rightX > Toy2::g_screenClipRight)                                            \
			pixelCount = Toy2::g_screenClipRight - leftX + 1;                                 \
	}

#define RASTERIZE_TEXTURED_EDGE_WITH_END(vertexA, vertexB, doneLabel, endOperator)                                               \
	do                                                                                                                           \
	{                                                                                                                            \
		int32_t edgeY;                                                                                                           \
		int32_t edgeEndY;                                                                                                        \
		const SoftwareRasterVertex* edgeStart;                                                                                   \
		const SoftwareRasterVertex* edgeEnd;                                                                                     \
		int32_t edgeHeight;                                                                                                      \
		int32_t edgeXStep;                                                                                                       \
		int32_t edgeUStep;                                                                                                       \
		int32_t edgeVStep;                                                                                                       \
		int32_t edgeXFixed;                                                                                                      \
		int32_t edgeU;                                                                                                           \
		int32_t edgeV;                                                                                                           \
		int32_t clippedRows;                                                                                                     \
		ScanlineScratch* edgeScanline;                                                                                           \
		if ((vertexA)->y < (vertexB)->y)                                                                                         \
		{                                                                                                                        \
			if ((vertexA)->y > Toy2::g_screenClipBottom || (vertexB)->y < Toy2::g_screenClipTop)                                 \
				goto doneLabel;                                                                                                  \
			edgeY = (vertexA)->y;                                                                                                \
			edgeEndY = (vertexB)->y;                                                                                             \
			edgeStart = (vertexA);                                                                                               \
			edgeEnd = (vertexB);                                                                                                 \
		}                                                                                                                        \
		else                                                                                                                     \
		{                                                                                                                        \
			if ((vertexB)->y >= (vertexA)->y || (vertexB)->y > Toy2::g_screenClipBottom || (vertexA)->y < Toy2::g_screenClipTop) \
				goto doneLabel;                                                                                                  \
			edgeY = (vertexB)->y;                                                                                                \
			edgeEndY = (vertexA)->y;                                                                                             \
			edgeStart = (vertexB);                                                                                               \
			edgeEnd = (vertexA);                                                                                                 \
		}                                                                                                                        \
		edgeHeight = edgeEndY - edgeY;                                                                                           \
		edgeXStep = (edgeEnd->x - edgeStart->x) * 0x400 / edgeHeight;                                                            \
		edgeUStep = (edgeEnd->u - edgeStart->u) / edgeHeight;                                                                    \
		edgeVStep = (edgeEnd->v - edgeStart->v) / edgeHeight;                                                                    \
		edgeXFixed = edgeStart->x * 0x400 + 0x200;                                                                               \
		edgeU = edgeStart->u;                                                                                                    \
		edgeV = edgeStart->v;                                                                                                    \
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
		} while (edgeY endOperator edgeEndY && edgeY <= Toy2::g_screenClipBottom);                                               \
	doneLabel:;                                                                                                                  \
	} while (0)

#define RASTERIZE_TEXTURED_EDGE(vertexA, vertexB, doneLabel) RASTERIZE_TEXTURED_EDGE_WITH_END(vertexA, vertexB, doneLabel, <=)
#define RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(vertexA, vertexB, doneLabel) RASTERIZE_TEXTURED_EDGE_WITH_END(vertexA, vertexB, doneLabel, <)

#endif

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

	// The software renderer's DirectDraw palette and its backing entry buffers.
	// SetPaletteOnAPI creates the palette from g_paletteEntries (entry 0 onward)
	// and attaches it to the front/back buffers. UpdatePaletteTint rebuilds the live
	// entries (1..255) by tinting the same range in the source palette by the
	// camera tint colour, preserving entry 0, then SetEntries on the palette.
	// The palette is stored B,G,R,X per entry (byte 0 = blue, 1 = green,
	// 2 = red); entry 0 is skipped by the tint loop and the SetEntries call.
	// GLOBAL: TOY2 0x00704E44
	LPDIRECTDRAWPALETTE g_lpPalette;

	// GLOBAL: TOY2 0x00704630
	uint8_t g_paletteEntries[0x400];

	// Maps each 5-bit BGR colour to the nearest entry in g_paletteSource.
	// GLOBAL: TOY2 0x0070462C
	uint8_t* g_rgbToPaletteIndex;

	// GLOBAL: TOY2 0x00704A38
	uint8_t g_paletteSource[0x400];

	// Maps a palette entry and a 0..127 light level to the nearest lit palette
	// entry. Palette entry zero is reserved and is not selected.
	// GLOBAL: TOY2 0x00534564
	uint8_t g_paletteLightingTable[128][256];

	// GLOBAL: TOY2 0x00704A34
	uint8_t* g_additivePaletteTable;

	// For each palette entry, stores the 8x8x8 combinations of channel offsets
	// from -96 through +128 in steps of 32.
	// GLOBAL: TOY2 0x00704A30
	uint8_t* g_paletteColourOffsetTable;

	// GLOBAL: TOY2 0x00704E48
	uint8_t* g_subtractivePaletteTable;

	// GLOBAL: TOY2 0x00704E38
	uint8_t* g_paletteBlend25Table;

	// GLOBAL: TOY2 0x00704E3C
	uint8_t* g_paletteBlend50Table;

	// GLOBAL: TOY2 0x00704E40
	uint8_t* g_paletteBlend75Table;

	// GLOBAL: TOY2 0x008393E0
	ScanlineScratch g_scanlineScratch[1024];

	void ClearScanlineFlags(ScanlineScratch* scanline, int32_t count);

	// GLOBAL: TOY2 0x00A4CC74
	int32_t g_levelFileIndex;

	// GLOBAL: TOY2 0x009F5FF4
	Nu3D::Viewport::ViewportRect* g_viewportRect;

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

	// .rdata depth-sort scale: 1023.0 (= 1024 - 1). SubmitSortedTriangle maps
	// the triangle's minimum vertex z into the 1024-entry bucket range.
	// GLOBAL: TOY2 0x004DDAC4
	extern const float k_depthSortScale = 1023.0f;

	// GLOBAL: TOY2 0x009F6010
	int32_t g_disableSortedPrimitiveSubmission;

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

// Combines one texel with the interpolated light of a span into an RGB555 pixel. Each ramp
// table holds the lit value of every level of one channel, so one table read lights a channel.
#define LIT_TEXEL_555(texel, red, green, blue)                                                                                                                 \
	(g_greenRampFull[(((texel) >> 5) & k_fiveBitChannelMask) + ((green) >> k_fixedPointShift)] + g_redRampFull[((texel) >> 10) + ((red) >> k_fixedPointShift)] \
		+ g_blueRampFull[((texel) & k_fiveBitChannelMask) + ((blue) >> k_fixedPointShift)])

	// Texel value that the colour key makes transparent in RGB555 (pure green).
	const uint16_t COLOUR_KEY_TEXEL_555 = 0x3E0;

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
	// UnkRenderAPI13 (RGB555) and UnkRenderAPI25 (RGB565) combine a polygon with the shared overlay
	// texture in one of these modes.
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
		int32_t topY = item->vertices[0].y;
		int32_t bottomY = topY;
		// Vertex 1 extends the row range.
		if (item->vertices[1].y < topY)
			topY = item->vertices[1].y;
		// Vertex 1 extends the bottom.
		else if (item->vertices[1].y > bottomY)
			bottomY = item->vertices[1].y;

		// Vertex 2 extends the row range.
		if (item->vertices[2].y < topY)
			topY = item->vertices[2].y;
		// Vertex 2 extends the bottom.
		else if (item->vertices[2].y > bottomY)
			bottomY = item->vertices[2].y;

		// Quad: vertex 3 extends the row range.
		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			if (item->vertices[3].y < topY)
				topY = item->vertices[3].y;
			else if (item->vertices[3].y > bottomY)
				bottomY = item->vertices[3].y;
		}

		// Clip the range to the screen top.
		if (topY < Toy2::g_screenClipTop)
			topY = Toy2::g_screenClipTop;
		// Clip the range to the screen bottom.
		if (bottomY > Toy2::g_screenClipBottom)
			bottomY = Toy2::g_screenClipBottom;

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

#undef ADD_SATURATED_555

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
	// STUB: TOY2 0x00469900
	void UnkRenderAPI18(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x0046AC60
	void UnkRenderAPI19(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x0046BF90
	void UnkRenderAPI20(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00465180
	void UnkRenderAPI24(SoftwareRenderItem* item) {}
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
		int32_t topY = item->vertices[0].y;
		int32_t bottomY = topY;
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

#undef ADD_SATURATED_565

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

#undef RASTERIZE_SOLID_EDGE

#define RASTERIZE_BLEND25_555_EDGE(vertexA, vertexB, doneLabel)                                                                  \
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

	// FUNCTION: TOY2 0x00459AB0 [PROVISIONAL]
	void RasterizeBlend25TexturedPolygon555(SoftwareRenderItem* item)
	{
		int32_t topY = item->vertices[0].y;
		int32_t bottomY = topY;
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

		RASTERIZE_BLEND25_555_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneBlend25_555);
		RASTERIZE_BLEND25_555_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneBlend25_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_BLEND25_555_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneBlend25_555);
		}
		else
		{
			RASTERIZE_BLEND25_555_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneBlend25_555);
			RASTERIZE_BLEND25_555_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneBlend25_555);
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

#undef RASTERIZE_BLEND25_555_EDGE

#define RASTERIZE_BLEND50_555_EDGE(vertexA, vertexB, doneLabel)                                                                  \
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

	// FUNCTION: TOY2 0x0045A5C0 [PROVISIONAL]
	void RasterizeBlend50TexturedPolygon555(SoftwareRenderItem* item)
	{
		int32_t topY = item->vertices[0].y;
		int32_t bottomY = topY;
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

		RASTERIZE_BLEND50_555_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneBlend50_555);
		RASTERIZE_BLEND50_555_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneBlend50_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_BLEND50_555_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneBlend50_555);
		}
		else
		{
			RASTERIZE_BLEND50_555_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneBlend50_555);
			RASTERIZE_BLEND50_555_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneBlend50_555);
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

#undef RASTERIZE_BLEND50_555_EDGE

#define RASTERIZE_BLEND75_555_EDGE(vertexA, vertexB, doneLabel)                                                                  \
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

	// FUNCTION: TOY2 0x0045B0B0 [PROVISIONAL]
	void RasterizeBlend75TexturedPolygon555(SoftwareRenderItem* item)
	{
		int32_t topY = item->vertices[0].y;
		int32_t bottomY = topY;
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

		RASTERIZE_BLEND75_555_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneBlend75_555);
		RASTERIZE_BLEND75_555_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneBlend75_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_BLEND75_555_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneBlend75_555);
		}
		else
		{
			RASTERIZE_BLEND75_555_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneBlend75_555);
			RASTERIZE_BLEND75_555_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneBlend75_555);
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

#undef RASTERIZE_BLEND75_555_EDGE

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

	// FUNCTION: TOY2 0x0045BBC0 [PROVISIONAL]
	void RasterizeTexturedPolygon555(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneTextured555);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneTextured555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneTextured555);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneTextured555);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneTextured555);
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
					if (leftX == rightX)
					{
						uint16_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
						if (texel != 0x3E0)
							rowStart[leftX - Toy2::g_screenClipLeft] = texel;
					}
					else
					{
						int32_t width = rightX - leftX;
						int32_t u = scanline->leftInterpolants[0];
						int32_t v = scanline->leftInterpolants[1];
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						int32_t pixelCount = width;
						uint16_t* pixel;
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
							if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_screenClipRight - leftX;
							pixelCount++;
							pixel = rowStart + leftX - Toy2::g_screenClipLeft;
						}

						do
						{
							uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x3E0)
								*pixel = texel;
							u += uStep;
							v += vStep;
							pixelCount--;
							pixel++;
						} while (pixelCount > 0);
					}
				}
				scanline++;
				rowStart += g_backBufferPitchPixels;
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
					uint16_t* pixel;
					if (leftX < Toy2::g_screenClipLeft)
					{
						int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
						v += clippedPixels * vStep;
						u += clippedPixels * uStep;
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
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x0045D110 [PROVISIONAL]
	void RasterizeAdditiveTexturedPolygon555(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneAdditive555);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneAdditive555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneAdditive555);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneAdditive555);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneAdditive555);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0x3E0)
					{
						uint16_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						int32_t red = (pixel & 0x7C00) + (texel & 0x7C00);
						if (red > 0x7C00)
							red = 0x7C00;
						int32_t green = (pixel & 0x3E0) + (texel & 0x3E0);
						if (green > 0x3E0)
							green = 0x3E0;
						int32_t blue = (pixel & 0x1F) + (texel & 0x1F);
						if (blue > 0x1F)
							blue = 0x1F;
						pixel = (uint16_t)(red + green + blue);
					}
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
					uint16_t* pixel;
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
						if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_screenClipRight - leftX;
						pixelCount++;
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					}

					do
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0x3E0)
						{
							int32_t red = (*pixel & 0x7C00) + (texel & 0x7C00);
							if (red > 0x7C00)
								red = 0x7C00;
							int32_t green = (*pixel & 0x3E0) + (texel & 0x3E0);
							if (green > 0x3E0)
								green = 0x3E0;
							int32_t blue = (*pixel & 0x1F) + (texel & 0x1F);
							if (blue > 0x1F)
								blue = 0x1F;
							*pixel = (uint16_t)(red + green + blue);
						}
						u += uStep;
						v += vStep;
						pixel++;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x0045C6B0 [PROVISIONAL]
	void RasterizeSubtractiveTexturedPolygon555(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneSubtractive555);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneSubtractive555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneSubtractive555);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneSubtractive555);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneSubtractive555);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0x3E0)
					{
						uint16_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						int32_t red = (pixel & 0x7C00) - (texel & 0x7C00);
						if (red < 0x400)
							red = 0;
						int32_t green = (pixel & 0x3E0) - (texel & 0x3E0);
						if (green < 0x20)
							green = 0;
						int32_t blue = (pixel & 0x1F) - (texel & 0x1F);
						if (blue < 1)
							blue = 0;
						pixel = (uint16_t)(red + green + blue);
					}
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
					uint16_t* pixel;
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
						if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_screenClipRight - leftX;
						pixelCount++;
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					}

					do
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0x3E0)
						{
							int32_t red = (*pixel & 0x7C00) - (texel & 0x7C00);
							if (red < 0x400)
								red = 0;
							int32_t green = (*pixel & 0x3E0) - (texel & 0x3E0);
							if (green < 0x20)
								green = 0;
							int32_t blue = (*pixel & 0x1F) - (texel & 0x1F);
							if (blue < 1)
								blue = 0;
							*pixel = (uint16_t)(red + green + blue);
						}
						u += uStep;
						v += vStep;
						pixel++;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x00463090 [PROVISIONAL]
	void RasterizeTexturedPolygon565(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneTextured565);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneTextured565);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneTextured565);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneTextured565);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneTextured565);
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
					if (leftX == rightX)
					{
						uint16_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
						if (texel != 0x7C0)
							rowStart[leftX - Toy2::g_screenClipLeft] = texel;
					}
					else
					{
						int32_t width = rightX - leftX;
						int32_t u = scanline->leftInterpolants[0];
						int32_t v = scanline->leftInterpolants[1];
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						int32_t pixelCount = width;
						uint16_t* pixel;
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
							if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_screenClipRight - leftX;
							pixelCount++;
							pixel = rowStart + leftX - Toy2::g_screenClipLeft;
						}

						do
						{
							uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x7C0)
								*pixel = texel;
							u += uStep;
							v += vStep;
							pixelCount--;
							pixel++;
						} while (pixelCount > 0);
					}
				}
				scanline++;
				rowStart += g_backBufferPitchPixels;
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
					uint16_t* pixel;
					if (leftX < Toy2::g_screenClipLeft)
					{
						int32_t clippedPixels = Toy2::g_screenClipLeft - leftX;
						v += clippedPixels * vStep;
						u += clippedPixels * uStep;
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
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
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

	// FUNCTION: TOY2 0x00468430 [PROVISIONAL]
	void RasterizeSubtractiveTexturedPolygon565(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneSubtractive565);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneSubtractive565);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneSubtractive565);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneSubtractive565);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneSubtractive565);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0x7C0)
					{
						uint16_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						int32_t red = (pixel & 0xF800) - (texel & 0xF800);
						if (red < 0x800)
							red = 0;
						int32_t green = (pixel & 0x7E0) - (texel & 0x7E0);
						if (green < 0x20)
							green = 0;
						int32_t blue = (pixel & 0x1F) - (texel & 0x1F);
						if (blue < 1)
							blue = 0;
						pixel = (uint16_t)(red + green + blue);
					}
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
					uint16_t* pixel;
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
						if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_screenClipRight - leftX;
						pixelCount++;
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					}

					do
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0x7C0)
						{
							int32_t red = (*pixel & 0xF800) - (texel & 0xF800);
							if (red < 0x800)
								red = 0;
							int32_t green = (*pixel & 0x7E0) - (texel & 0x7E0);
							if (green < 0x20)
								green = 0;
							int32_t blue = (*pixel & 0x1F) - (texel & 0x1F);
							if (blue < 1)
								blue = 0;
							*pixel = (uint16_t)(red + green + blue);
						}
						u += uStep;
						v += vStep;
						pixel++;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			scanline++;
			rowStart += g_backBufferPitchPixels;
			scanlineCount--;
		} while (scanlineCount != 0);
	}

	// FUNCTION: TOY2 0x00468E90 [PROVISIONAL]
	void RasterizeAdditiveTexturedPolygon565(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE(&item->vertices[0], &item->vertices[1], edge01DoneAdditive565);
		RASTERIZE_TEXTURED_EDGE(&item->vertices[1], &item->vertices[2], edge12DoneAdditive565);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[0], edge20DoneAdditive565);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE(&item->vertices[2], &item->vertices[3], edge23DoneAdditive565);
			RASTERIZE_TEXTURED_EDGE(&item->vertices[3], &item->vertices[0], edge30DoneAdditive565);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				int32_t rightX = scanline->rightXFixed >> SOLID_EDGE_X_FRACTION_BITS;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> k_fixedPointShift) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0x7C0)
					{
						uint16_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						int32_t red = (pixel & 0xF800) + (texel & 0xF800);
						if (red > 0xF800)
							red = 0xF800;
						int32_t green = (pixel & 0x7E0) + (texel & 0x7E0);
						if (green > 0x7E0)
							green = 0x7E0;
						int32_t blue = (pixel & 0x1F) + (texel & 0x1F);
						if (blue > 0x1F)
							blue = 0x1F;
						pixel = (uint16_t)(red + green + blue);
					}
				}
				else
				{
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
					uint16_t* pixel;
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
						if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_screenClipRight - leftX;
						pixelCount++;
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					}

					do
					{
						uint16_t texel = texture[(u >> k_fixedPointShift) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0x7C0)
						{
							int32_t red = (*pixel & 0xF800) + (texel & 0xF800);
							if (red > 0xF800)
								red = 0xF800;
							int32_t green = (*pixel & 0x7E0) + (texel & 0x7E0);
							if (green > 0x7E0)
								green = 0x7E0;
							int32_t blue = (*pixel & 0x1F) + (texel & 0x1F);
							if (blue > 0x1F)
								blue = 0x1F;
							*pixel = (uint16_t)(red + green + blue);
						}
						u += uStep;
						v += vStep;
						pixel++;
						pixelCount--;
					} while (pixelCount > 0);
				}
			}
			scanline++;
			rowStart += g_backBufferPitchPixels;
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
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
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
						if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_screenClipRight - leftX;
						pixelCount++;
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					}

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
					int32_t width = rightX - leftX;
					int32_t u = scanline->leftInterpolants[0];
					int32_t v = scanline->leftInterpolants[1];
					int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
					int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
					int32_t pixelCount = width;
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
						if (rightX > Toy2::g_screenClipRight)
							pixelCount = Toy2::g_screenClipRight - leftX;
						pixelCount++;
						pixel = rowStart + leftX - Toy2::g_screenClipLeft;
					}

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
						int32_t width = rightX - leftX;
						int32_t u = scanline->leftInterpolants[0];
						int32_t v = scanline->leftInterpolants[1];
						int32_t uStep = (scanline->rightInterpolants[0] - u) / width;
						int32_t vStep = (scanline->rightInterpolants[1] - v) / width;
						int32_t pixelCount = width;
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
							if (rightX > Toy2::g_screenClipRight)
								pixelCount = Toy2::g_screenClipRight - leftX;
							pixelCount++;
							pixel = rowStart + leftX - Toy2::g_screenClipLeft;
						}

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

#undef RASTERIZE_TEXTURED_EDGE
#undef RASTERIZE_TEXTURED_EDGE_EXCLUSIVE
#undef RASTERIZE_TEXTURED_EDGE_WITH_END
	// STUB: TOY2 0x00471B30
	void UnkRenderAPI30(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00472E70
	void UnkRenderAPI31(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00474C80
	void UnkRenderAPI33(SoftwareRenderItem* item) {}
// Index into g_paletteColourOffsetTable of one interpolated colour, before the texel row.
#define PALETTE_COLOUR_OFFSET(blue, green, red) (((blue) >> 12 & ~0x3F) + ((green) >> 15 & ~7) + ((red) >> 18))

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
		int32_t topY = item->vertices[0].y;
		int32_t bottomY = topY;
		// Vertex 1 extends the row range.
		if (item->vertices[1].y < topY)
			topY = item->vertices[1].y;
		// Vertex 1 extends the bottom.
		else if (item->vertices[1].y > bottomY)
			bottomY = item->vertices[1].y;

		// Vertex 2 extends the row range.
		if (item->vertices[2].y < topY)
			topY = item->vertices[2].y;
		// Vertex 2 extends the bottom.
		else if (item->vertices[2].y > bottomY)
			bottomY = item->vertices[2].y;

		// Quad: vertex 3 extends the row range.
		if (item->renderFlags & SOFTWARE_RENDER_QUAD)
		{
			if (item->vertices[3].y < topY)
				topY = item->vertices[3].y;
			else if (item->vertices[3].y > bottomY)
				bottomY = item->vertices[3].y;
		}

		// Clip the range to the screen top.
		if (topY < Toy2::g_screenClipTop)
			topY = Toy2::g_screenClipTop;
		// Clip the range to the screen bottom.
		if (bottomY > Toy2::g_screenClipBottom)
			bottomY = Toy2::g_screenClipBottom;

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

#undef PALETTE_COLOUR_OFFSET
#undef RASTERIZE_OVERLAY_EDGE
#undef RASTERIZE_LIT_OVERLAY_EDGE
#undef RASTERIZE_LIT_EDGE
#undef RASTERIZE_LIT_EDGE_INCLUSIVE
#undef RASTERIZE_LIT_EDGE_WITH_END

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

	// FUNCTION: TOY2 0x004B2B80 [MATCHED]
	int32_t GetStrideFromFVF(int32_t fvf)
	{
		switch (fvf)
		{
			case 0x112:
				return 0x20;
			case 0x152:
				return 0x24;
			case 0x1c4:
				return 0x20;
			default:
				return 0;
		}
	}

	// FUNCTION: TOY2 0x00452130 [MATCHED]
	void SwapRenderBuffer()
	{
		Toy2::MainMenu::g_menuClearColor.b = 0;
		Toy2::MainMenu::g_menuClearColor.g = 0;
		Toy2::MainMenu::g_menuClearColor.r = 0;
		g_currentRenderBuffer = (g_currentRenderBuffer == g_renderBufferA) ? g_renderBufferB : g_renderBufferA;
		g_renderBufferPixels = g_currentRenderBuffer + 0x4020;
	}

	// FUNCTION: TOY2 0x004C20E0 [MATCHED]
	void SetLevelFileIndex(int32_t index) { g_levelFileIndex = index; }

	// Builds one 256x256 colour-scale LUT into `tableGlobal`. The LUT maps
	// table[scale*256 + colour] = (min(colour*scale, 0xffff) >> shift) & mask,
	// placing a single colour component into its 16-bit channel position. The
	// table global is referenced directly (not cached) so MSVC6 reloads it each
	// inner iteration, matching retail's conservative-aliasing codegen.
	// FUNCTION: TOY2 0x00470BF0 [MATCHED]
	void SetPaletteOnAPI()
	{
		HRESULT result = d3dappi.lpDD->CreatePalette(0x44, (LPPALETTEENTRY)g_paletteEntries, &g_lpPalette, NULL);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpDD->CreatePalette(0x00000004l|0x00000040l,&pal[0],&SonicRPalette,0)", result);
		}
		result = d3dappi.lpFrontBuffer->SetPalette(g_lpPalette);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpFrontBuffer->SetPalette(SonicRPalette)", result);
		}
		result = d3dappi.lpBackBuffer->SetPalette(g_lpPalette);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpBackBuffer->SetPalette(SonicRPalette)", result);
		}
	}

	// FUNCTION: TOY2 0x00470C70 [MATCHED]
	void UpdatePaletteTint()
	{
		for (int32_t i = 0; i < 0x3fc; i += 4)
		{
			g_paletteEntries[i + 4] = (uint8_t)(g_paletteSource[i + 4] * Nu3D::Camera::g_cameraTintRed / 128);
			g_paletteEntries[i + 5] = (uint8_t)(g_paletteSource[i + 5] * Nu3D::Camera::g_cameraTintGreen / 128);
			g_paletteEntries[i + 6] = (uint8_t)(g_paletteSource[i + 6] * Nu3D::Camera::g_cameraTintBlue / 128);
		}
		g_lpPalette->SetEntries(0, 1, 255, (LPPALETTEENTRY)&g_paletteEntries[4]);
	}

	// FUNCTION: TOY2 0x00470D00 [PROVISIONAL]
	void LoadPaletteEntries(const uint8_t* source)
	{
		for (int32_t entry = 0; entry < 256; entry++)
		{
			g_paletteEntries[entry * 4] = 0;
			g_paletteEntries[entry * 4 + 1] = 0;
			g_paletteEntries[entry * 4 + 2] = 0;
		}

		for (int32_t sourceEntry = 0; sourceEntry < 256; sourceEntry++)
		{
			g_paletteSource[sourceEntry * 4] = source[sourceEntry * 3];
			g_paletteSource[sourceEntry * 4 + 1] = source[sourceEntry * 3 + 1];
			g_paletteSource[sourceEntry * 4 + 2] = source[sourceEntry * 3 + 2];
		}

		g_paletteSource[0] = 0;
		g_paletteSource[1] = 0;
		g_paletteSource[2] = 0;
		BuildPaletteLightingTable();
	}

	// FUNCTION: TOY2 0x004319E0 [PROVISIONAL]
	void BuildPaletteLightingTable()
	{
		for (int32_t lightLevel = 0; lightLevel < 128; lightLevel++)
		{
			for (int32_t sourceEntry = 0; sourceEntry < 256; sourceEntry++)
			{
				int32_t blue = g_paletteSource[sourceEntry * 4] * lightLevel / 64;
				int32_t green = g_paletteSource[sourceEntry * 4 + 1] * lightLevel / 64;
				int32_t red = g_paletteSource[sourceEntry * 4 + 2] * lightLevel / 64;
				if (blue > 255)
					blue = 255;
				if (green > 255)
					green = 255;
				if (red > 255)
					red = 255;

				int32_t bestDistance = 0x7fffffff;
				uint8_t bestEntry;
				for (int32_t candidate = 1; candidate < 256; candidate++)
				{
					int32_t blueDifference = abs(blue - g_paletteSource[candidate * 4]) * 4;
					int32_t greenDifference = abs(green - g_paletteSource[candidate * 4 + 1]) * 5;
					int32_t redDifference = abs(red - g_paletteSource[candidate * 4 + 2]) * 3;
					int32_t distance = blueDifference * blueDifference + greenDifference * greenDifference + redDifference * redDifference;
					if (distance < bestDistance)
					{
						bestDistance = distance;
						bestEntry = (uint8_t)candidate;
					}
				}
				g_paletteLightingTable[lightLevel][sourceEntry] = bestEntry;
			}
		}
		OutputDebugStringA("Generated lighting\n");
	}

	// FUNCTION: TOY2 0x00470D60 [PROVISIONAL]
	void BuildRGBToPaletteTable()
	{
		uint8_t* output = g_rgbToPaletteIndex;
		for (int32_t blue = 0; blue < 256; blue += 8)
		{
			for (int32_t green = 0; green < 256; green += 8)
			{
				for (int32_t red = 0; red < 256; red += 8)
				{
					uint8_t nearestEntry = 0;
					int32_t nearestDistance = 9999999;
					for (int32_t entry = 1; entry < 256; entry++)
					{
						int32_t blueDifference = (g_paletteSource[entry * 4] - blue) * 4;
						int32_t greenDifference = (g_paletteSource[entry * 4 + 1] - green) * 5;
						int32_t redDifference = (g_paletteSource[entry * 4 + 2] - red) * 3;
						int32_t totalDifference = blueDifference + greenDifference + redDifference;
						int32_t distance = totalDifference * totalDifference + blueDifference * blueDifference + greenDifference * greenDifference
							+ redDifference * redDifference;
						if (distance < nearestDistance)
						{
							nearestEntry = (uint8_t)entry;
							nearestDistance = distance;
						}
					}
					*output++ = nearestEntry;
				}
			}
		}
	}

	// FUNCTION: TOY2 0x00471190 [PROVISIONAL]
	void BuildAdditivePaletteTable()
	{
		uint8_t* output = g_additivePaletteTable;
		for (int32_t first = 0; first < 256; first++)
		{
			int32_t firstBlue = g_paletteSource[first * 4];
			int32_t firstGreen = g_paletteSource[first * 4 + 1];
			int32_t firstRed = g_paletteSource[first * 4 + 2];
			for (int32_t second = 0; second < 256; second++)
			{
				int32_t blue = g_paletteSource[second * 4] + firstBlue;
				int32_t green = g_paletteSource[second * 4 + 1] + firstGreen;
				int32_t red = g_paletteSource[second * 4 + 2] + firstRed;
				if (blue > 255)
					blue = 255;
				if (green > 255)
					green = 255;
				if (red > 255)
					red = 255;
				int32_t lookup = ((blue & ~7) * 32 + (green & ~7)) * 4 + (red >> 3);
				*output++ = g_rgbToPaletteIndex[lookup];
			}
		}
	}

	// FUNCTION: TOY2 0x00471250 [PROVISIONAL]
	void BuildSubtractivePaletteTable()
	{
		uint8_t* output = g_subtractivePaletteTable;
		for (int32_t first = 0; first < 256; first++)
		{
			int32_t firstBlue = g_paletteSource[first * 4];
			int32_t firstGreen = g_paletteSource[first * 4 + 1];
			int32_t firstRed = g_paletteSource[first * 4 + 2];
			for (int32_t second = 0; second < 256; second++)
			{
				int32_t blue = firstBlue - g_paletteSource[second * 4];
				int32_t green = firstGreen - g_paletteSource[second * 4 + 1];
				int32_t red = firstRed - g_paletteSource[second * 4 + 2];
				if (blue < 0)
					blue = 0;
				if (green < 0)
					green = 0;
				if (red < 0)
					red = 0;
				int32_t lookup = ((blue & ~7) * 32 + (green & ~7)) * 4 + (red >> 3);
				*output++ = g_rgbToPaletteIndex[lookup];
			}
		}
	}

	// FUNCTION: TOY2 0x004710C0 [PROVISIONAL]
	void BuildPaletteBlendTable(uint8_t* output, int32_t blendWeight)
	{
		int32_t baseWeight = 1024 - blendWeight;
		for (int32_t baseEntry = 0; baseEntry < 256; baseEntry++)
		{
			int32_t baseBlue = g_paletteSource[baseEntry * 4] * baseWeight;
			int32_t baseGreen = g_paletteSource[baseEntry * 4 + 1] * baseWeight;
			int32_t baseRed = g_paletteSource[baseEntry * 4 + 2] * baseWeight;
			for (int32_t blendEntry = 0; blendEntry < 256; blendEntry++)
			{
				int32_t blue = (g_paletteSource[blendEntry * 4] * blendWeight + baseBlue) >> 10;
				int32_t green = (g_paletteSource[blendEntry * 4 + 1] * blendWeight + baseGreen) >> 10;
				int32_t red = (g_paletteSource[blendEntry * 4 + 2] * blendWeight + baseRed) >> 10;
				int32_t lookup = ((blue & ~7) * 32 + (green & ~7)) * 4 + (red >> 3);
				*output++ = g_rgbToPaletteIndex[lookup];
			}
		}
	}

	// FUNCTION: TOY2 0x00470FB0 [PROVISIONAL]
	void BuildPaletteColourOffsetTable()
	{
		uint8_t* output = g_paletteColourOffsetTable;
		uint8_t* palette = g_paletteSource + 1;
		do
		{
			int32_t sourceGreen = palette[0];
			int32_t sourceBlue = palette[-1];
			int32_t sourceRed = palette[1];
			int32_t blue = sourceBlue - 96;
			int32_t blueCount = 8;
			do
			{
				int32_t green = sourceGreen - 96;
				int32_t greenCount = 8;
				do
				{
					int32_t red = sourceRed - 96;
					int32_t redCount = 8;
					do
					{
						int32_t clampedBlue = blue;
						int32_t clampedGreen = green;
						int32_t clampedRed = red;
						if (clampedBlue < 0)
							clampedBlue = 0;
						else if (clampedBlue > 255)
							clampedBlue = 255;
						if (clampedGreen < 0)
							clampedGreen = 0;
						else if (clampedGreen > 255)
							clampedGreen = 255;
						if (clampedRed < 0)
							clampedRed = 0;
						else if (clampedRed > 255)
							clampedRed = 255;
						int32_t lookup = ((clampedBlue & ~7) * 32 + (clampedGreen & ~7)) * 4 + (clampedRed >> 3);
						*output++ = g_rgbToPaletteIndex[lookup];
						red += 32;
						redCount--;
					} while (redCount != 0);
					green += 32;
					greenCount--;
				} while (greenCount != 0);
				blue += 32;
				blueCount--;
			} while (blueCount != 0);
			palette += 4;
		} while (palette < g_paletteSource + sizeof(g_paletteSource) + 1);
	}

	enum PaletteTableFlags
	{
		PALETTE_TABLE_RGB = 0x1,
		PALETTE_TABLE_BLEND = 0x10,
		PALETTE_TABLE_ADDITIVE = 0x100,
		PALETTE_TABLE_SUBTRACTIVE = 0x1000,
		PALETTE_TABLE_COLOUR_OFFSET = 0x10000
	};

	// FUNCTION: TOY2 0x00471300 [MATCHED]
	void SetNewPalette(const uint8_t* source, uint32_t tableFlags)
	{
		Logger::Log("SOFT : SetNewPalette.\n");
		LoadPaletteEntries(source);

		if (g_rgbToPaletteIndex == NULL)
			g_rgbToPaletteIndex = (uint8_t*)malloc(0x8000);
		if (g_paletteBlend25Table == NULL)
			g_paletteBlend25Table = (uint8_t*)malloc(0x10000);
		if (g_paletteBlend50Table == NULL)
			g_paletteBlend50Table = (uint8_t*)malloc(0x10000);
		if (g_paletteBlend75Table == NULL)
			g_paletteBlend75Table = (uint8_t*)malloc(0x10000);
		if (g_additivePaletteTable == NULL)
			g_additivePaletteTable = (uint8_t*)malloc(0x10000);
		if (g_subtractivePaletteTable == NULL)
			g_subtractivePaletteTable = (uint8_t*)malloc(0x10000);
		if (g_paletteColourOffsetTable == NULL)
			g_paletteColourOffsetTable = (uint8_t*)malloc(0x20000);

		if (tableFlags & PALETTE_TABLE_RGB)
			BuildRGBToPaletteTable();
		if (tableFlags & PALETTE_TABLE_BLEND)
		{
			BuildPaletteBlendTable(g_paletteBlend25Table, 0x100);
			BuildPaletteBlendTable(g_paletteBlend50Table, 0x200);
			BuildPaletteBlendTable(g_paletteBlend75Table, 0x300);
		}
		if (tableFlags & PALETTE_TABLE_ADDITIVE)
			BuildAdditivePaletteTable();
		if (tableFlags & PALETTE_TABLE_SUBTRACTIVE)
			BuildSubtractivePaletteTable();
		if (tableFlags & PALETTE_TABLE_COLOUR_OFFSET)
			BuildPaletteColourOffsetTable();
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

#define NU_FMIN(a, b) ((a) < (b) ? (a) : (b))
	// FUNCTION: TOY2 0x004B5E40 [MATCHED]
	void SubmitSortedTriangle(
		int32_t renderFlags, Renderer::RenderEntry* renderEntry, int32_t textureIndex, Nu3D::VertexTL* v0, Nu3D::VertexTL* v1, Nu3D::VertexTL* v2)
	{
		if (g_disableSortedPrimitiveSubmission != 0)
		{
			return;
		}
		if (Renderer::g_primitiveBufferFreeCount == 0)
		{
			return;
		}

		Renderer::g_primitiveBufferFreeCount--;
		Renderer::SortedPrimitive* record = &Renderer::g_primitiveBuffer[Renderer::g_primitiveBufferFreeCount];
		record->renderFlags = renderFlags;
		record->renderEntry = renderEntry;
		record->textureIndex = textureIndex;
		record->v0 = *v0;
		record->v1 = *v1;
		record->v2 = *v2;
		float depthKey = NU_FMIN(record->v0.position.z, NU_FMIN(record->v1.position.z, record->v2.position.z)) * k_depthSortScale;
		record->depthKey = depthKey;
		int32_t bucket = (int32_t)depthKey & 0x3ff;
		Renderer::SortedPrimitive* node = (Renderer::SortedPrimitive*)Renderer::g_renderBuckets[bucket];
		Renderer::SortedPrimitive* prev = NULL;
		for (;;)
		{
			if (node == NULL)
			{
				break;
			}
			if (node->depthKey <= depthKey)
			{
				break;
			}
			prev = node;
			node = node->next;
		}
		if (prev != NULL)
		{
			prev->next = record;
		}
		else
		{
			Renderer::g_renderBuckets[bucket] = record;
		}
		record->next = node;
	}
#undef NU_FMIN

	// FUNCTION: TOY2 0x004B5FB0 [MATCHED]
	void SubmitTriangleList(int32_t renderFlags, LPDIRECT3DVERTEXBUFFER vertexBuffer, Renderer::RenderEntry* renderEntry, WORD* indices, int32_t indexCount)
	{
		Nu3D::VertexTL* lockedVertices;
		if (DrawingAPI::LockVertexBuffer(vertexBuffer, 0x801, (LPVOID*)&lockedVertices, 0) == 0)
		{
			if (indexCount != 0)
			{
				WORD* p = indices + indexCount + 1;
				do
				{
					p -= 3;
					indexCount -= 3;
					SubmitSortedTriangle(renderFlags, renderEntry, 0, &lockedVertices[p[-1]], &lockedVertices[p[0]], &lockedVertices[p[1]]);
				} while (indexCount != 0);
			}
			DrawingAPI::UnlockVertexBuffer(vertexBuffer);
		}
	}

	// FUNCTION: TOY2 0x004B6040 [PROVISIONAL]
	void SubmitTriangleStrip(int32_t renderFlags, LPDIRECT3DVERTEXBUFFER vertexBuffer, Renderer::RenderEntry* renderEntry, WORD* indices, int32_t indexCount)
	{
		Nu3D::VertexTL* lockedVertices;
		if (DrawingAPI::LockVertexBuffer(vertexBuffer, 0x801, (LPVOID*)&lockedVertices, 0) == 0)
		{
			WORD* p = indices;
			uint32_t a = *p++;
			uint32_t b = *p++;
			uint32_t c = *p++;
			int32_t remaining = indexCount - 3;
			SubmitSortedTriangle(renderFlags, renderEntry, 0, &lockedVertices[a], &lockedVertices[b], &lockedVertices[c]);
			while (remaining != 0)
			{
				a = b;
				b = c;
				remaining--;
				c = *p++;
				if (remaining & 1)
				{
					SubmitSortedTriangle(renderFlags, renderEntry, 0, &lockedVertices[a], &lockedVertices[b], &lockedVertices[c]);
				}
				else
				{
					SubmitSortedTriangle(renderFlags, renderEntry, 0, &lockedVertices[c], &lockedVertices[b], &lockedVertices[a]);
				}
			}
			DrawingAPI::UnlockVertexBuffer(vertexBuffer);
		}
	}

	// FUNCTION: TOY2 0x004B6140 [MATCHED]
	void SubmitTriangleStripRaw(int32_t renderFlags, Nu3D::VertexTL* lockedVertices, Renderer::RenderEntry* renderEntry, WORD* indices, int32_t indexCount)
	{
		WORD* p = indices;
		uint32_t a = *p++;
		uint32_t b = *p++;
		uint32_t c = *p++;
		indexCount -= 3;
		SubmitSortedTriangle(renderFlags, renderEntry, 0, &lockedVertices[a], &lockedVertices[b], &lockedVertices[c]);
		while (indexCount != 0)
		{
			a = b;
			b = c;
			indexCount--;
			c = *p++;
			if (indexCount & 1)
			{
				SubmitSortedTriangle(renderFlags, renderEntry, 0, &lockedVertices[a], &lockedVertices[b], &lockedVertices[c]);
			}
			else
			{
				SubmitSortedTriangle(renderFlags, renderEntry, 0, &lockedVertices[c], &lockedVertices[b], &lockedVertices[a]);
			}
		}
	}

	// FUNCTION: TOY2 0x004B6220 [MATCHED]
	void SubmitQuad(int32_t renderFlags, int32_t textureIndex, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices)
	{
		Nu3D::VertexTL* lockedVertices;
		if (DrawingAPI::LockVertexBuffer(vertexBuffer, 0x801, (LPVOID*)&lockedVertices, 0) == 0)
		{
			SubmitSortedTriangle(renderFlags, NULL, textureIndex, &lockedVertices[indices[0]], &lockedVertices[indices[1]], &lockedVertices[indices[2]]);
			SubmitSortedTriangle(renderFlags, NULL, textureIndex, &lockedVertices[2], &lockedVertices[indices[1]], &lockedVertices[indices[3]]);
			DrawingAPI::UnlockVertexBuffer(vertexBuffer);
		}
	}
}

namespace SoftwareDevice
{
	// A software vertex buffer is a malloc'd block: a 2-dword header (per-vertex
	// stride and vertex count) followed by the vertex data. The public handle
	// type is LPDIRECT3DVERTEXBUFFER; the software device casts it to this
	// header to access the payload.
	struct SoftwareVertexBuffer
	{
		int32_t stride;
		int32_t count;
	};

	// Drawing Methods

	// FUNCTION: TOY2 0x004B9950 [MATCHED]
	HRESULT DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE primitiveType, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices, DWORD indexCount, DWORD flags)
	{
		LPVOID lockedVertices;
		if (DrawingAPI::LockVertexBuffer(vertexBuffer, 0x801, &lockedVertices, 0) == 0)
		{
			SoftwareRenderer::TextureData scratch;
			SoftwareRenderer::GetCurrentTextureData(&scratch);
			if (SoftwareRenderer::g_viewportRect != NULL)
			{
				Nu3D::Viewport::ViewportRect* rect = SoftwareRenderer::g_viewportRect;
				SoftwareRenderer::UpdateViewportClipBounds((int32_t)rect->top, (int32_t)rect->bottom, (int32_t)rect->left, (int32_t)rect->right);
			}
			SoftwareRenderer::ProcessIndexedPrimitive(primitiveType, lockedVertices, indices, indexCount, flags);
			DrawingAPI::UnlockVertexBuffer(vertexBuffer);
		}
		return 0;
	}

	// FUNCTION: TOY2 0x004B99F0 [MATCHED]
	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE d3dptPrimitiveType,
		DWORD dwVertexTypeDesc,
		LPVOID lpvVertices,
		DWORD dwVertexCount,
		LPWORD lpwIndices,
		DWORD dwIndexCount,
		DWORD dwFlags)
	{
		SoftwareRenderer::TextureData scratch;
		SoftwareRenderer::GetCurrentTextureData(&scratch);
		if (SoftwareRenderer::g_viewportRect != NULL)
		{
			Nu3D::Viewport::ViewportRect* rect = SoftwareRenderer::g_viewportRect;
			SoftwareRenderer::UpdateViewportClipBounds((int32_t)rect->top, (int32_t)rect->bottom, (int32_t)rect->left, (int32_t)rect->right);
		}
		SoftwareRenderer::ProcessIndexedPrimitive(d3dptPrimitiveType, lpvVertices, lpwIndices, dwIndexCount, dwFlags);
		return 0;
	}

	// Vertex Methods

	// FUNCTION: TOY2 0x004B2B20 [MATCHED]
	HRESULT ReleaseVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer)
	{
		free(buffer);
		return 0;
	}

	// FUNCTION: TOY2 0x004B2B30 [MATCHED]
	HRESULT CreateVertexBuffer(D3DVERTEXBUFFERDESC* desc, LPDIRECT3DVERTEXBUFFER* outBuffer, DWORD flags)
	{
		int32_t count = (int32_t)desc->dwNumVertices;
		int32_t stride = SoftwareRenderer::GetStrideFromFVF((int32_t)desc->dwFVF);
		if (stride != 0)
		{
			SoftwareVertexBuffer* vb = (SoftwareVertexBuffer*)malloc(sizeof(SoftwareVertexBuffer) + stride * count);
			if (vb != NULL)
			{
				vb->stride = stride;
				vb->count = count;
				*outBuffer = (LPDIRECT3DVERTEXBUFFER)vb;
				return 0;
			}
			return E_OUTOFMEMORY;
		}
		return E_INVALIDARG;
	}

	// FUNCTION: TOY2 0x004B2BB0 [MATCHED]
	HRESULT LockVertexBuffer(LPDIRECT3DVERTEXBUFFER vertexBuffer, DWORD dwFlags, LPVOID* lplpData, DWORD* lpStride)
	{
		SoftwareVertexBuffer* vb = (SoftwareVertexBuffer*)vertexBuffer;
		*lplpData = vb + 1;
		if (lpStride != NULL)
			*lpStride = vb->count * vb->stride;
		return 0;
	}

	// FUNCTION: TOY2 0x004B2BD0 [MATCHED]
	HRESULT UnlockVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer) { return 0; }

	// FUNCTION: TOY2 0x004B2BE0 [MATCHED]
	HRESULT OptimizeVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer, LPDIRECT3DDEVICE3 device, DWORD flags) { return 0; }
}

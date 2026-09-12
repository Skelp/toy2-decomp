#include "SoftwareRenderer.h"
#include "SoftwareRendererInternal.h"
#include "Renderer/Renderer.h"
#include "Toy2/Toy2.h"
#include "Nu3D/Math.h"

// The span and quad rasterizers of the software device. They consume the
// RenderCommand queue: RasterizeRenderCommand and RasterizeSortedRenderCommand
// choose one span variant per command, publish it in g_spanRasterizer, and the
// scanline walkers call that variant once per row.
//
// Retail band 0x004C4340-0x004C9D00. The retail assert of nu3d\world.c sits at
// 0x004C41AA and the one of nu3d\hobjload.c at 0x004CA3EA, so this band is one
// retail object between those two units.
namespace SoftwareRenderer
{
	// Texture size, channel masks and the colour-scale subtable size. Every
	// rasterizer band of the software renderer addresses the same 256x256
	// textures and the same five-bit channels, so each band declares the set it
	// spells. SoftwareRendererInternal.h is the place for these, but the
	// unnamed-constant rule of tools/decomp lint resolves a name only inside the
	// file that uses it, so a shared header would hide every bare literal that
	// still stands where one of these names belongs.
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

	// GLOBAL: TOY2 0x00B626B0
	SpanRasterizer g_spanRasterizer;

	// Vertex 0's diffuse alpha, and its complement 255 - alpha. The two blending
	// span rasterizers read them instead of re-reading the vertex, so the
	// selector has to publish them whenever it picks a blending path.

	// GLOBAL: TOY2 0x00B626AC
	int32_t g_spanAlpha;

	// GLOBAL: TOY2 0x00B626B8
	int32_t g_spanInvAlpha;

	// The span rasterizers use 8.8 fixed-point texture coordinates.
	// GLOBAL: TOY2 0x004DDB58
	extern const double k_textureCoordinateScale = 256.0;

	// FUNCTION: TOY2 0x004C4340 [MATCHED]
	void UnpackColourChannels(uint32_t colour, int32_t* red, int32_t* green, int32_t* blue)
	{
		*red = (colour & 0x00ff0000) >> 8;
		*green = colour & 0x0000ff00;
		*blue = (colour & 0x000000ff) << 8;
	}

	// FUNCTION: TOY2 0x004C4640 [PROVISIONAL]
	void RasterizeTexturedSpanPairSample(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
			return;

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t pairCount = width >> 1;
			int32_t stepRed;
			int32_t stepGreen;
			int32_t stepBlue;
			if (pairCount > 0)
			{
				stepRed = (farRed - edgeBRed) / pairCount;
				stepGreen = (farGreen - edgeBGreen) / pairCount;
				stepBlue = (farBlue - edgeBBlue) / pairCount;
			}

			int32_t startX = (int32_t)edgeB->position.x;
			int32_t endX = (int32_t)edgeA->position.x;
			destRow += startX;

			int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale);
			if (textureU > k_textureCoordinateFixedMax)
				textureU = k_textureCoordinateFixedMax;
			textureU <<= k_textureCoordinateShift;

			int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
			if (textureVValue > k_textureCoordinateFixedMax)
				textureVValue = k_textureCoordinateFixedMax;
			int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

			int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale);
			if (farTextureU > k_textureCoordinateMax)
				farTextureU = k_textureCoordinateMax;
			farTextureU <<= k_textureCoordinateShift;
			if (farTextureU > k_textureCoordinateFixedMax)
				farTextureU = k_textureCoordinateFixedMax;
			int32_t stepTextureU = (farTextureU - textureU) / width;

			int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
			if (farTextureVValue > k_textureCoordinateMax)
				farTextureVValue = k_textureCoordinateMax;
			int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
			if (farTextureV > k_textureCoordinateFixedMax)
				farTextureV = k_textureCoordinateFixedMax;
			int32_t stepTextureV = (farTextureV - textureV) / width;

			if (startX & 1)
			{
				int32_t textureIndex = ((textureV >> 8) & 0xff) * 256 + ((textureU >> 8) & 0xff);
				uint32_t texel = texData[textureIndex];
				*destRow++ = g_colourScaleTable0[(edgeBBlue & 0xff00) + (texel & 0xff)]
					+ g_colourScaleTable0[0x10000 + (edgeBGreen & 0xff00) + ((texel >> 8) & 0xff)]
					+ g_colourScaleTable0[0x20000 + (edgeBRed & 0xff00) + ((texel >> 16) & 0xff)];
				pairCount = (width - 1) >> 1;
				textureU += stepTextureU;
				textureV += stepTextureV;
			}

			while (pairCount != 0)
			{
				int32_t textureIndex = ((textureV >> 8) & 0xff) * 256 + ((textureU >> 8) & 0xff);
				uint32_t texel = texData[textureIndex];
				uint16_t pixel = g_colourScaleTable0[(edgeBBlue & 0xff00) + (texel & 0xff)]
					+ g_colourScaleTable0[0x10000 + (edgeBGreen & 0xff00) + ((texel >> 8) & 0xff)]
					+ g_colourScaleTable0[0x20000 + (edgeBRed & 0xff00) + ((texel >> 16) & 0xff)];
				uint32_t* destPair = (uint32_t*)destRow;
				*destPair = pixel | ((uint32_t)pixel << 16);
				destRow += 2;
				textureU += stepTextureU * 2;
				textureV += stepTextureV * 2;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				pairCount--;
			}

			if (endX & 1)
			{
				int32_t textureIndex = ((textureV >> 8) & 0xff) * 256 + ((textureU >> 8) & 0xff);
				uint32_t texel = texData[textureIndex];
				*destRow = g_colourScaleTable0[(edgeBBlue & 0xff00) + (texel & 0xff)]
					+ g_colourScaleTable0[0x10000 + (edgeBGreen & 0xff00) + ((texel >> 8) & 0xff)]
					+ g_colourScaleTable0[0x20000 + (edgeBRed & 0xff00) + ((texel >> 16) & 0xff)];
			}
		}
	}

	// FUNCTION: TOY2 0x004C4370 [PROVISIONAL]
	void RasterizeTexturedSpan(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
			return;

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t pairCount = width >> 1;
			int32_t stepRed;
			int32_t stepGreen;
			int32_t stepBlue;
			if (pairCount > 0)
			{
				stepRed = (farRed - edgeBRed) / pairCount;
				stepGreen = (farGreen - edgeBGreen) / pairCount;
				stepBlue = (farBlue - edgeBBlue) / pairCount;
			}

			int32_t startX = (int32_t)edgeB->position.x;
			int32_t endX = (int32_t)edgeA->position.x;
			destRow += startX;

			int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale);
			if (textureU > k_textureCoordinateFixedMax)
				textureU = k_textureCoordinateFixedMax;
			textureU <<= k_textureCoordinateShift;

			int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
			if (textureVValue > k_textureCoordinateFixedMax)
				textureVValue = k_textureCoordinateFixedMax;
			int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

			int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale);
			if (farTextureU > k_textureCoordinateMax)
				farTextureU = k_textureCoordinateMax;
			farTextureU <<= k_textureCoordinateShift;
			if (farTextureU > k_textureCoordinateFixedMax)
				farTextureU = k_textureCoordinateFixedMax;
			int32_t stepTextureU = (farTextureU - textureU) / width;

			int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
			if (farTextureVValue > k_textureCoordinateMax)
				farTextureVValue = k_textureCoordinateMax;
			int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
			if (farTextureV > k_textureCoordinateFixedMax)
				farTextureV = k_textureCoordinateFixedMax;
			int32_t stepTextureV = (farTextureV - textureV) / width;

			if (startX & 1)
			{
				int32_t textureIndex = ((textureV >> 8) & 0xff) * 256 + ((textureU >> 8) & 0xff);
				uint32_t texel = texData[textureIndex];
				*destRow++ = g_colourScaleTable0[(edgeBBlue & 0xff00) + (texel & 0xff)]
					+ g_colourScaleTable0[0x10000 + (edgeBGreen & 0xff00) + ((texel >> 8) & 0xff)]
					+ g_colourScaleTable0[0x20000 + (edgeBRed & 0xff00) + ((texel >> 16) & 0xff)];
				pairCount = (width - 1) >> 1;
				textureU += stepTextureU;
				textureV += stepTextureV;
			}

			while (pairCount != 0)
			{
				int32_t textureIndex0 = ((textureV >> 8) & 0xff) * 256 + ((textureU >> 8) & 0xff);
				int32_t nextTextureU = textureU + stepTextureU;
				int32_t nextTextureV = textureV + stepTextureV;
				int32_t textureIndex1 = ((nextTextureV >> 8) & 0xff) * 256 + ((nextTextureU >> 8) & 0xff);
				uint32_t texel0 = texData[textureIndex0];
				uint32_t texel1 = texData[textureIndex1];
				uint16_t pixel0 = g_colourScaleTable0[(edgeBBlue & 0xff00) + (texel0 & 0xff)]
					+ g_colourScaleTable0[0x10000 + (edgeBGreen & 0xff00) + ((texel0 >> 8) & 0xff)]
					+ g_colourScaleTable0[0x20000 + (edgeBRed & 0xff00) + ((texel0 >> 16) & 0xff)];
				uint16_t pixel1 = g_colourScaleTable0[(edgeBBlue & 0xff00) + (texel1 & 0xff)]
					+ g_colourScaleTable0[0x10000 + (edgeBGreen & 0xff00) + ((texel1 >> 8) & 0xff)]
					+ g_colourScaleTable0[0x20000 + (edgeBRed & 0xff00) + ((texel1 >> 16) & 0xff)];
				uint32_t* destPair = (uint32_t*)destRow;
				*destPair = pixel0 | ((uint32_t)pixel1 << 16);
				destRow += 2;
				textureU += stepTextureU * 2;
				textureV += stepTextureV * 2;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				pairCount--;
			}

			if (endX & 1)
			{
				int32_t textureIndex = ((textureV >> 8) & 0xff) * 256 + ((textureU >> 8) & 0xff);
				uint32_t texel = texData[textureIndex];
				*destRow = g_colourScaleTable0[(edgeBBlue & 0xff00) + (texel & 0xff)]
					+ g_colourScaleTable0[0x10000 + (edgeBGreen & 0xff00) + ((texel >> 8) & 0xff)]
					+ g_colourScaleTable0[0x20000 + (edgeBRed & 0xff00) + ((texel >> 16) & 0xff)];
			}
		}
	}

	// FUNCTION: TOY2 0x004C48E0 [PROVISIONAL]
	void RasterizeOpaqueSpan555(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
#define SPAN_PACK_RED(component) (((uint32_t)(component) & 0xf800) >> 1)
#define SPAN_PACK_GREEN(component) (((uint32_t)(component) & 0xf800) >> 6)
#include "SoftwareDeviceRasterOpaqueSpan.inc"
#undef SPAN_PACK_GREEN
#undef SPAN_PACK_RED
	}

	// FUNCTION: TOY2 0x004C4A60 [PROVISIONAL]
	void RasterizeOpaqueSpan565(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
#define SPAN_PACK_RED(component) ((uint32_t)(component) & 0xf800)
#define SPAN_PACK_GREEN(component) (((uint32_t)(component) & 0xf800) >> 5)
#include "SoftwareDeviceRasterOpaqueSpan.inc"
#undef SPAN_PACK_GREEN
#undef SPAN_PACK_RED
	}

	// FUNCTION: TOY2 0x004C4BE0 [PROVISIONAL]
	void RasterizeTexturedAdditiveSpan555(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale);
			if (textureU > k_textureCoordinateFixedMax)
			{
				textureU = k_textureCoordinateFixedMax;
			}
			textureU <<= k_textureCoordinateShift;

			int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
			if (textureVValue > k_textureCoordinateFixedMax)
			{
				textureVValue = k_textureCoordinateFixedMax;
			}
			int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

			int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale);
			if (farTextureU > k_textureCoordinateMax)
			{
				farTextureU = k_textureCoordinateMax;
			}
			farTextureU <<= k_textureCoordinateShift;
			if (farTextureU > k_textureCoordinateFixedMax)
			{
				farTextureU = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureU = (farTextureU - textureU) / width;

			int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
			if (farTextureVValue > k_textureCoordinateMax)
			{
				farTextureVValue = k_textureCoordinateMax;
			}
			int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
			if (farTextureV > k_textureCoordinateFixedMax)
			{
				farTextureV = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureV = (farTextureV - textureV) / width;

			do
			{
				int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
					+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
				uint32_t texel = texData[textureIndex];
				if (texel > k_rgbMask)
				{
					uint32_t pixel = *(const uint32_t*)destRow;

					int32_t blue = (int32_t)(pixel & k_fiveBitChannelMask) + g_colourScaleTable3[(edgeBBlue & k_upperByteMask) + (texel & k_lowerByteMask)];
					if (blue >= k_fiveBitChannelLimit)
					{
						blue = k_fiveBitChannelMask;
					}

					int32_t green =
						(int32_t)((pixel >> 5) & k_fiveBitChannelMask) + g_colourScaleTable3[(edgeBGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)];
					if (green >= k_fiveBitChannelLimit)
					{
						green = k_fiveBitChannelMask;
					}

					int32_t red =
						(int32_t)((pixel >> 10) & k_fiveBitChannelMask) + g_colourScaleTable3[(edgeBRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					if (red >= k_fiveBitChannelLimit)
					{
						red = k_fiveBitChannelMask;
					}

					*destRow = (uint16_t)((red << 10) + (green << 5) + blue);
				}

				destRow++;
				textureU += stepTextureU;
				textureV += stepTextureV;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C4E00 [PROVISIONAL]
	void RasterizeAdditiveSpan555(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		// The caller does not order the endpoints. Keep the one with the smaller x
		// in edgeB and its colours in the edgeB accumulators, so the walk below
		// always runs to increasing columns. farRed/Green/Blue hold the other end.
		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			edgeB = edgeA;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			do
			{
				// Retail reads a full dword through the 16-bit cursor and keeps only
				// the low pixel. The upper half is discarded by the channel masks.
				uint32_t pixel = *(const uint32_t*)destRow;

				int32_t channel = (int32_t)(pixel & 0x1f) + (int32_t)((uint32_t)edgeBBlue >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}
				int32_t out = channel;

				channel = (int32_t)((pixel >> 5) & 0x1f) + (int32_t)((uint32_t)edgeBGreen >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}
				out += channel << 5;

				channel = (int32_t)((pixel >> 10) & 0x1f) + (int32_t)((uint32_t)edgeBRed >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}

				*destRow = (uint16_t)((channel << 10) + out);
				destRow++;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C4F30 [PROVISIONAL]
	void RasterizeAdditiveSpan565(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		// The caller does not order the endpoints. Keep the one with the smaller x
		// in edgeB and its colours in the edgeB accumulators, so the walk below
		// always runs to increasing columns. farRed/Green/Blue hold the other end.
		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			edgeB = edgeA;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			do
			{
				// Retail reads a full dword through the 16-bit cursor and keeps only
				// the low pixel. The upper half is discarded by the channel masks.
				uint32_t pixel = *(const uint32_t*)destRow;

				int32_t channel = (int32_t)(pixel & 0x1f) + (int32_t)((uint32_t)edgeBBlue >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}
				int32_t out = channel;

				channel = (int32_t)((pixel >> 6) & 0x1f) + (int32_t)((uint32_t)edgeBGreen >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}
				out += channel << 6;

				channel = (int32_t)((pixel >> 11) & 0x1f) + (int32_t)((uint32_t)edgeBRed >> 11);
				if (channel >= 0x20)
				{
					channel = 0x1f;
				}

				*destRow = (uint16_t)((channel << 11) + out);
				destRow++;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C5060 [PROVISIONAL]
	void RasterizeTexturedAdditiveSpan565(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale);
			if (textureU > k_textureCoordinateFixedMax)
			{
				textureU = k_textureCoordinateFixedMax;
			}
			textureU <<= k_textureCoordinateShift;

			int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
			if (textureVValue > k_textureCoordinateFixedMax)
			{
				textureVValue = k_textureCoordinateFixedMax;
			}
			int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

			int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale);
			if (farTextureU > k_textureCoordinateMax)
			{
				farTextureU = k_textureCoordinateMax;
			}
			farTextureU <<= k_textureCoordinateShift;
			if (farTextureU > k_textureCoordinateFixedMax)
			{
				farTextureU = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureU = (farTextureU - textureU) / width;

			int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
			if (farTextureVValue > k_textureCoordinateMax)
			{
				farTextureVValue = k_textureCoordinateMax;
			}
			int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
			if (farTextureV > k_textureCoordinateFixedMax)
			{
				farTextureV = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureV = (farTextureV - textureV) / width;

			do
			{
				int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
					+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
				uint32_t texel = texData[textureIndex];
				if (texel > k_rgbMask)
				{
					uint32_t pixel = *(const uint32_t*)destRow;

					int32_t blue = (int32_t)(pixel & k_fiveBitChannelMask) + g_colourScaleTable3[(edgeBBlue & k_upperByteMask) + (texel & k_lowerByteMask)];
					if (blue >= k_fiveBitChannelLimit)
					{
						blue = k_fiveBitChannelMask;
					}

					int32_t green =
						(int32_t)((pixel >> 6) & k_fiveBitChannelMask) + g_colourScaleTable3[(edgeBGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)];
					if (green >= k_fiveBitChannelLimit)
					{
						green = k_fiveBitChannelMask;
					}

					int32_t red =
						(int32_t)((pixel >> 11) & k_fiveBitChannelMask) + g_colourScaleTable3[(edgeBRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					if (red >= k_fiveBitChannelLimit)
					{
						red = k_fiveBitChannelMask;
					}

					*destRow = (uint16_t)((red << 11) + (green << 6) + blue);
				}

				destRow++;
				textureU += stepTextureU;
				textureV += stepTextureV;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C5280 [PROVISIONAL]
	void RasterizeTexturedSubtractiveSpan555(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale);
			if (textureU > k_textureCoordinateFixedMax)
			{
				textureU = k_textureCoordinateFixedMax;
			}
			textureU <<= k_textureCoordinateShift;

			int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
			if (textureVValue > k_textureCoordinateFixedMax)
			{
				textureVValue = k_textureCoordinateFixedMax;
			}
			int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

			int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale);
			if (farTextureU > k_textureCoordinateMax)
			{
				farTextureU = k_textureCoordinateMax;
			}
			farTextureU <<= k_textureCoordinateShift;
			if (farTextureU > k_textureCoordinateFixedMax)
			{
				farTextureU = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureU = (farTextureU - textureU) / width;

			int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
			if (farTextureVValue > k_textureCoordinateMax)
			{
				farTextureVValue = k_textureCoordinateMax;
			}
			int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
			if (farTextureV > k_textureCoordinateFixedMax)
			{
				farTextureV = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureV = (farTextureV - textureV) / width;

			do
			{
				int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
					+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
				uint32_t texel = texData[textureIndex];
				if (texel > k_rgbMask)
				{
					uint16_t pixel = *destRow;

					int32_t channel = (pixel & k_fiveBitChannelMask) - g_colourScaleTable3[(edgeBBlue & k_upperByteMask) + (texel & k_lowerByteMask)];
					if (channel < 0)
					{
						channel = 0;
					}
					int32_t out = channel;

					channel = ((pixel >> 5) & k_fiveBitChannelMask) - g_colourScaleTable3[(edgeBGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)];
					if (channel < 0)
					{
						channel = 0;
					}
					out += channel << 5;

					channel = ((pixel >> 10) & k_fiveBitChannelMask) - g_colourScaleTable3[(edgeBRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					if (channel < 0)
					{
						channel = 0;
					}

					*destRow = (uint16_t)((channel << 10) + out);
				}

				destRow++;
				textureU += stepTextureU;
				textureV += stepTextureV;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C5490 [PROVISIONAL]
	void RasterizeSubtractiveSpan555(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		// The caller does not order the endpoints. Keep the one with the smaller x
		// in edgeB and its colours in the edgeB accumulators, so the walk below
		// always runs to increasing columns. farRed/Green/Blue hold the other end.
		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			edgeB = edgeA;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			do
			{
				uint16_t pixel = *destRow;

				int32_t channel = (pixel & 0x1f) - ((uint16_t)edgeBBlue >> 11);
				if (channel < 0)
				{
					channel = 0;
				}
				int32_t out = channel;

				channel = ((pixel >> 5) & 0x1f) - ((uint16_t)edgeBGreen >> 11);
				if (channel < 0)
				{
					channel = 0;
				}
				out += channel << 5;

				channel = ((pixel >> 10) & 0x1f) - ((uint16_t)edgeBRed >> 11);
				if (channel < 0)
				{
					channel = 0;
				}

				*destRow = (uint16_t)((channel << 10) + out);
				destRow++;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C55B0 [PROVISIONAL]
	void RasterizeSubtractiveSpan565(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		// The caller does not order the endpoints. Keep the one with the smaller x
		// in edgeB and its colours in the edgeB accumulators, so the walk below
		// always runs to increasing columns. farRed/Green/Blue hold the other end.
		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			edgeB = edgeA;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			do
			{
				uint16_t pixel = *destRow;

				int32_t channel = (pixel & 0x1f) - ((uint16_t)edgeBBlue >> 11);
				if (channel < 0)
				{
					channel = 0;
				}
				int32_t out = channel;

				channel = ((pixel >> 6) & 0x1f) - ((uint16_t)edgeBGreen >> 11);
				if (channel < 0)
				{
					channel = 0;
				}
				out += channel << 6;

				channel = ((pixel >> 11) & 0x1f) - ((uint16_t)edgeBRed >> 11);
				if (channel < 0)
				{
					channel = 0;
				}

				*destRow = (uint16_t)((channel << 11) + out);
				destRow++;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C56D0 [PROVISIONAL]
	void RasterizeTexturedSubtractiveSpan565(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale);
			if (textureU > k_textureCoordinateFixedMax)
			{
				textureU = k_textureCoordinateFixedMax;
			}
			textureU <<= k_textureCoordinateShift;

			int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
			if (textureVValue > k_textureCoordinateFixedMax)
			{
				textureVValue = k_textureCoordinateFixedMax;
			}
			int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

			int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale);
			if (farTextureU > k_textureCoordinateMax)
			{
				farTextureU = k_textureCoordinateMax;
			}
			farTextureU <<= k_textureCoordinateShift;
			if (farTextureU > k_textureCoordinateFixedMax)
			{
				farTextureU = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureU = (farTextureU - textureU) / width;

			int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
			if (farTextureVValue > k_textureCoordinateMax)
			{
				farTextureVValue = k_textureCoordinateMax;
			}
			int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
			if (farTextureV > k_textureCoordinateFixedMax)
			{
				farTextureV = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureV = (farTextureV - textureV) / width;

			do
			{
				int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
					+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
				uint32_t texel = texData[textureIndex];
				if (texel > k_rgbMask)
				{
					uint16_t pixel = *destRow;

					int32_t channel = (pixel & k_fiveBitChannelMask) - g_colourScaleTable3[(edgeBBlue & k_upperByteMask) + (texel & k_lowerByteMask)];
					if (channel < 0)
					{
						channel = 0;
					}
					int32_t out = channel;

					channel = ((pixel >> 6) & k_fiveBitChannelMask) - g_colourScaleTable3[(edgeBGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)];
					if (channel < 0)
					{
						channel = 0;
					}
					out += channel << 6;

					channel = (pixel >> 11) - g_colourScaleTable3[(edgeBRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					if (channel < 0)
					{
						channel = 0;
					}

					*destRow = (uint16_t)((channel << 11) + out);
				}

				destRow++;
				textureU += stepTextureU;
				textureV += stepTextureV;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C58E0 [PROVISIONAL]
	void RasterizeTexturedOpaqueSpan(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
		if (width == 0)
		{
			return;
		}

		int32_t farRed;
		int32_t farGreen;
		int32_t farBlue;
		if (width < 0)
		{
			farRed = edgeBRed;
			farGreen = edgeBGreen;
			farBlue = edgeBBlue;
			edgeBRed = edgeARed;
			edgeBGreen = edgeAGreen;
			edgeBBlue = edgeABlue;
			Nu3D::VertexTL* swap = edgeA;
			edgeA = edgeB;
			edgeB = swap;
			width = -width;
		}
		else
		{
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		if (width > 0)
		{
			int32_t stepRed = (farRed - edgeBRed) / width;
			int32_t stepGreen = (farGreen - edgeBGreen) / width;
			int32_t stepBlue = (farBlue - edgeBBlue) / width;

			destRow += (int32_t)edgeB->position.x;

			int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale);
			if (textureU > k_textureCoordinateFixedMax)
			{
				textureU = k_textureCoordinateFixedMax;
			}
			textureU <<= k_textureCoordinateShift;

			int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
			if (textureVValue > k_textureCoordinateFixedMax)
			{
				textureVValue = k_textureCoordinateFixedMax;
			}
			int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

			int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale);
			if (farTextureU > k_textureCoordinateMax)
			{
				farTextureU = k_textureCoordinateMax;
			}
			farTextureU <<= k_textureCoordinateShift;
			if (farTextureU > k_textureCoordinateFixedMax)
			{
				farTextureU = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureU = (farTextureU - textureU) / width;

			int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
			if (farTextureVValue > k_textureCoordinateMax)
			{
				farTextureVValue = k_textureCoordinateMax;
			}
			int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
			if (farTextureV > k_textureCoordinateFixedMax)
			{
				farTextureV = k_textureCoordinateFixedMax;
			}
			int32_t stepTextureV = (farTextureV - textureV) / width;

			do
			{
				int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
					+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
				uint32_t texel = texData[textureIndex];
				if (texel > k_rgbMask)
				{
					uint16_t out = g_colourScaleTable0[(edgeBBlue & k_upperByteMask) + (texel & k_lowerByteMask)];
					out += g_colourScaleTable0[k_colourScaleSubtableSize + (edgeBGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)];
					out += g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (edgeBRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					*destRow = out;
				}

				destRow++;
				textureU += stepTextureU;
				textureV += stepTextureV;
				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				width--;
			} while (width != 0);
		}
	}

	// FUNCTION: TOY2 0x004C5AC0 [PROVISIONAL]
	void RasterizeTexturedAlphaBlendSpan555(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
#define SPAN_UNPACK_RED(value) (((value) >> 7) & 0xf8)
#define SPAN_UNPACK_GREEN(value) (((value) >> 2) & 0xf8)
#define SPAN_PACK_RED(component) (((component) >> 1) & 0x7c00)
#define SPAN_PACK_GREEN(component) (((component) >> 6) & 0x3e0)
#include "SoftwareDeviceRasterTexturedAlphaBlendSpan.inc"
#undef SPAN_PACK_GREEN
#undef SPAN_PACK_RED
#undef SPAN_UNPACK_GREEN
#undef SPAN_UNPACK_RED
	}

	// FUNCTION: TOY2 0x004C5D80 [PROVISIONAL]
	void RasterizeAlphaBlendSpan555(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
#define SPAN_UNPACK_RED(value) (((value) >> 7) & 0xf8)
#define SPAN_UNPACK_GREEN(value) (((value) >> 2) & 0xf8)
#define SPAN_PACK_RED(component) (((component) >> 1) & 0x7c00)
#define SPAN_PACK_GREEN(component) (((component) >> 6) & 0x3e0)
#include "SoftwareDeviceRasterAlphaBlendSpan.inc"
#undef SPAN_PACK_GREEN
#undef SPAN_PACK_RED
#undef SPAN_UNPACK_GREEN
#undef SPAN_UNPACK_RED
	}

	// FUNCTION: TOY2 0x004C5F00 [PROVISIONAL]
	void RasterizeAlphaBlendSpan565(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
#define SPAN_UNPACK_RED(value) (((value) >> 8) & 0xf8)
#define SPAN_UNPACK_GREEN(value) (((value) >> 3) & 0xf8)
#define SPAN_PACK_RED(component) ((component) & 0xf800)
#define SPAN_PACK_GREEN(component) (((component) >> 5) & 0x7c0)
#include "SoftwareDeviceRasterAlphaBlendSpan.inc"
#undef SPAN_PACK_GREEN
#undef SPAN_PACK_RED
#undef SPAN_UNPACK_GREEN
#undef SPAN_UNPACK_RED
	}

	// FUNCTION: TOY2 0x004C6080 [PROVISIONAL]
	void RasterizeTexturedAlphaBlendSpan565(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue)
	{
#define SPAN_UNPACK_RED(value) (((value) >> 8) & 0xf8)
#define SPAN_UNPACK_GREEN(value) (((value) >> 3) & 0xf8)
#define SPAN_PACK_RED(component) ((component) & 0xf800)
#define SPAN_PACK_GREEN(component) (((component) >> 5) & 0x7c0)
#include "SoftwareDeviceRasterTexturedAlphaBlendSpan.inc"
#undef SPAN_PACK_GREEN
#undef SPAN_PACK_RED
#undef SPAN_UNPACK_GREEN
#undef SPAN_UNPACK_RED
	}

	// Whole-primitive rasterizers for an untextured quad.

	// FUNCTION: TOY2 0x004C80D0 [PROVISIONAL]
	void RasterizeOpaqueQuad555(RenderCommand* command)
	{
#define QUAD_PACK_RED(component) (((uint32_t)(component) & 0xf800) >> 1)
#define QUAD_PACK_GREEN(component) (((uint32_t)(component) & 0xf800) >> 6)
#include "SoftwareDeviceOpaqueQuad.inc"
#undef QUAD_PACK_GREEN
#undef QUAD_PACK_RED
	}

	// FUNCTION: TOY2 0x004C8930 [PROVISIONAL]
	void RasterizeOpaqueQuad565(RenderCommand* command)
	{
#define QUAD_PACK_RED(component) ((uint32_t)(component) & 0xf800)
#define QUAD_PACK_GREEN(component) (((uint32_t)(component) & 0xf800) >> 5)
#include "SoftwareDeviceOpaqueQuad.inc"
#undef QUAD_PACK_GREEN
#undef QUAD_PACK_RED
	}

	// The primitive walkers: a textured quad, then the triangle and quad span
	// loops that call g_spanRasterizer once per scanline.

	// FUNCTION: TOY2 0x004C6340 [PROVISIONAL]
	void RasterizeTriangleSpans(RenderCommand* command, uint32_t* texData)
	{
		Nu3D::VertexTL* vertex0 = &command->vertices[0];
		Nu3D::VertexTL* vertex1 = &command->vertices[1];
		Nu3D::VertexTL* vertex2 = &command->vertices[2];
		Nu3D::VertexTL* topCandidate = vertex0;
		int32_t topCandidateIndex = 0;
		if (vertex1->position.y <= vertex0->position.y)
		{
			topCandidate = vertex1;
			topCandidateIndex = 1;
		}

		Nu3D::VertexTL* vertexOrder[4];
		if (vertex2->position.y <= topCandidate->position.y)
		{
			vertexOrder[0] = vertex2;
			vertexOrder[1] = vertex0;
			vertexOrder[2] = vertex1;
			vertexOrder[3] = vertex2;
		}
		else if (topCandidateIndex == 0)
		{
			vertexOrder[0] = vertex0;
			vertexOrder[1] = vertex1;
			vertexOrder[2] = vertex2;
			vertexOrder[3] = vertex0;
		}
		else
		{
			vertexOrder[0] = vertex1;
			vertexOrder[1] = vertex2;
			vertexOrder[2] = vertex0;
			vertexOrder[3] = vertex1;
		}

		Nu3D::VertexTL** edgeAStart = &vertexOrder[0];
		Nu3D::VertexTL** edgeAEnd = &vertexOrder[1];
		Nu3D::VertexTL** edgeBEnd = &vertexOrder[2];
		Nu3D::VertexTL** edgeBStart = &vertexOrder[3];
#include "SoftwareDeviceSpanWalk.inc"
	}

	// This quad path samples one texel for each pixel.
	// FUNCTION: TOY2 0x004C6B80 [PROVISIONAL]
	void RasterizeTexturedQuad(RenderCommand* command, uint32_t* texData)
	{
#define QUAD_EMIT_PIXEL_PAIR()                                                                                                                                \
	int32_t textureIndex0 =                                                                                                                                   \
		((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension + ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);         \
	int32_t nextTextureU = textureU + stepTextureU;                                                                                                           \
	int32_t nextTextureV = textureV + stepTextureV;                                                                                                           \
	int32_t textureIndex1 =                                                                                                                                   \
		((nextTextureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension + ((nextTextureU >> k_textureCoordinateShift) & k_lowerByteMask); \
	uint32_t texel0 = texData[textureIndex0];                                                                                                                 \
	uint32_t texel1 = texData[textureIndex1];                                                                                                                 \
	uint16_t pixel0 = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel0 & k_lowerByteMask)]                                                          \
		+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel0 >> 8) & k_lowerByteMask)]                                  \
		+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel0 >> 16) & k_lowerByteMask)];                              \
	uint16_t pixel1 = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel1 & k_lowerByteMask)]                                                          \
		+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel1 >> 8) & k_lowerByteMask)]                                  \
		+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel1 >> 16) & k_lowerByteMask)];                              \
	*(uint32_t*)destRow = pixel0 | ((uint32_t)pixel1 << 16);
#include "SoftwareDeviceTexturedQuad.inc"
#undef QUAD_EMIT_PIXEL_PAIR
	}

	// This quad path samples one texel for each aligned pixel pair.
	// FUNCTION: TOY2 0x004C7630 [PROVISIONAL]
	void RasterizeTexturedQuadPairSample(RenderCommand* command, uint32_t* texData)
	{
#define QUAD_EMIT_PIXEL_PAIR()                                                                                                                        \
	int32_t textureIndex =                                                                                                                            \
		((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension + ((textureU >> k_textureCoordinateShift) & k_lowerByteMask); \
	uint32_t texel = texData[textureIndex];                                                                                                           \
	uint16_t pixel = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]                                                    \
		+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]                           \
		+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];                       \
	*(uint32_t*)destRow = MAKELONG(pixel, pixel);
#include "SoftwareDeviceTexturedQuad.inc"
#undef QUAD_EMIT_PIXEL_PAIR
	}

	// FUNCTION: TOY2 0x004C9190 [PROVISIONAL]
	void RasterizeQuadSpans(RenderCommand* command, uint32_t* texData)
	{
		Nu3D::VertexTL* vertex0 = &command->vertices[0];
		Nu3D::VertexTL* vertex1 = &command->vertices[1];
		Nu3D::VertexTL* vertex2 = &command->vertices[2];
		Nu3D::VertexTL* vertex3 = &command->vertices[3];
		Nu3D::VertexTL* topCandidate = vertex0;
		int32_t topCandidateIndex = 0;
		if (vertex1->position.y <= topCandidate->position.y)
		{
			topCandidate = vertex1;
			topCandidateIndex = 1;
		}
		if (vertex2->position.y <= topCandidate->position.y)
		{
			topCandidate = vertex2;
			topCandidateIndex = 2;
		}
		if (vertex3->position.y <= topCandidate->position.y)
			topCandidateIndex = 3;

		Nu3D::VertexTL* vertexOrder[5];
		switch (topCandidateIndex)
		{
			case 0:
				vertexOrder[0] = vertex0;
				vertexOrder[1] = vertex1;
				vertexOrder[2] = vertex2;
				vertexOrder[3] = vertex3;
				vertexOrder[4] = vertex0;
				break;
			case 1:
				vertexOrder[0] = vertex1;
				vertexOrder[1] = vertex2;
				vertexOrder[2] = vertex3;
				vertexOrder[3] = vertex0;
				vertexOrder[4] = vertex1;
				break;
			case 2:
				vertexOrder[0] = vertex2;
				vertexOrder[1] = vertex3;
				vertexOrder[2] = vertex0;
				vertexOrder[3] = vertex1;
				vertexOrder[4] = vertex2;
				break;
			default:
				vertexOrder[0] = vertex3;
				vertexOrder[1] = vertex0;
				vertexOrder[2] = vertex1;
				vertexOrder[3] = vertex2;
				vertexOrder[4] = vertex3;
				break;
		}

		Nu3D::VertexTL** edgeAStart = &vertexOrder[0];
		Nu3D::VertexTL** edgeAEnd = &vertexOrder[1];
		Nu3D::VertexTL** edgeBEnd = &vertexOrder[3];
		Nu3D::VertexTL** edgeBStart = &vertexOrder[4];
#include "SoftwareDeviceSpanWalk.inc"
	}

	// Chooses the span rasterizer for one queued command, then runs it.
	//
	// Three things select the variant: the surface pixel format, whether a
	// texture is bound, and the alpha mode in the render state. The command
	// already carries its own texture and render state, so the corresponding
	// parameters are dead here; FlushRenderCommands passes them because RasterizeSortedRenderCommand shares
	// the signature.
	//
	// An untextured quad and a textured quad have dedicated whole-primitive
	// rasterizers, so those two cases return early instead of selecting a span.
	// Everything else falls through to the shared tail, which walks a triangle
	// or a quad one scanline at a time.

	// FUNCTION: TOY2 0x004C9D00 [MATCHED]
	void RasterizeRenderCommand(RenderCommand* command, int32_t vertexCount, int32_t renderState, uint32_t* texData, int32_t useAlternateSpans)
	{
		int32_t pixelFormatMode = g_pixelFormatMode;
		uint32_t* commandTexData = command->texData;
		int32_t commandRenderState = command->renderState;
		int32_t commandVertexCount = command->vertexCount;

		if (pixelFormatMode == 0)
		{
			if (commandTexData == NULL)
			{
				if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
				{
					g_spanRasterizer = RasterizeAdditiveSpan555;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
				{
					g_spanRasterizer = RasterizeSubtractiveSpan555;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
				{
					g_spanAlpha = command->vertices[0].diffuse.value >> 24;
					if (g_spanAlpha == 255)
					{
						g_spanRasterizer = RasterizeOpaqueSpan555;
					}
					else
					{
						g_spanRasterizer = RasterizeAlphaBlendSpan555;
						g_spanInvAlpha = 255 - g_spanAlpha;
					}
				}
				else if (commandVertexCount == 4)
				{
					RasterizeOpaqueQuad555(command);
					return;
				}
				else
				{
					g_spanRasterizer = RasterizeOpaqueSpan555;
				}
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
			{
				g_spanRasterizer = RasterizeTexturedAdditiveSpan555;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = RasterizeTexturedSubtractiveSpan555;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = RasterizeTexturedOpaqueSpan;
				}
				else
				{
					g_spanRasterizer = RasterizeTexturedAlphaBlendSpan555;
					g_spanInvAlpha = 255 - g_spanAlpha;
				}
			}
			else
			{
				goto textured;
			}
		}
		else if (commandTexData == NULL)
		{
			if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
			{
				g_spanRasterizer = RasterizeAdditiveSpan565;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = RasterizeSubtractiveSpan565;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = RasterizeOpaqueSpan565;
				}
				else
				{
					g_spanRasterizer = RasterizeAlphaBlendSpan565;
					g_spanInvAlpha = 255 - g_spanAlpha;
				}
			}
			else if (commandVertexCount == 4)
			{
				RasterizeOpaqueQuad565(command);
				return;
			}
			else
			{
				g_spanRasterizer = RasterizeOpaqueSpan565;
			}
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
		{
			g_spanRasterizer = RasterizeTexturedAdditiveSpan565;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
		{
			g_spanRasterizer = RasterizeTexturedSubtractiveSpan565;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
		{
			g_spanAlpha = command->vertices[0].diffuse.value >> 24;
			if (g_spanAlpha == 255)
			{
				g_spanRasterizer = RasterizeTexturedOpaqueSpan;
			}
			else
			{
				g_spanRasterizer = RasterizeTexturedAlphaBlendSpan565;
				g_spanInvAlpha = 255 - g_spanAlpha;
			}
		}
		else
		{
		textured:
			if (commandVertexCount == 4)
			{
				RasterizeTexturedQuad(command, commandTexData);
				return;
			}
			g_spanRasterizer = RasterizeTexturedSpan;
		}

		if (commandVertexCount == 3)
		{
			RasterizeTriangleSpans(command, commandTexData);
			return;
		}
		RasterizeQuadSpans(command, commandTexData);
	}

	// FUNCTION: TOY2 0x004C9A50 [PROVISIONAL]
	void RasterizeSortedRenderCommand(RenderCommand* command, int32_t vertexCount, int32_t renderState, uint32_t* texData, int32_t useAlternateSpans)
	{
		int32_t pixelFormatMode = g_pixelFormatMode;
		uint32_t* commandTexData = command->texData;
		int32_t commandRenderState = command->renderState;
		int32_t commandUseAlternateSpans = command->useAlternateSpans;
		int32_t commandVertexCount = command->vertexCount;

		if (pixelFormatMode == 0)
		{
			if (commandTexData == NULL)
			{
				if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
				{
					g_spanRasterizer = RasterizeAdditiveSpan555;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
				{
					g_spanRasterizer = RasterizeSubtractiveSpan555;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
				{
					g_spanAlpha = command->vertices[0].diffuse.value >> 24;
					if (g_spanAlpha == 255)
					{
						g_spanRasterizer = RasterizeOpaqueSpan555;
					}
					else
					{
						g_spanRasterizer = RasterizeAlphaBlendSpan555;
						g_spanInvAlpha = 255 - g_spanAlpha;
					}
				}
				else if (commandVertexCount == 4)
				{
					RasterizeOpaqueQuad555(command);
					return;
				}
				else
				{
					g_spanRasterizer = RasterizeOpaqueSpan555;
				}
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
			{
				g_spanRasterizer = RasterizeTexturedAdditiveSpan555;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = RasterizeTexturedSubtractiveSpan555;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = RasterizeTexturedOpaqueSpan;
				}
				else
				{
					g_spanRasterizer = RasterizeTexturedAlphaBlendSpan555;
					g_spanInvAlpha = 255 - g_spanAlpha;
				}
			}
			else
			{
				if (commandVertexCount == 4)
				{
					if (commandUseAlternateSpans != 0)
					{
						RasterizeTexturedQuadPairSample(command, commandTexData);
					}
					else
					{
						RasterizeTexturedQuad(command, commandTexData);
					}
					return;
				}
				if (commandUseAlternateSpans != 0)
				{
					g_spanRasterizer = RasterizeTexturedSpanPairSample;
				}
				else
				{
					g_spanRasterizer = RasterizeTexturedSpan;
				}
			}
		}
		else if (commandTexData == NULL)
		{
			if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
			{
				g_spanRasterizer = RasterizeAdditiveSpan565;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = RasterizeSubtractiveSpan565;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = RasterizeOpaqueSpan565;
				}
				else
				{
					g_spanRasterizer = RasterizeAlphaBlendSpan565;
					g_spanInvAlpha = 255 - g_spanAlpha;
				}
			}
			else if (commandVertexCount == 4)
			{
				RasterizeOpaqueQuad565(command);
				return;
			}
			else
			{
				g_spanRasterizer = RasterizeOpaqueSpan565;
			}
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
		{
			g_spanRasterizer = RasterizeTexturedAdditiveSpan565;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
		{
			g_spanRasterizer = RasterizeTexturedSubtractiveSpan565;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
		{
			g_spanAlpha = command->vertices[0].diffuse.value >> 24;
			if (g_spanAlpha == 255)
			{
				g_spanRasterizer = RasterizeTexturedOpaqueSpan;
			}
			else
			{
				g_spanRasterizer = RasterizeTexturedAlphaBlendSpan565;
				g_spanInvAlpha = 255 - g_spanAlpha;
			}
		}
		else
		{
			if (commandVertexCount == 4)
			{
				if (commandUseAlternateSpans != 0)
				{
					RasterizeTexturedQuadPairSample(command, commandTexData);
				}
				else
				{
					RasterizeTexturedQuad(command, commandTexData);
				}
				return;
			}
			if (commandUseAlternateSpans != 0)
			{
				g_spanRasterizer = RasterizeTexturedSpanPairSample;
			}
			else
			{
				g_spanRasterizer = RasterizeTexturedSpan;
			}
		}

		if (commandVertexCount == 3)
		{
			RasterizeTriangleSpans(command, commandTexData);
			return;
		}
		RasterizeQuadSpans(command, commandTexData);
	}
}

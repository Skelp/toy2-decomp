#include "SoftwareRenderer.h"
#include "SoftwareRendererInternal.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "Toy2/Toy2.h"
#include "Toy2/Direct6.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include "Logger.h"
#include <math.h>

// The render command queue of the software device. QueueRenderCommand and
// QueueSortedRenderCommand project and clip a primitive and append it here; the
// flush pair hands each command to the rasterizer. The same unit keeps the
// viewport, the clip rectangle, the zoom state and the colour scale tables.
//
// Retail band 0x004BC900-0x004C1FC0.
namespace SoftwareRenderer
{
	// Channel masks that this unit spells. SoftwareRendererInternal.h is the place
	// for these, but the unnamed-constant rule of tools/decomp lint resolves a name
	// only inside the file that uses it, so a shared header would hide every bare
	// literal that still stands where one of these names belongs.
	enum
	{
		k_lowerByteMask = 0x000000ff,
	};

	// GLOBAL: TOY2 0x00E4D950
	int32_t g_softwarePrimitiveType;

	// GLOBAL: TOY2 0x009F6008
	int32_t g_reverseDepthSortEnabled;

	// GLOBAL: TOY2 0x00A4CC80
	int32_t g_sortedRenderFlushPhase;

	// FlushSortedRenderCommands resets this state after the sorted bucket walk.
	// This value is immediately before the bucket array.
	// GLOBAL: TOY2 0x00B626C0
	int32_t g_sortedRenderBucketState;

	// GLOBAL: TOY2 0x00DE20A8
	int32_t g_renderCommandCount;

	// GLOBAL: TOY2 0x00B626E0
	RenderCommand* g_sortedRenderBuckets[30000];

	// GLOBAL: TOY2 0x00B7FBA0
	uint32_t g_sortedRenderFlags;

	// GLOBAL: TOY2 0x00B7FBB0
	float g_sortDepth;

	// GLOBAL: TOY2 0x00B7FBE0
	RenderCommand g_sortedRenderCommands[15000];

	// GLOBAL: TOY2 0x00DBB094
	int32_t g_sortedRenderCommandCount;

	// GLOBAL: TOY2 0x00DBB0A0
	RenderCommand g_renderCommands[1024];

	// GLOBAL: TOY2 0x00B7FBB8
	int32_t g_sortedRenderState;

	// GLOBAL: TOY2 0x00E4D960
	int32_t g_softwareClearColor;

	// GLOBAL: TOY2 0x005088D4
	int32_t g_leftOffset = -1;

	// GLOBAL: TOY2 0x005088D8
	int32_t g_rightOffset = -1;

	// GLOBAL: TOY2 0x005088DC
	int32_t g_topOffset = -1;

	// GLOBAL: TOY2 0x005088E0
	int32_t g_bottomOffset = -1;

	// GLOBAL: TOY2 0x005088E4
	int32_t g_primaryBucketOffsets[16] = { 0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240 };

	// GLOBAL: TOY2 0x00508924
	int32_t g_secondaryBucketOffsets[16] = { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60 };

	// GLOBAL: TOY2 0x00B7FBBC
	int32_t g_clipLeft;

	// GLOBAL: TOY2 0x00DE20B8
	int32_t g_clipRight;

	// GLOBAL: TOY2 0x00DE20A4
	int32_t g_clipTop;

	// GLOBAL: TOY2 0x00DBB088
	int32_t g_clipBottom;

	// GLOBAL: TOY2 0x00508964
	int32_t g_cachedViewportTop = 0;

	// GLOBAL: TOY2 0x00508968
	int32_t g_cachedViewportBottom = 0;

	// GLOBAL: TOY2 0x0050896C
	int32_t g_cachedViewportLeft = 0;

	// GLOBAL: TOY2 0x00508970
	int32_t g_cachedViewportRight = 0;

	// GLOBAL: TOY2 0x00A4CC6C
	int32_t g_zoomLevel;

	// GLOBAL: TOY2 0x00E4D7BC
	int32_t g_zoomExtentV;

	// GLOBAL: TOY2 0x00B7FBB4
	int32_t g_zoomExtentH;

	// Float render-transform globals derived from the integer zoom/offset state
	// by CommitZoom. The polygon pipeline applies them as a screen-space
	// mapping (x' = spanScaleV*x + topOffsetF, y' = spanScaleH*y + leftOffsetF)
	// with extent/dimension ratios as projection scales. The engine cross-wires
	// the axes (the vertical span feeds the x scale); reproduced faithfully.
	// GLOBAL: TOY2 0x00DBB084
	float g_topOffsetF;

	// GLOBAL: TOY2 0x00DE20B0
	float g_leftOffsetF;

	// GLOBAL: TOY2 0x00DFF580
	float g_spanScaleV;

	// GLOBAL: TOY2 0x00DFF5A0
	Nu3D::VertexTL g_processedVertices[10000];

	// GLOBAL: TOY2 0x00B626BC
	float g_spanScaleH;

	// The span rasterizer that RasterizeSortedRenderCommand and RasterizeRenderCommand select from the render
	// state. RasterizeTriangleSpans and RasterizeQuadSpans call it once per scanline. The
	// selection is a plain global, so the executors take no function argument.

	// GLOBAL: TOY2 0x00E4D7A8
	float g_zoomScaleV;

	// GLOBAL: TOY2 0x00B7FBA4
	float g_zoomScaleH;

	// Screen dimensions (set at boot by InitialisePrimarySurface). Used as the
	// integer divisors for the zoom scales and as clipBottom+1 / clipRight+1.
	// GLOBAL: TOY2 0x00DE20B4
	int32_t g_screenDimV;

	// GLOBAL: TOY2 0x00E4D7B0
	int32_t g_screenDimH;

	// GLOBAL: TOY2 0x00B7FBA8
	float g_primaryRenderDistance = 0.0f;

	// GLOBAL: TOY2 0x00DE20A0
	float g_secondaryRenderDistance = 0.0f;

	// .rdata span-scale constants: 1/320 and 1/220.
	// GLOBAL: TOY2 0x004DDB48
	extern const double k_vSpanScale = 1.0 / 320.0;

	// GLOBAL: TOY2 0x004DDB40
	extern const double k_hSpanScale = 1.0 / 220.0;

	// GLOBAL: TOY2 0x004DDAE0
	extern const double k_viewportScaleV = 1.7;

	// GLOBAL: TOY2 0x004DDAD8
	extern const double k_viewportScaleH = 1.9;

	// GLOBAL: TOY2 0x004DDAD4
	extern const float k_reverseDepthSortScale = -25000.0f;

	// Locked primary surface pointer and pitch, captured by
	// InitialisePrimarySurface and read by PresentFrame when blitting.
	// GLOBAL: TOY2 0x00E4D7B4
	LPVOID g_primarySurfacePtr;

	// GLOBAL: TOY2 0x00E4D7A0
	int32_t g_primarySurfacePitch;

	// Pixel format mode detected from the primary surface:
	// 0 = 16-bit 555, 1 = 16-bit 565, 2 = 24/32-bit BGR, 3 = 24/32-bit RGB.
	// Read by InitialiseColourScaleTables and the colour-conversion helpers.
	// GLOBAL: TOY2 0x00DE20AC
	int32_t g_pixelFormatMode;

	// Surface-sized working buffer (malloc'd as height*pitch) reallocated by
	// InitialisePrimarySurface when the surface changes; read by PresentFrame.
	// GLOBAL: TOY2 0x00A4CC84
	void* g_backBuffer;

	// Colour-scale sub-table pointers. Each table is a 256x256 uint16 LUT
	// laid out as table[scale*256 + colour], mapping a scaled colour component
	// (min(colour*scale, 0xffff)) into its 16-bit position for the active pixel
	// format. Built by InitialiseColourScaleTables from g_pixelFormatMode and
	// read by the colour-conversion render helpers.
	// GLOBAL: TOY2 0x00E4D7AC
	uint16_t* g_colourScaleTable0;

	// GLOBAL: TOY2 0x00DBB08C
	uint16_t* g_colourScaleTable1;

	// GLOBAL: TOY2 0x00B7FBC0
	uint16_t* g_colourScaleTable2;

	// GLOBAL: TOY2 0x00E4D7B8
	uint16_t* g_colourScaleTable3;

	// Colour-scale lookup tables, allocated by InitialiseColourScaleTables and
	// freed (then rebuilt) by InitialisePrimarySurface on surface change.
	// GLOBAL: TOY2 0x00A4CC78
	void* g_colourScaleTables;

	// GLOBAL: TOY2 0x00DBB080
	float g_cameraNearZ;

	// GLOBAL: TOY2 0x00DBB090
	float g_cameraFarZ;

	// FUNCTION: TOY2 0x004C1C20 [MATCHED]
	void SetCameraNearFarZ(float nearZ, float farZ)
	{
		g_cameraNearZ = nearZ;
		g_cameraFarZ = farZ;
	}

#define TOY2_BUILD_COLOUR_SCALE_TABLE(tableGlobal, shift, mask)           \
	do                                                                    \
	{                                                                     \
		int idx = 0;                                                      \
		for (int s = 0; s < 256; s++)                                     \
		{                                                                 \
			uint32_t acc = 0;                                             \
			for (int c = 0; c < 256; c++)                                 \
			{                                                             \
				uint32_t v = acc;                                         \
				if (acc > 0xffff)                                         \
					v = 0xffff;                                           \
				tableGlobal[idx++] = (uint16_t)((v >> (shift)) & (mask)); \
				acc += s;                                                 \
			}                                                             \
		}                                                                 \
	} while (0)

	// FUNCTION: TOY2 0x004C1C40 [PROVISIONAL]
	void InitialiseColourScaleTables()
	{
		g_colourScaleTables = malloc(0x80000);
		g_colourScaleTable0 = (uint16_t*)g_colourScaleTables;
		g_colourScaleTable1 = g_colourScaleTable0 + 0x10000;
		g_colourScaleTable2 = g_colourScaleTable1 + 0x10000;
		g_colourScaleTable3 = g_colourScaleTable2 + 0x10000;

		if (g_pixelFormatMode == 0) // 16-bit 555
		{
			TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable0, 11, 0x1f);
			TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable1, 6, 0x3e0);
			TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable2, 1, 0x7c00);
			TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable3, 11, 0x1f);
			return;
		}
		// 16-bit 565 (24/32-bit BGR/RGB modes reuse these tables).
		TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable0, 11, 0x1f);
		TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable1, 5, 0x7c0);
		TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable2, 0, 0xf800);
		TOY2_BUILD_COLOUR_SCALE_TABLE(g_colourScaleTable3, 11, 0x1f);
	}

#undef TOY2_BUILD_COLOUR_SCALE_TABLE

	// FUNCTION: TOY2 0x004BCE00 [PROVISIONAL]
	void InitialisePrimarySurface()
	{
		DDSURFACEDESC2 surfaceDesc;
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		DrawingDevice::LockPrimarySurface(&surfaceDesc);

		g_primarySurfacePtr = surfaceDesc.lpSurface;
		int32_t clipRight = surfaceDesc.dwHeight - 1;
		int32_t clipBottom = surfaceDesc.dwWidth - 1;
		int32_t clipTop = 0;
		g_primarySurfacePitch = surfaceDesc.lPitch;
		g_screenDimV = surfaceDesc.dwWidth;
		g_screenDimH = surfaceDesc.dwHeight;

		// 320x200 mode: height==200 (clipRight = height-1 == 0xc7). Override the
		// width-axis clip to a centered 14..306 window. (g_zoomExtentV store is
		// overwritten below; reproduced faithfully from retail.)
		if (clipRight == 0xc7)
		{
			clipTop = 0xe;
			clipBottom = 0x132;
			g_zoomExtentV = 0x124;
		}

		if (surfaceDesc.ddpfPixelFormat.dwRGBBitCount == 0x10)
		{
			g_pixelFormatMode = (surfaceDesc.ddpfPixelFormat.dwGBitMask != 0x3e0);
		}
		else if (surfaceDesc.ddpfPixelFormat.dwBBitMask == 0xff)
		{
			g_pixelFormatMode = 2;
		}
		else if (surfaceDesc.ddpfPixelFormat.dwRBitMask == 0xff)
		{
			g_pixelFormatMode = 3;
		}

		DrawingDevice::UnlockPrimarySurface();

		g_clipLeft = 0;
		g_clipRight = clipRight;
		g_clipTop = clipTop;
		g_clipBottom = clipBottom;
		g_leftOffset = 0;
		g_topOffset = clipTop;
		g_rightOffset = clipRight;
		g_bottomOffset = clipBottom;
		g_zoomExtentV = surfaceDesc.dwWidth;
		g_zoomExtentH = surfaceDesc.dwHeight;

		CommitZoom();

		g_zoomLevel = 0;
		if (g_backBuffer)
		{
			free(g_backBuffer);
			g_backBuffer = 0;
		}
		if (g_colourScaleTables)
		{
			free(g_colourScaleTables);
		}
		g_backBuffer = malloc(surfaceDesc.dwHeight * surfaceDesc.lPitch);
		InitialiseColourScaleTables();
	}

	// FUNCTION: TOY2 0x004C1E60 [MATCHED]
	void InitialisePrimarySurface_T() { InitialisePrimarySurface(); }

	// FUNCTION: TOY2 0x004C1E70 [MATCHED]
	void CommitZoom()
	{
		g_topOffsetF = (float)g_topOffset;
		g_leftOffsetF = (float)g_leftOffset;
		g_spanScaleV = (float)((g_bottomOffset - g_topOffset) + 1) * k_vSpanScale;
		g_spanScaleH = (float)((g_rightOffset - g_leftOffset) + 1) * k_hSpanScale;
		g_zoomScaleV = (float)g_zoomExtentV / g_screenDimV;
		g_zoomScaleH = (float)g_zoomExtentH / g_screenDimH;
	}

	// FUNCTION: TOY2 0x004C1FC0 [MATCHED]
	void ZoomOut()
	{
		if (g_zoomLevel >= 10)
		{
			g_zoomLevel = 10;
			return;
		}
		g_zoomLevel++;
		g_zoomExtentV -= 0x10;
		g_zoomExtentH -= 0xc;
		g_topOffset += 8;
		g_bottomOffset -= 8;
		g_leftOffset += 6;
		g_rightOffset -= 6;
		g_clipTop += 8;
		g_clipBottom -= 8;
		g_clipLeft += 6;
		g_clipRight -= 6;
		CommitZoom();
	}

	// FUNCTION: TOY2 0x004C1F00 [MATCHED]
	void ZoomIn()
	{
		if (g_zoomLevel <= 0)
		{
			g_zoomLevel = 0;
			return;
		}
		g_zoomLevel--;
		g_zoomExtentV += 0x10;
		g_zoomExtentH += 0xc;
		g_topOffset -= 8;
		g_bottomOffset += 8;
		g_leftOffset -= 6;
		g_rightOffset += 6;
		g_clipTop -= 8;
		g_clipBottom += 8;
		g_clipLeft -= 6;
		g_clipRight += 6;
		CommitZoom();
	}

	// FUNCTION: TOY2 0x004C17B0 [PROVISIONAL]
	void PresentFrame()
	{
		DDSURFACEDESC2 surfaceDesc;
		uint8_t* primaryRow;
		uint8_t* backRow;
		uint32_t* primaryPixel;
		uint32_t* backPixel;
		uint32_t dwordCount;
		int32_t rowCount;
		int32_t rowOffset;

		surfaceDesc.dwSize = sizeof(surfaceDesc);
		if (g_backBuffer == NULL)
		{
			return;
		}

		DrawingDevice::LockPrimarySurface(&surfaceDesc);

		backRow = static_cast<uint8_t*>(g_backBuffer);
		dwordCount = static_cast<uint32_t>(g_screenDimV) >> 1;
		backPixel = reinterpret_cast<uint32_t*>(backRow);
		do
		{
			*backPixel++ = 0;
			dwordCount--;
		} while (dwordCount != 0);

		backRow = static_cast<uint8_t*>(g_backBuffer);
		rowCount = g_screenDimH - 1;
		do
		{
			reinterpret_cast<uint16_t*>(backRow)[0] = 0;
			reinterpret_cast<uint16_t*>(backRow)[g_screenDimV - 1] = 0;
			backRow += g_primarySurfacePitch;
			rowCount--;
		} while (rowCount != 0);

		dwordCount = static_cast<uint32_t>(g_screenDimV) >> 1;
		backPixel = reinterpret_cast<uint32_t*>(backRow);
		do
		{
			*backPixel++ = 0;
			dwordCount--;
		} while (dwordCount != 0);

		dwordCount = static_cast<uint32_t>(g_bottomOffset - g_topOffset + 1) >> 1;
		rowOffset = g_primarySurfacePitch * g_leftOffset + g_topOffset * sizeof(uint16_t);
		rowCount = g_rightOffset - g_leftOffset + 1;
		primaryRow = static_cast<uint8_t*>(g_primarySurfacePtr) + rowOffset;
		backRow = static_cast<uint8_t*>(g_backBuffer) + rowOffset;
		do
		{
			primaryPixel = reinterpret_cast<uint32_t*>(primaryRow);
			backPixel = reinterpret_cast<uint32_t*>(backRow);
			uint32_t remaining = dwordCount;
			do
			{
				*primaryPixel++ = *backPixel;
				*backPixel++ = g_softwareClearColor;
				remaining--;
			} while (remaining != 0);
			primaryRow += g_primarySurfacePitch;
			backRow += g_primarySurfacePitch;
			rowCount--;
		} while (rowCount != 0);

		primaryRow = static_cast<uint8_t*>(g_primarySurfacePtr);
		backRow = static_cast<uint8_t*>(g_backBuffer);
		rowCount = g_leftOffset;
		if (rowCount != 0)
		{
			do
			{
				primaryPixel = reinterpret_cast<uint32_t*>(primaryRow);
				backPixel = reinterpret_cast<uint32_t*>(backRow);
				dwordCount = static_cast<uint32_t>(g_screenDimV) >> 1;
				do
				{
					*primaryPixel++ = *backPixel;
					*backPixel++ = 0x00100010;
					dwordCount--;
				} while (dwordCount != 0);
				primaryRow += g_primarySurfacePitch;
				backRow += g_primarySurfacePitch;
				rowCount--;
			} while (rowCount != 0);

			rowOffset = (g_rightOffset + 1) * g_primarySurfacePitch;
			primaryRow = static_cast<uint8_t*>(g_primarySurfacePtr) + rowOffset;
			backRow = static_cast<uint8_t*>(g_backBuffer) + rowOffset;
			rowCount = g_leftOffset;
			do
			{
				primaryPixel = reinterpret_cast<uint32_t*>(primaryRow);
				backPixel = reinterpret_cast<uint32_t*>(backRow);
				dwordCount = static_cast<uint32_t>(g_screenDimV) >> 1;
				do
				{
					*primaryPixel++ = *backPixel;
					*backPixel++ = 0x00100010;
					dwordCount--;
				} while (dwordCount != 0);
				primaryRow += g_primarySurfacePitch;
				backRow += g_primarySurfacePitch;
				rowCount--;
			} while (rowCount != 0);
		}

		rowOffset = g_primarySurfacePitch * g_leftOffset;
		primaryRow = static_cast<uint8_t*>(g_primarySurfacePtr) + rowOffset;
		backRow = static_cast<uint8_t*>(g_backBuffer) + rowOffset;
		if (g_topOffset != 0)
		{
			uint32_t marginDwordCount = static_cast<uint32_t>(g_topOffset) >> 1;
			int32_t rightMarginOffset = g_bottomOffset * sizeof(uint16_t);
			rowCount = g_rightOffset - g_leftOffset + 1;
			do
			{
				primaryPixel = reinterpret_cast<uint32_t*>(primaryRow);
				backPixel = reinterpret_cast<uint32_t*>(backRow);
				uint32_t* primaryRight = reinterpret_cast<uint32_t*>(primaryRow + rightMarginOffset);
				uint32_t* backRight = reinterpret_cast<uint32_t*>(backRow + rightMarginOffset);
				uint32_t marginDwords = marginDwordCount;
				do
				{
					*primaryPixel = *backPixel;
					*backPixel = 0x00100010;
					*primaryRight++ = *backRight;
					*backRight++ = 0x00100010;
					primaryPixel++;
					backPixel++;
					marginDwords--;
				} while (marginDwords != 0);
				primaryRow += g_primarySurfacePitch;
				backRow += g_primarySurfacePitch;
				rowCount--;
			} while (rowCount != 0);
		}

		DrawingDevice::UnlockPrimarySurface();
	}

	// FUNCTION: TOY2 0x004BCAD0 [MATCHED]
	void QueueRenderCommand(Nu3D::VertexTL* vertices[4], int32_t vertexCount, uint32_t* texData, int32_t renderState)
	{
		if (g_renderCommandCount < 0x400)
		{
			RenderCommand* command = &g_renderCommands[g_renderCommandCount];
			g_renderCommandCount++;
			Nu3D::VertexTL** source = vertices;
			Nu3D::VertexTL* dest = command->vertices;
			*dest = **source;
			source++;
			dest++;
			*dest = **source;
			source++;
			dest++;
			*dest = **source;
			if (vertexCount == 4)
			{
				source++;
				dest++;
				*dest = **source;
			}
			command->vertexCount = vertexCount;
			command->texData = texData;
			command->renderState = renderState;
		}
	}

	// FUNCTION: TOY2 0x004BCBE0 [EFFECTIVE]
	void FlushRenderCommands()
	{
		for (int i = 0; i < g_renderCommandCount; i++)
		{
			RenderCommand& command = g_renderCommands[i];
			RasterizeRenderCommand(&command, command.vertexCount, command.renderState, command.texData, command.useAlternateSpans);
		}
		g_sortedRenderFlushPhase = 0;
	}

	// The span rasterizers that RasterizeSortedRenderCommand and RasterizeRenderCommand select. Each writes one
	// scanline using the globals the selector published.

	// FUNCTION: TOY2 0x004BC900 [MATCHED]
	void UnpackColourToFloats(uint32_t colour, float* red, float* green, float* blue, uint32_t* alphaMask)
	{
		*red = ((colour >> 16) & k_lowerByteMask) * (1.0 / 255.0);
		*green = ((colour >> 8) & k_lowerByteMask) * (1.0 / 255.0);
		*blue = (colour & k_lowerByteMask) * (1.0 / 255.0);
		*alphaMask = colour & 0xff000000;
	}

	// FUNCTION: TOY2 0x004BC980 [PROVISIONAL]
	void QueueSortedRenderCommand(Nu3D::VertexTL* vertices[4], int32_t vertexCount, uint32_t* texData, int32_t renderState, int32_t bucketGroup)
	{
		if (g_sortedRenderCommandCount >= 15000)
		{
			return;
		}

		float sortDepth = g_sortDepth;
		int32_t bucket;
		if (g_reverseDepthSortEnabled == 1)
		{
			int32_t depth = 2 - (int32_t)(sortDepth * k_reverseDepthSortScale);
			if ((g_sortedRenderFlags & 1) == 0)
			{
				int32_t offset = g_primaryBucketOffsets[bucketGroup];
				depth += offset;
				bucket = depth + offset * 2;
			}
			else
			{
				int32_t offset = g_secondaryBucketOffsets[bucketGroup];
				depth += offset;
				bucket = depth + offset * 2 + 1100;
			}
		}
		else
		{
			int32_t depth = (int32_t)sortDepth;
			if ((g_sortedRenderFlags & 1) == 0)
			{
				int32_t offset = g_primaryBucketOffsets[bucketGroup];
				depth += offset;
				bucket = depth + offset * 2;
			}
			else
			{
				int32_t offset = g_secondaryBucketOffsets[bucketGroup];
				depth += offset;
				bucket = depth + offset * 2 + 1100;
			}
		}

		if (bucket >= 30000)
		{
			return;
		}

		RenderCommand* command = &g_sortedRenderCommands[g_sortedRenderCommandCount];
		g_sortedRenderCommandCount++;
		Nu3D::VertexTL** source = vertices;
		Nu3D::VertexTL* dest = command->vertices;
		*dest = **source;
		source++;
		dest++;
		*dest = **source;
		source++;
		dest++;
		*dest = **source;
		if (vertexCount == 4)
		{
			source++;
			dest++;
			*dest = **source;
		}
		command->vertexCount = vertexCount;
		command->texData = texData;
		command->renderState = renderState;
		command->next = g_sortedRenderBuckets[bucket];
		g_sortedRenderBuckets[bucket] = command;
		command->useAlternateSpans = g_sortedRenderFlags;
	}

	// FUNCTION: TOY2 0x004BCF60 [MATCHED]
	int32_t IsClockwiseWinding(const Nu3D::VertexTL* first, const Nu3D::VertexTL* second, const Nu3D::VertexTL* third)
	{
		return (first->position.x - second->position.x) * (third->position.y - second->position.y)
			- (third->position.x - second->position.x) * (first->position.y - second->position.y)
			< 0.0;
	}

	// FUNCTION: TOY2 0x004BCFA0 [MATCHED]
	void ProjectVertex(Nu3D::VertexTL* vertex)
	{
		float halfScreenV = (float)(g_screenDimV * 0.5);
		float halfScreenH = (float)(g_screenDimH * 0.5);
		vertex->specular.value = (uint32_t)(int32_t)vertex->position.x;
		vertex->rhw = vertex->position.y;
		if (vertex->position.z > 0.0)
		{
			float centerV = (float)(g_screenDimV / 2);
			vertex->position.x = centerV + halfScreenV * vertex->position.x / (vertex->position.z + 1.0f) * g_zoomScaleV * k_viewportScaleV;
			float centerH = (float)(g_screenDimH / 2);
			vertex->position.y = centerH + halfScreenH * vertex->position.y / (vertex->position.z + 1.0f) * g_zoomScaleH * k_viewportScaleH;
		}
	}

	// FUNCTION: TOY2 0x004C0100 [PROVISIONAL]
	int32_t IsPrimitiveOutsideViewport(Nu3D::VertexTL* vertices[4], int32_t vertexCount)
	{
		Nu3D::VertexTL* vertex0 = vertices[0];
		Nu3D::VertexTL* vertex1 = vertices[1];
		Nu3D::VertexTL* vertex2 = vertices[2];
		Nu3D::VertexTL* vertex3 = vertices[3];

		if (vertexCount == 4)
		{
			if ((vertex0->position.x < (float)g_clipTop && vertex1->position.x < (float)g_clipTop && vertex2->position.x < (float)g_clipTop
					&& vertex3->position.x < (float)g_clipTop)
				|| (vertex0->position.x > (float)g_clipBottom && vertex1->position.x > (float)g_clipBottom && vertex2->position.x > (float)g_clipBottom
					&& vertex3->position.x > (float)g_clipBottom)
				|| (vertex0->position.y < (float)g_clipLeft && vertex1->position.y < (float)g_clipLeft && vertex2->position.y < (float)g_clipLeft
					&& vertex3->position.y < (float)g_clipLeft)
				|| (vertex0->position.y > (float)g_clipRight && vertex1->position.y > (float)g_clipRight && vertex2->position.y > (float)g_clipRight
					&& vertex3->position.y > (float)g_clipRight))
				return 1;
		}
		else
		{
			if (vertexCount != 3)
				return 1;

			if ((vertex0->position.x < (float)g_clipTop && vertex1->position.x < (float)g_clipTop && vertex2->position.x < (float)g_clipTop)
				|| (vertex0->position.x > (float)g_clipBottom && vertex1->position.x > (float)g_clipBottom && vertex2->position.x > (float)g_clipBottom)
				|| (vertex0->position.y < (float)g_clipLeft && vertex1->position.y < (float)g_clipLeft && vertex2->position.y < (float)g_clipLeft)
				|| (vertex0->position.y > (float)g_clipRight && vertex1->position.y > (float)g_clipRight && vertex2->position.y > (float)g_clipRight))
				return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004BCC40 [MATCHED]
	void ResetRenderCommands()
	{
		if (g_sortedRenderFlushPhase == 0)
		{
			for (int i = 0; i < 30000; i++)
			{
				g_sortedRenderBuckets[i] = NULL;
			}
			g_sortedRenderCommandCount = 0;
			g_renderCommandCount = 0;
		}
	}

	// FUNCTION: TOY2 0x004C1790 [MATCHED]
	void FlushSortedRenderCommandsAndReset()
	{
		FlushSortedRenderCommands();
		ResetRenderCommands();
	}

	// FUNCTION: TOY2 0x004C17A0 [MATCHED]
	void FlushRenderCommandsAndReset()
	{
		FlushRenderCommands();
		ResetRenderCommands();
	}

	// FUNCTION: TOY2 0x004BCB60 [MATCHED]
	void FlushSortedRenderCommands()
	{
		if (g_sortedRenderFlushPhase == 1)
		{
			for (int i = 29999; i >= 0; i--)
			{
				RenderCommand* command = g_sortedRenderBuckets[i];
				while (command != NULL)
				{
					RasterizeSortedRenderCommand(command, command->vertexCount, command->renderState, command->texData, command->useAlternateSpans);
					command = command->next;
				}
			}
		}
		g_sortedRenderState = 0;
		g_sortedRenderBucketState = 0;
		g_sortedRenderFlushPhase++;
	}

	// ViewportRect stores the source left edge in bottom and its bottom edge in left.
	// FUNCTION: TOY2 0x004BCC70 [PROVISIONAL]
	void UpdateViewportClipBounds(int32_t top, int32_t bottom, int32_t left, int32_t right)
	{
		int32_t vCenter;
		int32_t hCenter;
		float scaleV;
		float scaleH;

		if (top != g_cachedViewportTop || left != g_cachedViewportLeft)
		{
			vCenter = g_screenDimV >> 1;
			scaleV = (float)((g_bottomOffset - g_topOffset + 1) * k_viewportScaleV / g_screenDimV);
		}
		else
		{
			vCenter = right;
		}
		if (bottom != g_cachedViewportBottom || right != g_cachedViewportRight)
		{
			hCenter = g_screenDimH >> 1;
			scaleH = (float)((g_rightOffset - g_leftOffset + 1) * k_viewportScaleH / g_screenDimH);
		}
		else
		{
			hCenter = right;
		}
		if (top != g_cachedViewportTop)
		{
			g_cachedViewportTop = top;
			int32_t clipTop = (int32_t)((top - vCenter) * scaleV) + vCenter - 4;
			if (clipTop < g_topOffset)
			{
				clipTop = g_topOffset;
			}
			g_clipTop = clipTop;
		}
		if (left != g_cachedViewportLeft)
		{
			g_cachedViewportLeft = left;
			int32_t clipBottom = (int32_t)((left - vCenter) * scaleV) + vCenter + 4;
			if (clipBottom > g_bottomOffset)
			{
				clipBottom = g_bottomOffset;
			}
			g_clipBottom = clipBottom;
		}
		if (bottom != g_cachedViewportBottom)
		{
			g_cachedViewportBottom = bottom;
			int32_t clipLeft = (int32_t)((bottom - hCenter) * scaleH) + hCenter - 6;
			if (clipLeft < g_leftOffset)
			{
				clipLeft = g_leftOffset;
			}
			g_clipLeft = clipLeft;
		}
		if (right != g_cachedViewportRight)
		{
			g_cachedViewportRight = right;
			int32_t clipRight = (int32_t)((right - hCenter) * scaleH) + hCenter + 0x20;
			if (clipRight > g_rightOffset)
			{
				clipRight = g_rightOffset;
			}
			g_clipRight = clipRight;
		}
	}

	// FUNCTION: TOY2 0x004C14A0 [PROVISIONAL]
	void ProcessIndexedTriangleList(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags)
	{
		TextureData tex;
		int32_t noTexture = GetCurrentTextureData(&tex);
		LPWORD indices = lpwIndices;
		Nu3D::VertexTL* vertexBase = static_cast<Nu3D::VertexTL*>(lpvVertices);
		uint32_t* maskedTexData = (noTexture != 0) ? NULL : tex.texData;
		DWORD remaining = dwIndexCount / 3;
		if (remaining != 0)
		{
			do
			{
				Nu3D::VertexTL* vertices[3];
				vertices[0] = &vertexBase[indices[0]];
				vertices[1] = &vertexBase[indices[1]];
				vertices[2] = &vertexBase[indices[2]];
				SubmitDepthCheckedPrimitive(vertices, 3, maskedTexData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
				indices += 3;
				remaining--;
			} while (remaining != 0);
		}
	}

	// STUB: TOY2 0x004C0320
	void SubmitDepthCheckedPrimitive(
		Nu3D::VertexTL* vertices[3], int32_t vertexCount, uint32_t* texData, int32_t renderState, int32_t primitiveType, DWORD drawFlags)
	{}

	// FUNCTION: TOY2 0x004C1540 [PROVISIONAL]
	void ProcessIndexedTriangleStrip(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags)
	{
		TextureData texture;
		uint32_t* texData = GetCurrentTextureData(&texture) != 0 ? NULL : texture.texData;
		Nu3D::VertexTL* vertexBase = (Nu3D::VertexTL*)lpvVertices;
		Nu3D::VertexTL* vertices[4];

		if (((dwIndexCount - 2) & ~1u) != 0)
		{
			uint16_t firstIndex = *lpwIndices++;
			uint16_t secondIndex = *lpwIndices++;
			uint16_t previousIndex0 = *lpwIndices++;
			uint16_t previousIndex1 = *lpwIndices++;
			vertices[0] = &vertexBase[previousIndex0];
			vertices[3] = &vertexBase[firstIndex];
			vertices[2] = &vertexBase[secondIndex];
			vertices[1] = &vertexBase[previousIndex1];
			SubmitDepthCheckedPrimitive(vertices, 4, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);

			for (DWORD remaining = (dwIndexCount - 4) / 2; remaining != 0; remaining--)
			{
				uint16_t oldIndex0 = previousIndex0;
				previousIndex0 = *lpwIndices++;
				uint16_t oldIndex1 = previousIndex1;
				previousIndex1 = *lpwIndices++;
				vertices[0] = &vertexBase[previousIndex0];
				vertices[3] = &vertexBase[oldIndex0];
				vertices[2] = &vertexBase[oldIndex1];
				vertices[1] = &vertexBase[previousIndex1];
				SubmitDepthCheckedPrimitive(vertices, 4, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
			}

			if ((dwIndexCount & 1) != 0)
			{
				vertices[0] = &vertexBase[*lpwIndices];
				vertices[1] = &vertexBase[previousIndex1];
				vertices[2] = &vertexBase[previousIndex0];
				SubmitDepthCheckedPrimitive(vertices, 3, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
			}
		}
		else
		{
			vertices[0] = &vertexBase[lpwIndices[0]];
			vertices[1] = &vertexBase[lpwIndices[1]];
			vertices[2] = &vertexBase[lpwIndices[2]];
			SubmitDepthCheckedPrimitive(vertices, 3, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
		}
	}

	// FUNCTION: TOY2 0x004C1720 [MATCHED]
	void ProcessIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, LPVOID vertices, LPWORD indices, DWORD indexCount, DWORD flags)
	{
		GetRenderDistances(&g_primaryRenderDistance, &g_secondaryRenderDistance);
		switch (primitiveType)
		{
			case D3DPT_TRIANGLELIST:
				ProcessIndexedTriangleList(vertices, indices, indexCount, flags);
				break;
			case D3DPT_TRIANGLESTRIP:
				ProcessIndexedTriangleStrip(vertices, indices, indexCount, flags);
				break;
		}
		g_reverseDepthSortEnabled = 0;
	}

}

namespace SoftwareDevice
{
	// FUNCTION: TOY2 0x004C19E0 [PROVISIONAL]
	HRESULT ProcessVerticesOnBuffer(LPDIRECT3DVERTEXBUFFER destBuffer,
		DWORD dwVertexOp,
		DWORD dwDestIndex,
		DWORD dwCount,
		LPDIRECT3DVERTEXBUFFER srcBuffer,
		DWORD dwSrcIndex,
		DWORD dwFlags)
	{
		float halfScreenV = (float)(SoftwareRenderer::g_screenDimV * 0.5);
		float halfScreenH = (float)(SoftwareRenderer::g_screenDimH * 0.5);
		SoftwareRenderer::GetRenderDistances(&SoftwareRenderer::g_primaryRenderDistance, &SoftwareRenderer::g_secondaryRenderDistance);

		Nu3D::Vertex* sourceVertices;
		DWORD bufferSize;
		DrawingAPI::LockVertexBuffer(srcBuffer, 0x11, (LPVOID*)&sourceVertices, &bufferSize);
		sourceVertices += dwSrcIndex;

		D3DMATRIX worldViewMatrix;
		Nu3D::Math::MultiplyMatrix3x4(&worldViewMatrix, DrawingDevice::g_currentWorldTransform, DrawingDevice::g_currentViewTransform);

		Nu3D::VertexTL* processedVertex = SoftwareRenderer::g_processedVertices;
		if (dwCount > 0)
		{
			DWORD remaining = dwCount;
			do
			{
				Vector3F sourcePosition = sourceVertices->position;
				Vector3F transformedPosition;
				Nu3D::Math::TransformPointByMatrix(&transformedPosition, &sourcePosition, &worldViewMatrix);

				if (transformedPosition.z > 0.0)
				{
					processedVertex->position.x = (float)(SoftwareRenderer::g_screenDimV / 2)
						+ transformedPosition.x * halfScreenV / (transformedPosition.z + 1.0f) * SoftwareRenderer::g_zoomScaleV
							* (float)SoftwareRenderer::k_viewportScaleV;
					processedVertex->position.y = (float)(SoftwareRenderer::g_screenDimH / 2)
						- transformedPosition.y * halfScreenH / (transformedPosition.z + 1.0f) * SoftwareRenderer::g_zoomScaleH
							* (float)SoftwareRenderer::k_viewportScaleH;
				}
				else
				{
					processedVertex->position.x = transformedPosition.x;
					processedVertex->position.y = transformedPosition.y;
				}

				processedVertex->position.z = transformedPosition.z;
				processedVertex->specular.value = (uint32_t)(int32_t)halfScreenH;
				processedVertex->rhw = -transformedPosition.y;
				processedVertex->diffuse = sourceVertices->diffuse;
				processedVertex->uv = sourceVertices->coords;

				sourceVertices++;
				processedVertex++;
				remaining--;
			} while (remaining != 0);
		}

		DrawingAPI::UnlockVertexBuffer(srcBuffer);

		DrawingAPI::LockVertexBuffer(destBuffer, 0x21, (LPVOID*)&processedVertex, &bufferSize);
		processedVertex += dwDestIndex;
		memcpy(processedVertex, SoftwareRenderer::g_processedVertices, dwCount * sizeof(Nu3D::VertexTL));
		DrawingAPI::UnlockVertexBuffer(destBuffer);
		return 0;
	}
}

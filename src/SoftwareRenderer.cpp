#include "SoftwareRenderer.h"
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
	// GLOBAL: TOY2 0x004F7400
	PointI g_backdropScrollOverride = { -32768, -32768 };

	// GLOBAL: TOY2 0x004F73A8
	BackdropDimensions g_backdropDimensions = { 0, 0, 0 };

	// GLOBAL: TOY2 0x004F73D4
	BackdropDimensions g_staticBackdropDimensions = { 0, 0, 0 };

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

	struct BackdropBandColour
	{
		int16_t red;
		int16_t green;
		int16_t blue;
		int16_t reserved;
	};

	STATIC_ASSERT(sizeof(BackdropDimensions) == 0xC);
	STATIC_ASSERT(sizeof(BackdropBandColour) == 8);

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

	// GLOBAL: TOY2 0x00830C60
	int32_t g_backBufferClearComplete;

	// GLOBAL: TOY2 0x00559C40
	int32_t g_skipOddSoftwareFrames;

	// GLOBAL: TOY2 0x00839278
	int32_t g_displayMaxX;

	// Active base of the 4096 software-render depth buckets.
	// GLOBAL: TOY2 0x0087E50C
	SoftwareRenderItem* g_softwareRenderBucketStorage[4096];

	// GLOBAL: TOY2 0x00504D34
	SoftwareRenderItem** g_softwareRenderBuckets = g_softwareRenderBucketStorage;

	// GLOBAL: TOY2 0x00839280
	int32_t g_softwareRenderItemCount;

	// GLOBAL: TOY2 0x00E4D950
	int32_t g_softwarePrimitiveType;

	// GLOBAL: TOY2 0x009F6008
	int32_t g_reverseDepthSortEnabled;

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

	// GLOBAL: TOY2 0x00A4CC80
	int32_t g_sortedRenderFlushPhase;

	// Reset by FlushSortedRenderCommands after the sorted bucket walk. g_unkB626C0 sits just
	// below the bucket array; g_unkB7FBB8 sits just below g_clipLeft. Roles
	// not yet fully understood.
	// GLOBAL: TOY2 0x00B626C0
	int32_t g_unkB626C0;

	// GLOBAL: TOY2 0x00DE20A8
	int32_t g_renderCommandCount;

	// A queued render command for the software rasterizer. QueueRenderCommand enqueues
	// transformed vertices (3 for a triangle, 4 for a quad when vertexCount is
	// 4) and RasterizeRenderCommand dequeues and rasterizes one. Stride 0x9C, capacity 1024
	// (g_renderCommandCount is the live count). The same struct is reused by the sorted
	// path: FlushSortedRenderCommands walks the sorted depth buckets and threads nodes
	// through the next pointer (+0x8C), calling RasterizeSortedRenderCommand per node. The
	// metadata at +0x80 is only partially understood; refine the names when
	// QueueRenderCommand and RasterizeRenderCommand are reconstructed.
	struct RenderCommand
	{
		Nu3D::VertexTL vertices[4]; // +0x00
		uint32_t* texData; // +0x80 (NULL selects an untextured rasterizer)
		int32_t vertexCount; // +0x84 (3 = triangle, 4 = quad)
		int32_t renderState; // +0x88 (Renderer::RENDER_* alpha flags)
		RenderCommand* next; // +0x8C (sorted-path list link)
		int32_t reserved90; // +0x90
		// +0x94: nonzero selects alternate span rasterizers. RasterizeRenderCommand ignores it;
		// RasterizeSortedRenderCommand branches on it, which is what gives it this role.
		int32_t useAlternateSpans;
		int32_t reserved98; // +0x98
	};
	STATIC_ASSERT(sizeof(RenderCommand) == 0x9c);
	STATIC_ASSERT(offsetof(RenderCommand, texData) == 0x80);
	STATIC_ASSERT(offsetof(RenderCommand, next) == 0x8c);
	STATIC_ASSERT(offsetof(RenderCommand, useAlternateSpans) == 0x94);

	struct ScanlineScratch
	{
		union
		{
			int32_t populated;
			uint8_t populatedByte;
		};
		int32_t leftXFixed;
		int32_t leftInterpolants[5];
		int32_t rightXFixed;
		int32_t rightInterpolants[11];
	};
	STATIC_ASSERT(sizeof(ScanlineScratch) == 0x4c);
	STATIC_ASSERT(offsetof(ScanlineScratch, leftXFixed) == 0x4);
	STATIC_ASSERT(offsetof(ScanlineScratch, rightXFixed) == 0x1c);

	// GLOBAL: TOY2 0x008393E0
	ScanlineScratch g_scanlineScratch[1024];

	void ClearScanlineFlags(ScanlineScratch* scanline, int32_t count);

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
	int32_t g_unkB7FBB8;

	// GLOBAL: TOY2 0x00A4CC74
	int32_t g_levelFileIndex;

	// GLOBAL: TOY2 0x009F5FF4
	Nu3D::Viewport::ViewportRect* g_viewportRect;

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
	int32_t g_primaryBucketOffsets[16];

	// GLOBAL: TOY2 0x00508924
	int32_t g_secondaryBucketOffsets[16];

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

	// GLOBAL: TOY2 0x00B626B0
	SpanRasterizer g_spanRasterizer;

	// Vertex 0's diffuse alpha, and its complement 255 - alpha. The two blending
	// span rasterizers read them instead of re-reading the vertex, so the
	// selector has to publish them whenever it picks a blending path.

	// GLOBAL: TOY2 0x00B626AC
	int32_t g_spanAlpha;

	// GLOBAL: TOY2 0x00B626B8
	int32_t g_spanInvAlpha;

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

	// The span rasterizers use 8.8 fixed-point texture coordinates.
	// GLOBAL: TOY2 0x004DDB58
	extern const double k_textureCoordinateScale = 256.0;

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

	// GLOBAL: TOY2 0x004DDAE0
	extern const double k_viewportScaleV = 1.7;

	// GLOBAL: TOY2 0x004DDAD8
	extern const double k_viewportScaleH = 1.9;

	// .rdata depth-sort scale: 1023.0 (= 1024 - 1). SubmitSortedTriangle maps
	// the triangle's minimum vertex z into the 1024-entry bucket range.
	// GLOBAL: TOY2 0x004DDAC4
	extern const float k_depthSortScale = 1023.0f;

	// GLOBAL: TOY2 0x004DDAD4
	extern const float k_reverseDepthSortScale = -25000.0f;

	// GLOBAL: TOY2 0x009F6010
	int32_t g_disableSortedPrimitiveSubmission;

	// Heap buffer allocated by InitSoftwareRenderer (malloc'd, ~1.25MB) and
	// released by Destroy on shutdown.
	// GLOBAL: TOY2 0x0084CBE0
	void* g_softwareRendererBuffer;

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

	// GLOBAL: TOY2 0x00882910
	int32_t g_bitsPerPixel;

	// GLOBAL: TOY2 0x00703E20
	uint16_t g_blueRampFull[64];

	// GLOBAL: TOY2 0x00703EA0
	uint16_t g_greenRampFull[128];

	// GLOBAL: TOY2 0x00703FA0
	uint16_t g_redRampFull[64];

	// GLOBAL: TOY2 0x00704028
	uint16_t g_blueRampLow[64];

	// GLOBAL: TOY2 0x007040A8
	uint16_t g_blueRampHigh[64];

	// GLOBAL: TOY2 0x00704128
	uint16_t g_blueRampMedium[64];

	// GLOBAL: TOY2 0x007041A8
	uint16_t g_greenRampLow[128];

	// GLOBAL: TOY2 0x007042A8
	uint16_t g_greenRampHigh[128];

	// GLOBAL: TOY2 0x007043A8
	uint16_t g_greenRampMedium[128];

	// GLOBAL: TOY2 0x007044A8
	uint16_t g_redRampLow[64];

	// GLOBAL: TOY2 0x00704528
	uint16_t g_redRampHigh[64];

	// GLOBAL: TOY2 0x007045A8
	uint16_t g_redRampMedium[64];

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

	// Back-buffer surface state used by the software frame drain. The surface
	// pointer is the retail `d3dappi.lpBackBuffer` member. The locked pointer is
	// valid only between Lock and Unlock. The pitch is in pixels.
	// GLOBAL: TOY2 0x00534558
	void* g_lockedBackBuffer;

	// GLOBAL: TOY2 0x00882778
	int32_t g_backBufferPitchPixels;

	// Counts deferred frame clears. Initialisation sets it to two, and the first
	// successful clear consumes one count.
	// GLOBAL: TOY2 0x00500C04
	int32_t g_pendingBackBufferClears;

	struct SoftwareRasterVertex
	{
		int32_t x;
		int32_t y;
		int32_t blue;
		int32_t green;
		int32_t red;
		int32_t u;
		int32_t v;
	};

	struct SoftwareRenderItem
	{
		SoftwareRasterVertex vertices[4];
		SoftwareRenderItem* next;
		uint16_t renderFlags;
		uint8_t textureIndex;
		uint8_t reserved[9];
	};

	STATIC_ASSERT(sizeof(SoftwareRasterVertex) == 0x1C);
	STATIC_ASSERT(sizeof(SoftwareRenderItem) == 0x80);
	STATIC_ASSERT(offsetof(SoftwareRenderItem, next) == 0x70);
	STATIC_ASSERT(offsetof(SoftwareRenderItem, renderFlags) == 0x74);
	STATIC_ASSERT(offsetof(SoftwareRenderItem, textureIndex) == 0x76);

	enum SoftwareRenderFlags
	{
		SOFTWARE_RENDER_QUAD = 0x1,
		SOFTWARE_RENDER_COLOUR_KEY = 0x8,
		SOFTWARE_RENDER_BLEND_50 = 0x10,
		SOFTWARE_RENDER_ADDITIVE = 0x20,
		SOFTWARE_RENDER_COLOUR_OFFSET = 0x40,
		SOFTWARE_RENDER_SUBTRACTIVE = 0x1000,
	};

	// GLOBAL: TOY2 0x008827A0
	void* g_softwareTextureData[64];

	typedef void (*SoftwareRenderCallback)(SoftwareRenderItem* item);
	typedef void (*SoftwareSolidQuadCallback)(const PointI* point0, const PointI* point1, const PointI* point2, const PointI* point3, uint32_t colourPair);

	struct SoftwareRenderDispatchTable
	{
		SoftwareRenderCallback highPriority;
		SoftwareSolidQuadCallback solidQuad;
		SoftwareRenderCallback additive;
		SoftwareRenderCallback subtractive;
		SoftwareRenderCallback colourOffset[4];
		SoftwareRenderCallback defaultCallback[4];
		SoftwareRenderCallback flag80;
	};
	STATIC_ASSERT(sizeof(SoftwareRenderDispatchTable) == 0x34);

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
			redRampOffset = item->vertices[0].blue >> 16;
			greenRampOffset = item->vertices[0].green >> 16;
			blueRampOffset = item->vertices[0].red >> 16;
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
						uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
						if (texel != 0x3E0)
						{
							uint16_t destinationPixel = *destination;
							uint16_t litTexel = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
								+ g_blueRampFull[(texel & 0x1F) + blueRampOffset];
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
					uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
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
						uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
						if (texel != 0x3E0)
						{
							uint32_t destinationPixel = *destination;
							uint32_t sourcePixel = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
								+ g_blueRampFull[(texel & 0x1F) + blueRampOffset];
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
					uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
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
						uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
						if (texel != 0x3E0)
						{
							uint16_t litTexel = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
								+ g_blueRampFull[(texel & 0x1F) + blueRampOffset];
							*destination = (*destination >> 1 & 0x3DEF) + (litTexel >> 1 & 0x3DEF);
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
					uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
					if (texel != 0x3E0)
						*destination = (*destination >> 1 & 0x3DEF) + (texel >> 1 & 0x3DEF);
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
					uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
					if (texel != 0x3E0)
					{
						*destination = g_redRampFull[(texel >> 10) + redRampOffset] + g_greenRampFull[((texel >> 5) & 0x1F) + greenRampOffset]
							+ g_blueRampFull[(texel & 0x1F) + blueRampOffset];
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
				uint16_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
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

	// STUB: TOY2 0x00454D30
	void UnkRenderAPI5(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x004560E0
	void UnkRenderAPI6(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00457440
	void UnkRenderAPI7(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00458770
	void UnkRenderAPI8(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x0045E390
	void UnkRenderAPI13(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00461D20
	void UnkRenderAPI14(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00467080
	void UnkRenderAPI17(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00469900
	void UnkRenderAPI18(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x0046AC60
	void UnkRenderAPI19(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x0046BF90
	void UnkRenderAPI20(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x00465180
	void UnkRenderAPI24(SoftwareRenderItem* item) {}
	// STUB: TOY2 0x0046D7B0
	void UnkRenderAPI25(SoftwareRenderItem* item) {}
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
						uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
						if (texel != 0)
						{
							uint8_t litTexel = g_paletteColourOffsetTable[colourOffsetIndex + texel * 0x200];
							*destination = g_additivePaletteTable[*destination + litTexel * 0x100];
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
					uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
					if (texel != 0)
						*destination = g_additivePaletteTable[*destination + texel * 0x100];
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
						uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
						if (texel != 0)
						{
							uint8_t litTexel = g_paletteColourOffsetTable[colourOffsetIndex + texel * 0x200];
							*destination = g_subtractivePaletteTable[*destination * 0x100 + litTexel];
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
					uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
					if (texel != 0)
						*destination = g_subtractivePaletteTable[texel + *destination * 0x100];
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
						uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
						if (texel != 0)
						{
							uint8_t litTexel = g_paletteColourOffsetTable[colourOffsetIndex + texel * 0x200];
							*destination = g_paletteBlend50Table[*destination + litTexel * 0x100];
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
					uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
					if (texel != 0)
						*destination = g_paletteBlend50Table[*destination + texel * 0x100];
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
					uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
					if (texel != 0)
						*destination = g_paletteColourOffsetTable[colourOffsetIndex + texel * 0x200];
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
				uint8_t texel = texture[(rowU >> 16) + ((v >> 16) & 0xFF) * 0x100];
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
		RASTERIZE_SOLID_EDGE(point3, point0, edge30Done8);

		uint8_t* rowStart = (uint8_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		do
		{
			if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
			{
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX != rightX)
				{
					if (leftX < Toy2::g_screenClipLeft)
						leftX = Toy2::g_screenClipLeft;
					if (rightX > Toy2::g_screenClipRight)
						rightX = Toy2::g_screenClipRight;

					uint8_t* pixel = rowStart + leftX - Toy2::g_screenClipLeft;
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
						pixelCount -= filledPixels;
						pixel += filledPixels;
					}
					if (pixelCount != 0)
						*pixel = (uint8_t)colourPair;
				}
			}

			scanline++;
			scanlineCount--;
			rowStart += g_backBufferPitchPixels;
		} while (scanlineCount != 0);
	}

#undef RASTERIZE_SOLID_EDGE

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

	// FUNCTION: TOY2 0x00459AB0 [PROVISIONAL]
	void RasterizeBlend25TexturedPolygon555(SoftwareRenderItem* item)
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

		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[0], &item->vertices[1], edge01DoneBlend25_555);
		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[1], &item->vertices[2], edge12DoneBlend25_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[0], edge20DoneBlend25_555);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[3], edge23DoneBlend25_555);
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[3], &item->vertices[0], edge30DoneBlend25_555);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX != rightX)
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
							pixel = rowStart;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else
							{
								pixelCount = Toy2::g_softWindowWidth;
								if (rightX <= Toy2::g_screenClipRight)
									pixelCount = rightX - Toy2::g_screenClipLeft;
							}
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
							uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX != rightX)
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
						v += clippedPixels * vStep;
						u += clippedPixels * uStep;
						pixel = rowStart;
						if (rightX == Toy2::g_screenClipRight)
							pixelCount = Toy2::g_softWindowWidth - 1;
						else
						{
							pixelCount = Toy2::g_softWindowWidth;
							if (rightX <= Toy2::g_screenClipRight)
								pixelCount = rightX - Toy2::g_screenClipLeft;
						}
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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

		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[0], &item->vertices[1], edge01DoneBlend50_555);
		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[1], &item->vertices[2], edge12DoneBlend50_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[0], edge20DoneBlend50_555);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[3], edge23DoneBlend50_555);
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[3], &item->vertices[0], edge30DoneBlend50_555);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX != rightX)
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
							pixel = rowStart;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else
							{
								pixelCount = Toy2::g_softWindowWidth;
								if (rightX <= Toy2::g_screenClipRight)
									pixelCount = rightX - Toy2::g_screenClipLeft;
							}
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
							uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x3E0)
								*pixel = (*pixel >> 1 & 0x3DEF) + (texel >> 1 & 0x3DEF);
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX != rightX)
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
						v += clippedPixels * vStep;
						u += clippedPixels * uStep;
						pixel = rowStart;
						if (rightX == Toy2::g_screenClipRight)
							pixelCount = Toy2::g_softWindowWidth - 1;
						else
						{
							pixelCount = Toy2::g_softWindowWidth;
							if (rightX <= Toy2::g_screenClipRight)
								pixelCount = rightX - Toy2::g_screenClipLeft;
						}
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
						*pixel = (*pixel >> 1 & 0x3DEF) + (texel >> 1 & 0x3DEF);
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

		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[0], &item->vertices[1], edge01DoneBlend75_555);
		RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[1], &item->vertices[2], edge12DoneBlend75_555);
		if ((item->renderFlags & SOFTWARE_RENDER_QUAD) == 0)
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[0], edge20DoneBlend75_555);
		}
		else
		{
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[2], &item->vertices[3], edge23DoneBlend75_555);
			RASTERIZE_TEXTURED_EDGE_EXCLUSIVE(&item->vertices[3], &item->vertices[0], edge30DoneBlend75_555);
		}

		uint16_t* texture = (uint16_t*)g_softwareTextureData[item->textureIndex];
		uint16_t* rowStart = (uint16_t*)g_lockedBackBuffer + g_backBufferPitchPixels * topY + Toy2::g_screenClipLeft;
		if (item->renderFlags & SOFTWARE_RENDER_COLOUR_KEY)
		{
			do
			{
				if (scanline->populated != 0 && scanline->leftXFixed <= Toy2::g_screenClipRightFixed && scanline->rightXFixed >= Toy2::g_screenClipLeftFixed)
				{
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX != rightX)
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
							pixel = rowStart;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else
							{
								pixelCount = Toy2::g_softWindowWidth;
								if (rightX <= Toy2::g_screenClipRight)
									pixelCount = rightX - Toy2::g_screenClipLeft;
							}
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
							uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX != rightX)
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
						pixel = rowStart;
						if (rightX == Toy2::g_screenClipRight)
							pixelCount = Toy2::g_softWindowWidth - 1;
						else
						{
							pixelCount = Toy2::g_softWindowWidth;
							if (rightX <= Toy2::g_screenClipRight)
								pixelCount = rightX - Toy2::g_screenClipLeft;
						}
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX == rightX)
					{
						uint16_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
							uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					rowStart[leftX - Toy2::g_screenClipLeft] =
						texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
						*pixel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX == rightX)
					{
						uint16_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
							uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					rowStart[leftX - Toy2::g_screenClipLeft] =
						texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
						*pixel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX != rightX)
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
							pixel = rowStart;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else
							{
								pixelCount = Toy2::g_softWindowWidth;
								if (rightX <= Toy2::g_screenClipRight)
									pixelCount = rightX - Toy2::g_screenClipLeft;
							}
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
							uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX != rightX)
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
						pixel = rowStart;
						if (rightX == Toy2::g_screenClipRight)
							pixelCount = Toy2::g_softWindowWidth - 1;
						else
						{
							pixelCount = Toy2::g_softWindowWidth;
							if (rightX <= Toy2::g_screenClipRight)
								pixelCount = rightX - Toy2::g_screenClipLeft;
						}
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX != rightX)
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
							pixel = rowStart;
							if (rightX == Toy2::g_screenClipRight)
								pixelCount = Toy2::g_softWindowWidth - 1;
							else
							{
								pixelCount = Toy2::g_softWindowWidth;
								if (rightX <= Toy2::g_screenClipRight)
									pixelCount = rightX - Toy2::g_screenClipLeft;
							}
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
							uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
							if (texel != 0x7C0)
								*pixel = (*pixel >> 1 & 0x7BEF) + (texel >> 1 & 0x7BEF);
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX != rightX)
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
						v += clippedPixels * vStep;
						u += clippedPixels * uStep;
						pixel = rowStart;
						if (rightX == Toy2::g_screenClipRight)
							pixelCount = Toy2::g_softWindowWidth - 1;
						else
						{
							pixelCount = Toy2::g_softWindowWidth;
							if (rightX <= Toy2::g_screenClipRight)
								pixelCount = rightX - Toy2::g_screenClipLeft;
						}
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					uint16_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
						uint16_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					uint8_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0)
					{
						uint8_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						pixel = g_additivePaletteTable[texel + pixel * 0x100];
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
						uint8_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0)
							*pixel = g_additivePaletteTable[texel + *pixel * 0x100];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					uint8_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
					if (texel != 0)
					{
						uint8_t& pixel = rowStart[leftX - Toy2::g_screenClipLeft];
						pixel = g_subtractivePaletteTable[texel + pixel * 0x100];
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
						uint8_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
						if (texel != 0)
							*pixel = g_subtractivePaletteTable[texel + *pixel * 0x100];
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
					int32_t leftX = scanline->leftXFixed >> 10;
					int32_t rightX = scanline->rightXFixed >> 10;
					if (leftX == rightX)
					{
						uint8_t texel = texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
							uint8_t texel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
				int32_t leftX = scanline->leftXFixed >> 10;
				int32_t rightX = scanline->rightXFixed >> 10;
				if (leftX == rightX)
				{
					rowStart[leftX - Toy2::g_screenClipLeft] =
						texture[(scanline->leftInterpolants[0] >> 16) + (scanline->leftInterpolants[1] >> 8 & 0xFFFFFF00)];
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
						*pixel = texture[(u >> 16) + (v >> 8 & 0xFFFFFF00)];
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
	// STUB: TOY2 0x00477EB0
	void UnkRenderAPI34(SoftwareRenderItem* item) {}

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
		UnkRenderAPI14,
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

	// FUNCTION: TOY2 0x0047C8B0 [PROVISIONAL]
	void BuildColourRampTables()
	{
		if (g_bitsPerPixel == 16)
		{
			int32_t source = -12;
			int32_t tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_redRampFull[tableIndex] = (uint16_t)(level << 11);
				g_redRampLow[tableIndex] = (uint16_t)((level * 10 / 32) << 11);
				g_redRampMedium[tableIndex] = (uint16_t)((level * 17 / 32) << 11);
				g_redRampHigh[tableIndex] = (uint16_t)((level * 25 / 32) << 11);
				++tableIndex;
				++source;
			} while (source + 12 < 64);

			source = -24;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 63)
					level = 63;

				g_greenRampFull[tableIndex] = (uint16_t)(level << 5);
				g_greenRampLow[tableIndex] = (uint16_t)((level * 18 / 64) << 5);
				g_greenRampMedium[tableIndex] = (uint16_t)((level * 33 / 64) << 5);
				g_greenRampHigh[tableIndex] = (uint16_t)((level * 49 / 64) << 5);
				++tableIndex;
				++source;
			} while (source + 24 < 128);

			source = -12;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_blueRampFull[tableIndex] = (uint16_t)level;
				g_blueRampLow[tableIndex] = (uint16_t)(level * 10 / 32);
				g_blueRampMedium[tableIndex] = (uint16_t)(level * 17 / 32);
				g_blueRampHigh[tableIndex] = (uint16_t)(level * 25 / 32);
				++tableIndex;
				++source;
			} while (source + 12 < 64);
		}
		else
		{
			int32_t source = -12;
			int32_t tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_redRampFull[tableIndex] = (uint16_t)(level << 10);
				g_redRampLow[tableIndex] = (uint16_t)((level * 10 / 32) << 10);
				g_redRampMedium[tableIndex] = (uint16_t)((level * 17 / 32) << 10);
				g_redRampHigh[tableIndex] = (uint16_t)((level * 25 / 32) << 10);
				++tableIndex;
				++source;
			} while (source + 12 < 64);

			source = -12;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_greenRampFull[tableIndex] = (uint16_t)(level << 5);
				g_greenRampLow[tableIndex] = (uint16_t)((level * 10 / 32) << 5);
				g_greenRampMedium[tableIndex] = (uint16_t)((level * 17 / 32) << 5);
				g_greenRampHigh[tableIndex] = (uint16_t)((level * 25 / 32) << 5);
				++tableIndex;
				++source;
			} while (source + 12 < 64);

			source = -12;
			tableIndex = 0;
			do
			{
				int32_t level = source;
				if (source < 0)
					level = 0;
				else if (source > 31)
					level = 31;

				g_blueRampFull[tableIndex] = (uint16_t)level;
				g_blueRampLow[tableIndex] = (uint16_t)(level * 10 / 32);
				g_blueRampMedium[tableIndex] = (uint16_t)(level * 17 / 32);
				g_blueRampHigh[tableIndex] = (uint16_t)(level * 25 / 32);
				++tableIndex;
				++source;
			} while (source + 12 < 64);
		}
	}

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

	// FUNCTION: TOY2 0x0047D0F0 [MATCHED]
	void Destroy()
	{
		Logger::Log("QUIT : Destroying software renderer.\n");
		if (g_softwareRendererBuffer)
		{
			free(g_softwareRendererBuffer);
		}
	}

	// FUNCTION: TOY2 0x0047D120 [PROVISIONAL]
	void ClearBackBufferOnce()
	{
		if (g_backBufferClearComplete != 0)
			return;

		uint32_t* pixel = static_cast<uint32_t*>(g_lockedBackBuffer);
		if (g_bitsPerPixel == 8)
		{
			int32_t rowPadding = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 4;
			int32_t rowWidth = Toy2::g_destRectWidth / 4;
			int32_t row = Toy2::g_destRectHeight;
			do
			{
				int32_t count = rowWidth;
				do
					*pixel++ = 0;
				while (--count != 0);

				uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowPadding;
				pixel = reinterpret_cast<uint32_t*>(nextRow);
			} while (--row != 0);
		}
		else
		{
			int32_t rowPadding = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 2;
			int32_t rowWidth = Toy2::g_destRectWidth / 2;
			int32_t row = Toy2::g_destRectHeight;
			do
			{
				int32_t count = rowWidth;
				do
					*pixel++ = 0;
				while (--count != 0);

				uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowPadding;
				pixel = reinterpret_cast<uint32_t*>(nextRow);
			} while (--row != 0);
		}

		g_backBufferClearComplete = 1;
		g_pendingBackBufferClears--;
	}

	// FUNCTION: TOY2 0x0047D540 [PROVISIONAL]
	void FillRect32(uint32_t* dest, int32_t width, int32_t height, int32_t rowPaddingBytes, uint32_t value)
	{
		do
		{
			int32_t count = width;
			do
				*dest++ = value;
			while (--count != 0);

			uint8_t* nextRow = reinterpret_cast<uint8_t*>(dest) + rowPaddingBytes;
			dest = reinterpret_cast<uint32_t*>(nextRow);
		} while (--height != 0);
	}

	// FUNCTION: TOY2 0x0047D650 [PROVISIONAL]
	void ClearScanlineFlags(ScanlineScratch* scanline, int32_t count)
	{
		do
		{
			scanline->populatedByte = 0;
			scanline++;
		} while (--count != 0);
	}

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
			int32_t yaw = (int16_t)Toy2::Camera::g_renderCameraTransform.angles.yaw;
			int32_t sinYaw = Numerics::g_sinCosLUT[-yaw & 0xFFF];
			int32_t cosYaw = Numerics::g_sinCosLUT[0x400 - yaw & 0xFFF];

			Matrix3x3I16 rotation;
			Nu3D::Math::EulerToRotationMatrix(&Toy2::Camera::g_renderCameraTransform.rotationAngles, &rotation);

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

						int32_t component = colour->red * Nu3D::Camera::g_cameraTintBlue;
						mat.diffuse.r = ((component + ((component >> 31) & 0x7F)) >> 7) * (1.0 / 255.0);
						component = colour->green * Nu3D::Camera::g_cameraTintGreen;
						mat.diffuse.g = ((component + ((component >> 31) & 0x7F)) >> 7) * (1.0 / 255.0);
						component = colour->blue * Nu3D::Camera::g_cameraTintRed;
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

	// FUNCTION: TOY2 0x004BCAD0 [PROVISIONAL]
	void QueueRenderCommand(Nu3D::VertexTL* vertices[4], int32_t vertexCount, uint32_t* texData, int32_t renderState)
	{
		if (g_renderCommandCount < 0x400)
		{
			RenderCommand* command = &g_renderCommands[g_renderCommandCount];
			g_renderCommandCount++;
			Nu3D::VertexTL* dst = command->vertices;
			for (int i = 0; i < 3; i++)
			{
				dst[i] = *vertices[i];
			}
			if (vertexCount == 4)
			{
				dst[3] = *vertices[3];
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

	// FUNCTION: TOY2 0x004C4340 [MATCHED]
	void UnpackColourChannels(uint32_t colour, int32_t* red, int32_t* green, int32_t* blue)
	{
		*red = (colour & 0x00ff0000) >> 8;
		*green = colour & 0x0000ff00;
		*blue = (colour & 0x000000ff) << 8;
	}

	// FUNCTION: TOY2 0x004BC900 [MATCHED]
	void UnpackColourToFloats(uint32_t colour, float* red, float* green, float* blue, uint32_t* alphaMask)
	{
		*red = ((colour >> 16) & 0xff) * (1.0 / 255.0);
		*green = ((colour >> 8) & 0xff) * (1.0 / 255.0);
		*blue = (colour & 0xff) * (1.0 / 255.0);
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
			sortDepth *= k_reverseDepthSortScale;
			int32_t depth = 2 - (int32_t)sortDepth;
			if ((g_sortedRenderFlags & 1) == 0)
			{
				bucket = depth + g_primaryBucketOffsets[bucketGroup] * 3;
			}
			else
			{
				bucket = depth + g_secondaryBucketOffsets[bucketGroup] * 3 + 1100;
			}
		}
		else
		{
			int32_t depth = (int32_t)sortDepth;
			if ((g_sortedRenderFlags & 1) == 0)
			{
				bucket = depth + g_primaryBucketOffsets[bucketGroup] * 3;
			}
			else
			{
				bucket = depth + g_secondaryBucketOffsets[bucketGroup] * 3 + 1100;
			}
		}

		if (bucket >= 30000)
		{
			return;
		}

		RenderCommand* command = &g_sortedRenderCommands[g_sortedRenderCommandCount++];
		Nu3D::VertexTL** source = vertices;
		Nu3D::VertexTL* destination = command->vertices;
		*destination++ = **source++;
		*destination++ = **source++;
		*destination = **source;
		if (vertexCount == 4)
		{
			destination[1] = *source[1];
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

	// FUNCTION: TOY2 0x004BCFA0 [PROVISIONAL]
	void ProjectVertex(Nu3D::VertexTL* vertex)
	{
		vertex->specular.value = (uint32_t)(int32_t)vertex->position.x;
		vertex->rhw = vertex->position.y;
		if (vertex->position.z > 0.0)
		{
			vertex->position.x =
				(float)(g_screenDimV / 2) + g_screenDimV * 0.5 * vertex->position.x / (vertex->position.z + 1.0f) * g_zoomScaleV * k_viewportScaleV;
			vertex->position.y =
				(float)(g_screenDimH / 2) + g_screenDimH * 0.5 * vertex->position.y / (vertex->position.z + 1.0f) * g_zoomScaleH * k_viewportScaleH;
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
			if ((vertex0->position.x >= (float)g_clipTop || vertex1->position.x >= (float)g_clipTop || vertex2->position.x >= (float)g_clipTop
					|| vertex3->position.x >= (float)g_clipTop)
				&& (vertex0->position.x <= (float)g_clipBottom || vertex1->position.x <= (float)g_clipBottom || vertex2->position.x <= (float)g_clipBottom
					|| vertex3->position.x <= (float)g_clipBottom)
				&& (vertex0->position.y >= (float)g_clipLeft || vertex1->position.y >= (float)g_clipLeft || vertex2->position.y >= (float)g_clipLeft
					|| vertex3->position.y >= (float)g_clipLeft)
				&& (vertex0->position.y <= (float)g_clipRight || vertex1->position.y <= (float)g_clipRight || vertex2->position.y <= (float)g_clipRight
					|| vertex3->position.y <= (float)g_clipRight))
				return 0;
		}
		else if (vertexCount == 3)
		{
			if ((vertex0->position.x >= (float)g_clipTop || vertex1->position.x >= (float)g_clipTop || vertex2->position.x >= (float)g_clipTop)
				&& (vertex0->position.x <= (float)g_clipBottom || vertex1->position.x <= (float)g_clipBottom || vertex2->position.x <= (float)g_clipBottom)
				&& (vertex0->position.y >= (float)g_clipLeft || vertex1->position.y >= (float)g_clipLeft || vertex2->position.y >= (float)g_clipLeft)
				&& (vertex0->position.y <= (float)g_clipRight || vertex1->position.y <= (float)g_clipRight || vertex2->position.y <= (float)g_clipRight))
				return 0;
		}

		return 1;
	}

	// FUNCTION: TOY2 0x0047C800 [PROVISIONAL]
	void LockBackBuffer()
	{
		DDSURFACEDESC surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);

		HRESULT result;
		do
		{
			result = d3dappi.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (result == DDERR_WASSTILLDRAWING);

		if (result == DD_OK)
		{
			g_lockedBackBuffer = surfaceDesc.lpSurface;
			return;
		}

		g_lockedBackBuffer = NULL;
		Logger::Log("SOFT : ERROR - Failed to lock back buffer - %s.\n", D3DAppErrorToString(result));
	}

	// FUNCTION: TOY2 0x0047C870 [MATCHED]
	void UnlockBackBuffer()
	{
		HRESULT result = d3dappi.lpBackBuffer->Unlock(NULL);
		g_lockedBackBuffer = NULL;
		if (result != DD_OK)
		{
			Logger::Log("SOFT : ERROR - Failed to unlock back buffer - %s.\n", D3DAppErrorToString(result));
		}
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

	// Untextured opaque span for a 16-bit 555 surface. The rasterizer writes two
	// pixels at a time with one interpolated colour. It writes a single pixel at
	// each unaligned end of the span.
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

			if (startX & 1)
			{
				*destRow = (uint16_t)(((uint32_t)edgeBRed & 0xf800) >> 1) + (uint16_t)(((uint32_t)edgeBGreen & 0xf800) >> 6)
					+ (uint16_t)(((uint32_t)edgeBBlue & 0xf800) >> 11);
				destRow++;
				pairCount = (width - 1) >> 1;
			}

			while (pairCount != 0)
			{
				uint32_t pixel = (((uint32_t)edgeBRed & 0xf800) >> 1) + (((uint32_t)edgeBGreen & 0xf800) >> 6) + ((uint32_t)edgeBBlue >> 11);
				*(uint32_t*)destRow = MAKELONG(pixel, pixel);
				destRow += 2;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				pairCount--;
			}

			if (endX & 1)
			{
				*destRow = (uint16_t)(((uint32_t)edgeBRed & 0xf800) >> 1) + (uint16_t)(((uint32_t)edgeBGreen & 0xf800) >> 6)
					+ (uint16_t)(((uint32_t)edgeBBlue & 0xf800) >> 11);
			}
		}
	}

	// The 565 twin of RasterizeOpaqueSpan555. It uses the same paired-pixel walk, but places
	// the five interpolated green bits at bit 6 and red at bit 11.
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

			if (startX & 1)
			{
				*destRow = (uint16_t)((uint32_t)edgeBRed & 0xf800) + (uint16_t)(((uint32_t)edgeBGreen & 0xf800) >> 5)
					+ (uint16_t)(((uint32_t)edgeBBlue & 0xf800) >> 11);
				destRow++;
				pairCount = (width - 1) >> 1;
			}

			while (pairCount != 0)
			{
				uint32_t pixel = ((uint32_t)edgeBRed & 0xf800) + (((uint32_t)edgeBGreen & 0xf800) >> 5) + ((uint32_t)edgeBBlue >> 11);
				*(uint32_t*)destRow = MAKELONG(pixel, pixel);
				destRow += 2;

				edgeBRed += stepRed;
				edgeBGreen += stepGreen;
				edgeBBlue += stepBlue;
				pairCount--;
			}

			if (endX & 1)
			{
				*destRow = (uint16_t)((uint32_t)edgeBRed & 0xf800) + (uint16_t)(((uint32_t)edgeBGreen & 0xf800) >> 5)
					+ (uint16_t)(((uint32_t)edgeBBlue & 0xf800) >> 11);
			}
		}
	}

	// Textured additive span for a 16-bit 555 surface. A texel with a zero
	// high byte is transparent. Other texels brighten the destination channels.
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

	// Untextured additive span for a 16-bit 555 surface.
	//
	// The span brightens what is already on the surface: for every pixel it reads
	// the destination, adds the interpolated span colour to each five-bit channel,
	// saturates each result at 0x1f, and writes the pixel back. This is the
	// RENDER_ALPHA_CUSTOM path with no texture bound, which the selector reaches
	// for glow, muzzle-flash, and light-bloom primitives. texData is unused here;
	// the walker passes it because every span variant shares one signature.
	//
	// The accumulators carry a five-bit channel in the high half of a 16-bit
	// fixed-point value, so each read shifts down by 11. Unlike the subtractive
	// siblings this one keeps them in full 32-bit registers, so retail uses a
	// dword load and a logical shift rather than a word load.
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

	// The 16-bit 565 twin of RasterizeAdditiveSpan555. Only the channel
	// positions move. Green starts at bit 6 and red at bit 11, and the rasterizer
	// still takes five bits per channel, so it uses the high five bits of the
	// six-bit green field.
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

	// The 565 twin of RasterizeTexturedAdditiveSpan555 uses the same texture sampling and additive
	// blend, but reads green at bit 6 and red at bit 11.
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

	// Textured subtractive span for a 16-bit 555 surface. A texel with a zero
	// high byte is transparent. Other texels darken the destination channels.
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

	// Untextured subtractive span for a 16-bit 555 surface.
	//
	// The span darkens what is already on the surface: for every pixel it reads
	// the destination, subtracts the interpolated span colour from each five-bit
	// channel, clamps each result at zero, and writes the pixel back. This is the
	// RENDER_ALPHA_ALT path with no texture bound, which the selector reaches for
	// shadow and darkening primitives. texData is unused here; the walker passes
	// it because every span variant shares one signature.
	//
	// The accumulators carry a five-bit channel in the high half of a 16-bit
	// fixed-point value, so each read truncates to 16 bits and shifts down by 11.
	// That is why retail uses a word load and needs no mask.
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

	// The 16-bit 565 twin of RasterizeSubtractiveSpan555 changes only the
	// channel positions. Green starts at bit 6 and red at bit 11, and the
	// rasterizer still takes five bits per channel, so it uses the high five bits
	// of the six-bit green field.
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

	// The 565 twin of RasterizeTexturedSubtractiveSpan555 uses the same texture sampling and
	// subtractive blend, but reads green at bit 6 and red at bit 11.
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

	// Converts a textured span to the active 16-bit surface format. A texel
	// with a zero high byte is transparent. The colour table entries already
	// contain their packed destination-channel bits.
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
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
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
			if (width == 0)
				return;
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		destRow += (int32_t)edgeB->position.x;

		int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale) << k_textureCoordinateShift;
		int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
		if (textureU > k_textureCoordinateFixedMax)
			textureU = k_textureCoordinateFixedMax;
		if (textureVValue > k_textureCoordinateMax)
			textureVValue = k_textureCoordinateMax;
		int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

		int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale) << k_textureCoordinateShift;
		int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
		if (farTextureU > k_textureCoordinateFixedMax)
			farTextureU = k_textureCoordinateFixedMax;
		if (farTextureVValue > k_textureCoordinateMax)
			farTextureVValue = k_textureCoordinateMax;
		int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;

		int32_t stepRed = (farRed - edgeBRed) / width;
		int32_t stepGreen = (farGreen - edgeBGreen) / width;
		int32_t stepBlue = (farBlue - edgeBBlue) / width;
		int32_t stepTextureU = (farTextureU - textureU) / width;
		int32_t stepTextureV = (farTextureV - textureV) / width;

		do
		{
			int32_t textureIndex =
				((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension + ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
			uint32_t texel = texData[textureIndex];
			if (texel > k_rgbMask)
			{
				int32_t alpha = g_spanAlpha;
				int32_t invAlpha = g_spanInvAlpha;
				uint16_t pixel = *destRow;

				int32_t blue = ((uint32_t)(((texel & 0xff) * edgeBBlue) >> 8) * alpha >> 8) + ((pixel & 0x1f) << 3) * invAlpha;
				if (blue > 0xf800)
					blue = 0xf800;

				int32_t green = ((pixel >> 2) & 0xf8) * invAlpha + ((uint32_t)(alpha * (((texel >> 8) & 0xff) * edgeBGreen >> 8)) >> 8);
				if (green > 0xf800)
					green = 0xf800;

				int32_t red = ((pixel >> 7) & 0xf8) * invAlpha + ((uint32_t)(alpha * (((texel >> 16) & 0xff) * edgeBRed >> 8)) >> 8);
				if (red > 0xf800)
					red = 0xf800;

				*destRow = (uint16_t)(((red >> 1) & 0x7c00) + ((green >> 6) & 0x3e0) + (blue >> 11));
			}

			textureU += stepTextureU;
			textureV += stepTextureV;
			edgeBRed += stepRed;
			edgeBGreen += stepGreen;
			edgeBBlue += stepBlue;
			destRow++;
			width--;
		} while (width != 0);
	}

	// Blends an untextured span with a 555 destination. The source and
	// destination factors are in g_spanAlpha and g_spanInvAlpha.
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
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
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
			if (width == 0)
			{
				return;
			}
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		destRow += (int32_t)edgeB->position.x;
		int32_t stepRed = (farRed - edgeBRed) / width;
		int32_t stepGreen = (farGreen - edgeBGreen) / width;
		int32_t stepBlue = (farBlue - edgeBBlue) / width;

		while (width > 0)
		{
			int32_t alpha = g_spanAlpha;
			uint16_t pixel = *destRow;
			int32_t invAlpha = g_spanInvAlpha;

			int32_t blue = ((pixel & 0x1f) << 3) * invAlpha + ((uint32_t)(alpha * edgeBBlue) >> 8);
			if (blue > 0xf800)
			{
				blue = 0xf800;
			}

			int32_t green = ((pixel >> 2) & 0xf8) * invAlpha + ((uint32_t)(alpha * edgeBGreen) >> 8);
			if (green > 0xf800)
			{
				green = 0xf800;
			}

			int32_t red = ((pixel >> 7) & 0xf8) * invAlpha + ((uint32_t)(alpha * edgeBRed) >> 8);
			if (red > 0xf800)
			{
				red = 0xf800;
			}

			*destRow = (uint16_t)(((red >> 1) & 0x7c00) + ((green >> 6) & 0x3e0) + (blue >> 11));
			destRow++;

			edgeBRed += stepRed;
			edgeBGreen += stepGreen;
			edgeBBlue += stepBlue;
			width--;
		}
	}

	// The alternate-format twin of RasterizeAlphaBlendSpan555 uses the same blend and
	// interpolation, but extracts and packs channels for the other surface mode.
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
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
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
			if (width == 0)
			{
				return;
			}
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		destRow += (int32_t)edgeB->position.x;
		int32_t stepRed = (farRed - edgeBRed) / width;
		int32_t stepGreen = (farGreen - edgeBGreen) / width;
		int32_t stepBlue = (farBlue - edgeBBlue) / width;

		while (width > 0)
		{
			int32_t alpha = g_spanAlpha;
			uint16_t pixel = *destRow;
			int32_t invAlpha = g_spanInvAlpha;

			int32_t blue = ((pixel & 0x1f) << 3) * invAlpha + ((uint32_t)(alpha * edgeBBlue) >> 8);
			if (blue > 0xf800)
			{
				blue = 0xf800;
			}

			int32_t green = ((pixel >> 3) & 0xf8) * invAlpha + ((uint32_t)(alpha * edgeBGreen) >> 8);
			if (green > 0xf800)
			{
				green = 0xf800;
			}

			int32_t red = ((pixel >> 8) & 0xf8) * invAlpha + ((uint32_t)(alpha * edgeBRed) >> 8);
			if (red > 0xf800)
			{
				red = 0xf800;
			}

			*destRow = (uint16_t)((red & 0xf800) + ((green >> 5) & 0x7c0) + (blue >> 11));
			destRow++;

			edgeBRed += stepRed;
			edgeBGreen += stepGreen;
			edgeBBlue += stepBlue;
			width--;
		}
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
		int32_t width = (int32_t)edgeA->position.x - (int32_t)edgeB->position.x;
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
			if (width == 0)
				return;
			farRed = edgeARed;
			farGreen = edgeAGreen;
			farBlue = edgeABlue;
		}

		destRow += (int32_t)edgeB->position.x;

		int32_t textureU = (int32_t)(edgeB->uv.x * k_textureCoordinateScale) << k_textureCoordinateShift;
		int32_t textureVValue = (int32_t)(edgeB->uv.y * k_textureCoordinateScale);
		if (textureU > k_textureCoordinateFixedMax)
			textureU = k_textureCoordinateFixedMax;
		if (textureVValue > k_textureCoordinateMax)
			textureVValue = k_textureCoordinateMax;
		int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

		int32_t farTextureU = (int32_t)(edgeA->uv.x * k_textureCoordinateScale) << k_textureCoordinateShift;
		int32_t farTextureVValue = (int32_t)(edgeA->uv.y * k_textureCoordinateScale);
		if (farTextureU > k_textureCoordinateFixedMax)
			farTextureU = k_textureCoordinateFixedMax;
		if (farTextureVValue > k_textureCoordinateMax)
			farTextureVValue = k_textureCoordinateMax;
		int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;

		int32_t stepRed = (farRed - edgeBRed) / width;
		int32_t stepGreen = (farGreen - edgeBGreen) / width;
		int32_t stepBlue = (farBlue - edgeBBlue) / width;
		int32_t stepTextureU = (farTextureU - textureU) / width;
		int32_t stepTextureV = (farTextureV - textureV) / width;

		do
		{
			int32_t textureIndex =
				((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension + ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
			uint32_t texel = texData[textureIndex];
			if (texel > k_rgbMask)
			{
				int32_t alpha = g_spanAlpha;
				int32_t invAlpha = g_spanInvAlpha;
				uint16_t pixel = *destRow;

				int32_t blue = ((uint32_t)(((texel & 0xff) * edgeBBlue) >> 8) * alpha >> 8) + ((pixel & 0x1f) << 3) * invAlpha;
				if (blue > 0xf800)
					blue = 0xf800;

				int32_t green = ((pixel >> 3) & 0xf8) * invAlpha + ((uint32_t)(alpha * (((texel >> 8) & 0xff) * edgeBGreen >> 8)) >> 8);
				if (green > 0xf800)
					green = 0xf800;

				int32_t red = ((pixel >> 8) & 0xf8) * invAlpha + ((uint32_t)(alpha * (((texel >> 16) & 0xff) * edgeBRed >> 8)) >> 8);
				if (red > 0xf800)
					red = 0xf800;

				*destRow = (uint16_t)((red & 0xf800) + ((green >> 5) & 0x7c0) + (blue >> 11));
			}

			textureU += stepTextureU;
			textureV += stepTextureV;
			edgeBRed += stepRed;
			edgeBGreen += stepGreen;
			edgeBBlue += stepBlue;
			destRow++;
			width--;
		} while (width != 0);
	}

	// Whole-primitive rasterizers for an untextured quad.

	// FUNCTION: TOY2 0x004C80D0 [PROVISIONAL]
	void RasterizeOpaqueQuad555(RenderCommand* command)
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
		uint8_t* destination = (uint8_t*)g_backBuffer + (int32_t)(*edgeAStart)->position.y * g_primarySurfacePitch;

		int32_t edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
		int32_t edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;

		int32_t edgeARed;
		int32_t edgeAGreen;
		int32_t edgeABlue;
		int32_t edgeAEndRed;
		int32_t edgeAEndGreen;
		int32_t edgeAEndBlue;
		int32_t edgeBRed;
		int32_t edgeBGreen;
		int32_t edgeBBlue;
		int32_t edgeBEndRed;
		int32_t edgeBEndGreen;
		int32_t edgeBEndBlue;
		UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
		edgeBRed = edgeARed;
		edgeBGreen = edgeAGreen;
		edgeBBlue = edgeABlue;
		UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
		UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
		Nu3D::VertexTL edgeA = **edgeAStart;
		Nu3D::VertexTL edgeB = **edgeBStart;
		int32_t completedEdge;
		int32_t rows;
		if (edgeARemaining < edgeBRemaining)
		{
			completedEdge = 0;
			rows = edgeARemaining;
		}
		else
		{
			rows = edgeBRemaining;
			completedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
		}
		if (rows < 0)
			return;

		float edgeAXStep;
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / (float)edgeARemaining;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / (float)edgeBRemaining;
			edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
			edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
			edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
		}

		for (;;)
		{
			int32_t remainingRows = rows;
			while (remainingRows > 0)
			{
				Nu3D::VertexTL* leftEdge = &edgeB;
				int32_t leftRed = edgeBRed;
				int32_t leftGreen = edgeBGreen;
				int32_t leftBlue = edgeBBlue;
				Nu3D::VertexTL* rightEdge = &edgeA;
				int32_t rightRed = edgeARed;
				int32_t rightGreen = edgeAGreen;
				int32_t rightBlue = edgeABlue;
				int32_t width = (int32_t)edgeA.position.x - (int32_t)edgeB.position.x;
				if (width < 0)
				{
					leftEdge = &edgeA;
					leftRed = edgeARed;
					leftGreen = edgeAGreen;
					leftBlue = edgeABlue;
					rightEdge = &edgeB;
					rightRed = edgeBRed;
					rightGreen = edgeBGreen;
					rightBlue = edgeBBlue;
					width = -width;
				}

				if (width != 0)
				{
					int32_t pairCount = width >> 1;
					int32_t stepRed;
					int32_t stepGreen;
					int32_t stepBlue;
					if (pairCount > 0)
					{
						stepRed = (rightRed - leftRed) / pairCount;
						stepGreen = (rightGreen - leftGreen) / pairCount;
						stepBlue = (rightBlue - leftBlue) / pairCount;
					}

					int32_t startX = (int32_t)leftEdge->position.x;
					int32_t endX = (int32_t)rightEdge->position.x;
					uint16_t* destRow = (uint16_t*)destination + startX;
					if (startX & 1)
					{
						*destRow = (uint16_t)(((uint32_t)leftRed & 0xf800) >> 1) + (uint16_t)(((uint32_t)leftGreen & 0xf800) >> 6)
							+ (uint16_t)(((uint32_t)leftBlue & 0xf800) >> 11);
						destRow++;
						pairCount = (width - 1) >> 1;
					}

					while (pairCount != 0)
					{
						uint32_t pixel = (((uint32_t)leftRed & 0xf800) >> 1) + (((uint32_t)leftGreen & 0xf800) >> 6) + ((uint32_t)leftBlue >> 11);
						*(uint32_t*)destRow = MAKELONG(pixel, pixel);
						destRow += 2;
						leftRed += stepRed;
						leftGreen += stepGreen;
						leftBlue += stepBlue;
						pairCount--;
					}

					if (endX & 1)
					{
						*destRow = (uint16_t)(((uint32_t)leftRed & 0xf800) >> 1) + (uint16_t)(((uint32_t)leftGreen & 0xf800) >> 6)
							+ (uint16_t)(((uint32_t)leftBlue & 0xf800) >> 11);
					}
				}

				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeAGreen += edgeAGreenStep;
				edgeBGreen += edgeBGreenStep;
				edgeABlue += edgeABlueStep;
				destination += g_primarySurfacePitch;
				edgeBBlue += edgeBBlueStep;
				remainingRows--;
			}

			if (completedEdge == 0)
			{
				edgeBRemaining -= rows;
				edgeAStart++;
				edgeAEnd++;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
			}
			else if (completedEdge == 1)
			{
				edgeARemaining -= rows;
				edgeBStart--;
				edgeBEnd--;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}
			else
			{
				edgeAStart++;
				edgeAEnd++;
				edgeBStart--;
				edgeBEnd--;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}

			int32_t nextCompletedEdge;
			int32_t nextRows;
			if (edgeARemaining < edgeBRemaining)
			{
				nextCompletedEdge = 0;
				nextRows = edgeARemaining;
			}
			else
			{
				nextRows = edgeBRemaining;
				nextCompletedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
			}
			if (nextRows < 0)
				return;

			if (completedEdge == 0)
			{
				edgeA = **edgeAStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
			}
			else if (completedEdge == 1)
			{
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}
			else
			{
				edgeA = **edgeAStart;
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}

			if (edgeARemaining > 0 && completedEdge != 1)
			{
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / (float)edgeARemaining;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / (float)edgeBRemaining;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
	}

	// FUNCTION: TOY2 0x004C8930 [PROVISIONAL]
	void RasterizeOpaqueQuad565(RenderCommand* command)
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
		uint8_t* destination = (uint8_t*)g_backBuffer + (int32_t)(*edgeAStart)->position.y * g_primarySurfacePitch;

		int32_t edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
		int32_t edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;

		int32_t edgeARed;
		int32_t edgeAGreen;
		int32_t edgeABlue;
		int32_t edgeAEndRed;
		int32_t edgeAEndGreen;
		int32_t edgeAEndBlue;
		int32_t edgeBRed;
		int32_t edgeBGreen;
		int32_t edgeBBlue;
		int32_t edgeBEndRed;
		int32_t edgeBEndGreen;
		int32_t edgeBEndBlue;
		UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
		edgeBRed = edgeARed;
		edgeBGreen = edgeAGreen;
		edgeBBlue = edgeABlue;
		UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
		UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
		Nu3D::VertexTL edgeA = **edgeAStart;
		Nu3D::VertexTL edgeB = **edgeBStart;
		int32_t completedEdge;
		int32_t rows;
		if (edgeARemaining < edgeBRemaining)
		{
			completedEdge = 0;
			rows = edgeARemaining;
		}
		else
		{
			rows = edgeBRemaining;
			completedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
		}
		if (rows < 0)
			return;

		float edgeAXStep;
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / (float)edgeARemaining;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / (float)edgeBRemaining;
			edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
			edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
			edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
		}

		for (;;)
		{
			int32_t remainingRows = rows;
			while (remainingRows > 0)
			{
				Nu3D::VertexTL* leftEdge = &edgeB;
				int32_t leftRed = edgeBRed;
				int32_t leftGreen = edgeBGreen;
				int32_t leftBlue = edgeBBlue;
				Nu3D::VertexTL* rightEdge = &edgeA;
				int32_t rightRed = edgeARed;
				int32_t rightGreen = edgeAGreen;
				int32_t rightBlue = edgeABlue;
				int32_t width = (int32_t)edgeA.position.x - (int32_t)edgeB.position.x;
				if (width < 0)
				{
					leftEdge = &edgeA;
					leftRed = edgeARed;
					leftGreen = edgeAGreen;
					leftBlue = edgeABlue;
					rightEdge = &edgeB;
					rightRed = edgeBRed;
					rightGreen = edgeBGreen;
					rightBlue = edgeBBlue;
					width = -width;
				}

				if (width != 0)
				{
					int32_t pairCount = width >> 1;
					int32_t stepRed;
					int32_t stepGreen;
					int32_t stepBlue;
					if (pairCount > 0)
					{
						stepRed = (rightRed - leftRed) / pairCount;
						stepGreen = (rightGreen - leftGreen) / pairCount;
						stepBlue = (rightBlue - leftBlue) / pairCount;
					}

					int32_t startX = (int32_t)leftEdge->position.x;
					int32_t endX = (int32_t)rightEdge->position.x;
					uint16_t* destRow = (uint16_t*)destination + startX;
					if (startX & 1)
					{
						*destRow = (uint16_t)((uint32_t)leftRed & 0xf800) + (uint16_t)(((uint32_t)leftGreen & 0xf800) >> 5)
							+ (uint16_t)(((uint32_t)leftBlue & 0xf800) >> 11);
						destRow++;
						pairCount = (width - 1) >> 1;
					}

					while (pairCount != 0)
					{
						uint32_t pixel = ((uint32_t)leftRed & 0xf800) + (((uint32_t)leftGreen & 0xf800) >> 5) + ((uint32_t)leftBlue >> 11);
						*(uint32_t*)destRow = MAKELONG(pixel, pixel);
						destRow += 2;
						leftRed += stepRed;
						leftGreen += stepGreen;
						leftBlue += stepBlue;
						pairCount--;
					}

					if (endX & 1)
					{
						*destRow = (uint16_t)((uint32_t)leftRed & 0xf800) + (uint16_t)(((uint32_t)leftGreen & 0xf800) >> 5)
							+ (uint16_t)(((uint32_t)leftBlue & 0xf800) >> 11);
					}
				}

				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeAGreen += edgeAGreenStep;
				edgeBGreen += edgeBGreenStep;
				edgeABlue += edgeABlueStep;
				destination += g_primarySurfacePitch;
				edgeBBlue += edgeBBlueStep;
				remainingRows--;
			}

			if (completedEdge == 0)
			{
				edgeBRemaining -= rows;
				edgeAStart++;
				edgeAEnd++;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
			}
			else if (completedEdge == 1)
			{
				edgeARemaining -= rows;
				edgeBStart--;
				edgeBEnd--;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}
			else
			{
				edgeAStart++;
				edgeAEnd++;
				edgeBStart--;
				edgeBEnd--;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}

			int32_t nextCompletedEdge;
			int32_t nextRows;
			if (edgeARemaining < edgeBRemaining)
			{
				nextCompletedEdge = 0;
				nextRows = edgeARemaining;
			}
			else
			{
				nextRows = edgeBRemaining;
				nextCompletedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
			}
			if (nextRows < 0)
				return;

			if (completedEdge == 0)
			{
				edgeA = **edgeAStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
			}
			else if (completedEdge == 1)
			{
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}
			else
			{
				edgeA = **edgeAStart;
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}

			if (edgeARemaining > 0 && completedEdge != 1)
			{
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / (float)edgeARemaining;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / (float)edgeBRemaining;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
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
		uint8_t* destination = (uint8_t*)g_backBuffer + (int32_t)(*edgeAStart)->position.y * g_primarySurfacePitch;

		int32_t edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
		int32_t edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;

		int32_t edgeARed;
		int32_t edgeAGreen;
		int32_t edgeABlue;
		int32_t edgeAEndRed;
		int32_t edgeAEndGreen;
		int32_t edgeAEndBlue;
		int32_t edgeBRed;
		int32_t edgeBGreen;
		int32_t edgeBBlue;
		int32_t edgeBEndRed;
		int32_t edgeBEndGreen;
		int32_t edgeBEndBlue;
		UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
		edgeBRed = edgeARed;
		edgeBGreen = edgeAGreen;
		edgeBBlue = edgeABlue;
		UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
		UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
		Nu3D::VertexTL edgeA = **edgeAStart;
		Nu3D::VertexTL edgeB = **edgeBStart;
		int32_t completedEdge;
		int32_t rows;
		if (edgeARemaining < edgeBRemaining)
		{
			completedEdge = 0;
			rows = edgeARemaining;
		}
		else
		{
			rows = edgeBRemaining;
			completedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
		}
		if (rows < 0)
			return;

		float edgeAXStep;
		float edgeAUStep;
		float edgeAVStep;
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			float edgeHeight = (float)edgeARemaining;
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
			edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
			edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		float edgeBUStep;
		float edgeBVStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			float edgeHeight = (float)edgeBRemaining;
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
			edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
			edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
			edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
			edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
			edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
		}

		for (;;)
		{
			int32_t remainingRows = rows;
			while (remainingRows > 0)
			{
				g_spanRasterizer(&edgeA, &edgeB, (uint16_t*)destination, texData, edgeARed, edgeAGreen, edgeABlue, edgeBRed, edgeBGreen, edgeBBlue);
				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeA.uv.x += edgeAUStep;
				edgeAGreen += edgeAGreenStep;
				edgeA.uv.y += edgeAVStep;
				edgeABlue += edgeABlueStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeBGreen += edgeBGreenStep;
				edgeBBlue += edgeBBlueStep;
				edgeB.uv.x += edgeBUStep;
				destination += g_primarySurfacePitch;
				edgeB.uv.y += edgeBVStep;
				remainingRows--;
			}

			if (completedEdge == 0)
			{
				edgeBRemaining -= rows;
				edgeAStart++;
				edgeAEnd++;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
			}
			else if (completedEdge == 1)
			{
				edgeARemaining -= rows;
				edgeBStart--;
				edgeBEnd--;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}
			else
			{
				edgeAStart++;
				edgeAEnd++;
				edgeBStart--;
				edgeBEnd--;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}

			int32_t nextCompletedEdge;
			int32_t nextRows;
			if (edgeARemaining < edgeBRemaining)
			{
				nextCompletedEdge = 0;
				nextRows = edgeARemaining;
			}
			else
			{
				nextRows = edgeBRemaining;
				nextCompletedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
			}
			if (nextRows < 0)
				return;

			if (completedEdge == 0)
			{
				edgeA = **edgeAStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
			}
			else if (completedEdge == 1)
			{
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}
			else
			{
				edgeA = **edgeAStart;
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}

			if (edgeARemaining > 0 && completedEdge != 1)
			{
				float edgeHeight = (float)edgeARemaining;
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
				edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
				edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				float edgeHeight = (float)edgeBRemaining;
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
				edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
				edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
	}

	// This quad path samples one texel for each pixel.
	// FUNCTION: TOY2 0x004C6B80 [PROVISIONAL]
	void RasterizeTexturedQuad(RenderCommand* command, uint32_t* texData)
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
		uint8_t* destination = (uint8_t*)g_backBuffer + (int32_t)(*edgeAStart)->position.y * g_primarySurfacePitch;

		int32_t edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
		int32_t edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;

		int32_t edgeARed;
		int32_t edgeAGreen;
		int32_t edgeABlue;
		int32_t edgeAEndRed;
		int32_t edgeAEndGreen;
		int32_t edgeAEndBlue;
		int32_t edgeBRed;
		int32_t edgeBGreen;
		int32_t edgeBBlue;
		int32_t edgeBEndRed;
		int32_t edgeBEndGreen;
		int32_t edgeBEndBlue;
		UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
		edgeBRed = edgeARed;
		edgeBGreen = edgeAGreen;
		edgeBBlue = edgeABlue;
		UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
		UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
		Nu3D::VertexTL edgeA = **edgeAStart;
		Nu3D::VertexTL edgeB = **edgeBStart;
		int32_t completedEdge;
		int32_t rows;
		if (edgeARemaining < edgeBRemaining)
		{
			completedEdge = 0;
			rows = edgeARemaining;
		}
		else
		{
			rows = edgeBRemaining;
			completedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
		}
		if (rows < 0)
			return;

		float edgeAXStep;
		float edgeAUStep;
		float edgeAVStep;
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			float edgeHeight = (float)edgeARemaining;
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
			edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
			edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		float edgeBUStep;
		float edgeBVStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			float edgeHeight = (float)edgeBRemaining;
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
			edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
			edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
			edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
			edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
			edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
		}

		for (;;)
		{
			int32_t remainingRows = rows;
			while (remainingRows > 0)
			{
				Nu3D::VertexTL* leftEdge = &edgeB;
				Nu3D::VertexTL* rightEdge = &edgeA;
				int32_t leftRed = edgeBRed;
				int32_t leftGreen = edgeBGreen;
				int32_t leftBlue = edgeBBlue;
				int32_t rightRed = edgeARed;
				int32_t rightGreen = edgeAGreen;
				int32_t rightBlue = edgeABlue;
				int32_t width = (int32_t)edgeA.position.x - (int32_t)edgeB.position.x;
				if (width < 0)
				{
					leftEdge = &edgeA;
					rightEdge = &edgeB;
					leftRed = edgeARed;
					leftGreen = edgeAGreen;
					leftBlue = edgeABlue;
					rightRed = edgeBRed;
					rightGreen = edgeBGreen;
					rightBlue = edgeBBlue;
					width = -width;
				}

				if (width != 0)
				{
					int32_t pairCount = width >> 1;
					int32_t stepRed;
					int32_t stepGreen;
					int32_t stepBlue;
					if (pairCount > 0)
					{
						stepRed = (rightRed - leftRed) / pairCount;
						stepGreen = (rightGreen - leftGreen) / pairCount;
						stepBlue = (rightBlue - leftBlue) / pairCount;
					}

					int32_t startX = (int32_t)leftEdge->position.x;
					int32_t endX = (int32_t)rightEdge->position.x;
					uint16_t* destRow = (uint16_t*)destination + startX;

					int32_t textureU = (int32_t)(leftEdge->uv.x * k_textureCoordinateScale);
					if (textureU > k_textureCoordinateMax)
						textureU = k_textureCoordinateMax;
					textureU <<= k_textureCoordinateShift;

					int32_t textureVValue = (int32_t)(leftEdge->uv.y * k_textureCoordinateScale);
					if (textureVValue > k_textureCoordinateMax)
						textureVValue = k_textureCoordinateMax;
					int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

					int32_t farTextureU = (int32_t)(rightEdge->uv.x * k_textureCoordinateScale);
					if (farTextureU > k_textureCoordinateMax)
						farTextureU = k_textureCoordinateMax;
					farTextureU <<= k_textureCoordinateShift;
					if (farTextureU > k_textureCoordinateFixedMax)
						farTextureU = k_textureCoordinateFixedMax;
					int32_t stepTextureU = (farTextureU - textureU) / width;

					int32_t farTextureVValue = (int32_t)(rightEdge->uv.y * k_textureCoordinateScale);
					if (farTextureVValue > k_textureCoordinateMax)
						farTextureVValue = k_textureCoordinateMax;
					int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
					if (farTextureV > k_textureCoordinateFixedMax)
						farTextureV = k_textureCoordinateFixedMax;
					int32_t stepTextureV = (farTextureV - textureV) / width;

					if (startX & 1)
					{
						int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel = texData[textureIndex];
						*destRow++ = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
						pairCount = (width - 1) >> 1;
						textureU += stepTextureU;
						textureV += stepTextureV;
					}

					while (pairCount != 0)
					{
						int32_t textureIndex0 = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						int32_t nextTextureU = textureU + stepTextureU;
						int32_t nextTextureV = textureV + stepTextureV;
						int32_t textureIndex1 = ((nextTextureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((nextTextureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel0 = texData[textureIndex0];
						uint32_t texel1 = texData[textureIndex1];
						uint16_t pixel0 = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel0 & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel0 >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel0 >> 16) & k_lowerByteMask)];
						uint16_t pixel1 = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel1 & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel1 >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel1 >> 16) & k_lowerByteMask)];
						*(uint32_t*)destRow = pixel0 | ((uint32_t)pixel1 << 16);
						destRow += 2;
						textureU += stepTextureU * 2;
						textureV += stepTextureV * 2;
						leftRed += stepRed;
						leftGreen += stepGreen;
						leftBlue += stepBlue;
						pairCount--;
					}

					if (endX & 1)
					{
						int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel = texData[textureIndex];
						*destRow = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					}
				}

				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeA.uv.x += edgeAUStep;
				edgeAGreen += edgeAGreenStep;
				edgeA.uv.y += edgeAVStep;
				edgeABlue += edgeABlueStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeBGreen += edgeBGreenStep;
				edgeBBlue += edgeBBlueStep;
				edgeB.uv.x += edgeBUStep;
				destination += g_primarySurfacePitch;
				edgeB.uv.y += edgeBVStep;
				remainingRows--;
			}

			if (completedEdge == 0)
			{
				edgeBRemaining -= rows;
				edgeAStart++;
				edgeAEnd++;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
			}
			else if (completedEdge == 1)
			{
				edgeARemaining -= rows;
				edgeBStart--;
				edgeBEnd--;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}
			else
			{
				edgeAStart++;
				edgeAEnd++;
				edgeBStart--;
				edgeBEnd--;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}

			int32_t nextCompletedEdge;
			int32_t nextRows;
			if (edgeARemaining < edgeBRemaining)
			{
				nextCompletedEdge = 0;
				nextRows = edgeARemaining;
			}
			else
			{
				nextRows = edgeBRemaining;
				nextCompletedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
			}
			if (nextRows < 0)
				return;

			if (completedEdge == 0)
			{
				edgeA = **edgeAStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
			}
			else if (completedEdge == 1)
			{
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}
			else
			{
				edgeA = **edgeAStart;
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}

			if (edgeARemaining > 0 && completedEdge != 1)
			{
				float edgeHeight = (float)edgeARemaining;
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
				edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
				edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				float edgeHeight = (float)edgeBRemaining;
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
				edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
				edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
	}

	// This quad path samples one texel for each aligned pixel pair.
	// FUNCTION: TOY2 0x004C7630 [PROVISIONAL]
	void RasterizeTexturedQuadPairSample(RenderCommand* command, uint32_t* texData)
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
		uint8_t* destination = (uint8_t*)g_backBuffer + (int32_t)(*edgeAStart)->position.y * g_primarySurfacePitch;

		int32_t edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
		int32_t edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;

		int32_t edgeARed;
		int32_t edgeAGreen;
		int32_t edgeABlue;
		int32_t edgeAEndRed;
		int32_t edgeAEndGreen;
		int32_t edgeAEndBlue;
		int32_t edgeBRed;
		int32_t edgeBGreen;
		int32_t edgeBBlue;
		int32_t edgeBEndRed;
		int32_t edgeBEndGreen;
		int32_t edgeBEndBlue;
		UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
		edgeBRed = edgeARed;
		edgeBGreen = edgeAGreen;
		edgeBBlue = edgeABlue;
		UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
		UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
		Nu3D::VertexTL edgeA = **edgeAStart;
		Nu3D::VertexTL edgeB = **edgeBStart;
		int32_t completedEdge;
		int32_t rows;
		if (edgeARemaining < edgeBRemaining)
		{
			completedEdge = 0;
			rows = edgeARemaining;
		}
		else
		{
			rows = edgeBRemaining;
			completedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
		}
		if (rows < 0)
			return;

		float edgeAXStep;
		float edgeAUStep;
		float edgeAVStep;
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			float edgeHeight = (float)edgeARemaining;
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
			edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
			edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		float edgeBUStep;
		float edgeBVStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			float edgeHeight = (float)edgeBRemaining;
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
			edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
			edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
			edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
			edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
			edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
		}

		for (;;)
		{
			int32_t remainingRows = rows;
			while (remainingRows > 0)
			{
				Nu3D::VertexTL* leftEdge = &edgeB;
				Nu3D::VertexTL* rightEdge = &edgeA;
				int32_t leftRed = edgeBRed;
				int32_t leftGreen = edgeBGreen;
				int32_t leftBlue = edgeBBlue;
				int32_t rightRed = edgeARed;
				int32_t rightGreen = edgeAGreen;
				int32_t rightBlue = edgeABlue;
				int32_t width = (int32_t)edgeA.position.x - (int32_t)edgeB.position.x;
				if (width < 0)
				{
					leftEdge = &edgeA;
					rightEdge = &edgeB;
					leftRed = edgeARed;
					leftGreen = edgeAGreen;
					leftBlue = edgeABlue;
					rightRed = edgeBRed;
					rightGreen = edgeBGreen;
					rightBlue = edgeBBlue;
					width = -width;
				}

				if (width != 0)
				{
					int32_t pairCount = width >> 1;
					int32_t stepRed;
					int32_t stepGreen;
					int32_t stepBlue;
					if (pairCount > 0)
					{
						stepRed = (rightRed - leftRed) / pairCount;
						stepGreen = (rightGreen - leftGreen) / pairCount;
						stepBlue = (rightBlue - leftBlue) / pairCount;
					}

					int32_t startX = (int32_t)leftEdge->position.x;
					int32_t endX = (int32_t)rightEdge->position.x;
					uint16_t* destRow = (uint16_t*)destination + startX;

					int32_t textureU = (int32_t)(leftEdge->uv.x * k_textureCoordinateScale);
					if (textureU > k_textureCoordinateMax)
						textureU = k_textureCoordinateMax;
					textureU <<= k_textureCoordinateShift;

					int32_t textureVValue = (int32_t)(leftEdge->uv.y * k_textureCoordinateScale);
					if (textureVValue > k_textureCoordinateMax)
						textureVValue = k_textureCoordinateMax;
					int32_t textureV = (k_textureCoordinateMax - textureVValue) << k_textureCoordinateShift;

					int32_t farTextureU = (int32_t)(rightEdge->uv.x * k_textureCoordinateScale);
					if (farTextureU > k_textureCoordinateMax)
						farTextureU = k_textureCoordinateMax;
					farTextureU <<= k_textureCoordinateShift;
					if (farTextureU > k_textureCoordinateFixedMax)
						farTextureU = k_textureCoordinateFixedMax;
					int32_t stepTextureU = (farTextureU - textureU) / width;

					int32_t farTextureVValue = (int32_t)(rightEdge->uv.y * k_textureCoordinateScale);
					if (farTextureVValue > k_textureCoordinateMax)
						farTextureVValue = k_textureCoordinateMax;
					int32_t farTextureV = (k_textureCoordinateMax - farTextureVValue) << k_textureCoordinateShift;
					if (farTextureV > k_textureCoordinateFixedMax)
						farTextureV = k_textureCoordinateFixedMax;
					int32_t stepTextureV = (farTextureV - textureV) / width;

					if (startX & 1)
					{
						int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel = texData[textureIndex];
						*destRow++ = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
						pairCount = (width - 1) >> 1;
						textureU += stepTextureU;
						textureV += stepTextureV;
					}

					while (pairCount != 0)
					{
						int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel = texData[textureIndex];
						uint16_t pixel = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
						*(uint32_t*)destRow = MAKELONG(pixel, pixel);
						destRow += 2;
						textureU += stepTextureU * 2;
						textureV += stepTextureV * 2;
						leftRed += stepRed;
						leftGreen += stepGreen;
						leftBlue += stepBlue;
						pairCount--;
					}

					if (endX & 1)
					{
						int32_t textureIndex = ((textureV >> k_textureCoordinateShift) & k_lowerByteMask) * k_textureDimension
							+ ((textureU >> k_textureCoordinateShift) & k_lowerByteMask);
						uint32_t texel = texData[textureIndex];
						*destRow = g_colourScaleTable0[(leftBlue & k_upperByteMask) + (texel & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize + (leftGreen & k_upperByteMask) + ((texel >> 8) & k_lowerByteMask)]
							+ g_colourScaleTable0[k_colourScaleSubtableSize * 2 + (leftRed & k_upperByteMask) + ((texel >> 16) & k_lowerByteMask)];
					}
				}

				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeA.uv.x += edgeAUStep;
				edgeAGreen += edgeAGreenStep;
				edgeA.uv.y += edgeAVStep;
				edgeABlue += edgeABlueStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeBGreen += edgeBGreenStep;
				edgeBBlue += edgeBBlueStep;
				edgeB.uv.x += edgeBUStep;
				destination += g_primarySurfacePitch;
				edgeB.uv.y += edgeBVStep;
				remainingRows--;
			}

			if (completedEdge == 0)
			{
				edgeBRemaining -= rows;
				edgeAStart++;
				edgeAEnd++;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
			}
			else if (completedEdge == 1)
			{
				edgeARemaining -= rows;
				edgeBStart--;
				edgeBEnd--;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}
			else
			{
				edgeAStart++;
				edgeAEnd++;
				edgeBStart--;
				edgeBEnd--;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}

			int32_t nextCompletedEdge;
			int32_t nextRows;
			if (edgeARemaining < edgeBRemaining)
			{
				nextCompletedEdge = 0;
				nextRows = edgeARemaining;
			}
			else
			{
				nextRows = edgeBRemaining;
				nextCompletedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
			}
			if (nextRows < 0)
				return;

			if (completedEdge == 0)
			{
				edgeA = **edgeAStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
			}
			else if (completedEdge == 1)
			{
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}
			else
			{
				edgeA = **edgeAStart;
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}

			if (edgeARemaining > 0 && completedEdge != 1)
			{
				float edgeHeight = (float)edgeARemaining;
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
				edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
				edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				float edgeHeight = (float)edgeBRemaining;
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
				edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
				edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
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
		uint8_t* destination = (uint8_t*)g_backBuffer + (int32_t)(*edgeAStart)->position.y * g_primarySurfacePitch;

		int32_t edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
		int32_t edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;

		int32_t edgeARed;
		int32_t edgeAGreen;
		int32_t edgeABlue;
		int32_t edgeAEndRed;
		int32_t edgeAEndGreen;
		int32_t edgeAEndBlue;
		int32_t edgeBRed;
		int32_t edgeBGreen;
		int32_t edgeBBlue;
		int32_t edgeBEndRed;
		int32_t edgeBEndGreen;
		int32_t edgeBEndBlue;
		UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
		edgeBRed = edgeARed;
		edgeBGreen = edgeAGreen;
		edgeBBlue = edgeABlue;
		UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
		UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
		Nu3D::VertexTL edgeA = **edgeAStart;
		Nu3D::VertexTL edgeB = **edgeBStart;
		int32_t completedEdge;
		int32_t rows;
		if (edgeARemaining < edgeBRemaining)
		{
			completedEdge = 0;
			rows = edgeARemaining;
		}
		else
		{
			rows = edgeBRemaining;
			completedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
		}
		if (rows < 0)
			return;

		float edgeAXStep;
		float edgeAUStep;
		float edgeAVStep;
		int32_t edgeARedStep;
		int32_t edgeAGreenStep;
		int32_t edgeABlueStep;
		if (edgeARemaining > 0)
		{
			float edgeHeight = (float)edgeARemaining;
			edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
			edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
			edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
			edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
			edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
			edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
		}

		float edgeBXStep;
		float edgeBUStep;
		float edgeBVStep;
		int32_t edgeBRedStep;
		int32_t edgeBGreenStep;
		int32_t edgeBBlueStep;
		if (edgeBRemaining > 0)
		{
			float edgeHeight = (float)edgeBRemaining;
			edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
			edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
			edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
			edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
			edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
			edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
		}

		for (;;)
		{
			int32_t remainingRows = rows;
			while (remainingRows > 0)
			{
				g_spanRasterizer(&edgeA, &edgeB, (uint16_t*)destination, texData, edgeARed, edgeAGreen, edgeABlue, edgeBRed, edgeBGreen, edgeBBlue);
				edgeA.position.x += edgeAXStep;
				edgeARed += edgeARedStep;
				edgeA.uv.x += edgeAUStep;
				edgeAGreen += edgeAGreenStep;
				edgeA.uv.y += edgeAVStep;
				edgeABlue += edgeABlueStep;
				edgeBRed += edgeBRedStep;
				edgeB.position.x += edgeBXStep;
				edgeBGreen += edgeBGreenStep;
				edgeBBlue += edgeBBlueStep;
				edgeB.uv.x += edgeBUStep;
				destination += g_primarySurfacePitch;
				edgeB.uv.y += edgeBVStep;
				remainingRows--;
			}

			if (completedEdge == 0)
			{
				edgeBRemaining -= rows;
				edgeAStart++;
				edgeAEnd++;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
			}
			else if (completedEdge == 1)
			{
				edgeARemaining -= rows;
				edgeBStart--;
				edgeBEnd--;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}
			else
			{
				edgeAStart++;
				edgeAEnd++;
				edgeBStart--;
				edgeBEnd--;
				edgeARemaining = (int32_t)(*edgeAEnd)->position.y - (int32_t)(*edgeAStart)->position.y;
				edgeBRemaining = (int32_t)(*edgeBEnd)->position.y - (int32_t)(*edgeBStart)->position.y;
			}

			int32_t nextCompletedEdge;
			int32_t nextRows;
			if (edgeARemaining < edgeBRemaining)
			{
				nextCompletedEdge = 0;
				nextRows = edgeARemaining;
			}
			else
			{
				nextRows = edgeBRemaining;
				nextCompletedEdge = edgeBRemaining < edgeARemaining ? 1 : 2;
			}
			if (nextRows < 0)
				return;

			if (completedEdge == 0)
			{
				edgeA = **edgeAStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
			}
			else if (completedEdge == 1)
			{
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}
			else
			{
				edgeA = **edgeAStart;
				edgeB = **edgeBStart;
				UnpackColourChannels((*edgeAStart)->diffuse.value, &edgeARed, &edgeAGreen, &edgeABlue);
				UnpackColourChannels((*edgeAEnd)->diffuse.value, &edgeAEndRed, &edgeAEndGreen, &edgeAEndBlue);
				UnpackColourChannels((*edgeBStart)->diffuse.value, &edgeBRed, &edgeBGreen, &edgeBBlue);
				UnpackColourChannels((*edgeBEnd)->diffuse.value, &edgeBEndRed, &edgeBEndGreen, &edgeBEndBlue);
			}

			if (edgeARemaining > 0 && completedEdge != 1)
			{
				float edgeHeight = (float)edgeARemaining;
				edgeAXStep = ((*edgeAEnd)->position.x - (*edgeAStart)->position.x) / edgeHeight;
				edgeAUStep = ((*edgeAEnd)->uv.x - (*edgeAStart)->uv.x) / edgeHeight;
				edgeAVStep = ((*edgeAEnd)->uv.y - (*edgeAStart)->uv.y) / edgeHeight;
				edgeARedStep = (edgeAEndRed - edgeARed) / edgeARemaining;
				edgeAGreenStep = (edgeAEndGreen - edgeAGreen) / edgeARemaining;
				edgeABlueStep = (edgeAEndBlue - edgeABlue) / edgeARemaining;
			}
			if (edgeBRemaining > 0 && completedEdge != 0)
			{
				float edgeHeight = (float)edgeBRemaining;
				edgeBXStep = ((*edgeBEnd)->position.x - (*edgeBStart)->position.x) / edgeHeight;
				edgeBUStep = ((*edgeBEnd)->uv.x - (*edgeBStart)->uv.x) / edgeHeight;
				edgeBVStep = ((*edgeBEnd)->uv.y - (*edgeBStart)->uv.y) / edgeHeight;
				edgeBRedStep = (edgeBEndRed - edgeBRed) / edgeBRemaining;
				edgeBGreenStep = (edgeBEndGreen - edgeBGreen) / edgeBRemaining;
				edgeBBlueStep = (edgeBEndBlue - edgeBBlue) / edgeBRemaining;
			}
			completedEdge = nextCompletedEdge;
			rows = nextRows;
			if (edgeAStart >= edgeBStart)
				return;
		}
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

	// FUNCTION: TOY2 0x004C9A50 [MATCHED]
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

	// FUNCTION: TOY2 0x004BCB60 [PROVISIONAL]
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
		g_unkB7FBB8 = 0;
		g_unkB626C0 = 0;
		g_sortedRenderFlushPhase++;
	}

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

	// Rebuilds the live palette entries (1..255) by tinting the source palette
	// (0..254) by the camera tint colour in fixed point (/128), preserving
	// entry 0, then commits the tinted range to the DirectDraw palette. Called
	// from Renderer::DrawTintOverlay.
	//
	// FUNCTION: TOY2 0x00470C70 [MATCHED]
	void UpdatePaletteTint()
	{
		for (int32_t i = 0; i < 0x3fc; i += 4)
		{
			g_paletteEntries[i + 4] = (uint8_t)(g_paletteSource[i + 4] * Nu3D::Camera::g_cameraTintBlue / 128);
			g_paletteEntries[i + 5] = (uint8_t)(g_paletteSource[i + 5] * Nu3D::Camera::g_cameraTintGreen / 128);
			g_paletteEntries[i + 6] = (uint8_t)(g_paletteSource[i + 6] * Nu3D::Camera::g_cameraTintRed / 128);
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

	// Converts the viewport rect (passed as top/bottom/left/right matching the
	// ViewportRect field order; GetViewClipRect stores the left value in the
	// bottom field and the bottom value in the left field) into the integer
	// clip rect g_clipTop/Bottom/Left/Right. Each edge is recomputed only when
	// its source field changes since the last call. The vertical pair (top,
	// left-field) scales through g_screenDimV; the horizontal pair (bottom-field,
	// right) scales through g_screenDimH.
	//
	// Residual diff is CAP-15: the build CSEs the right param load into ECX
	// where retail re-reads [esp+0x20] at each use, which reorders the V/H
	// conditional blocks. The scale and clamp blocks all match.
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

	// This function resolves the current texture with GetCurrentTextureData.
	// It groups the index stream into one triple for each triangle.
	// It sends each triple and the selected texture pointer to UnkFunc22.
	// Each index selects one 32-byte VertexTL from lpvVertices.
	//
	// dwFlags is unused by the retail body.
	//
	// Residual is CAP-19: MSVC assigns maskedTexData to EBP and keeps the
	// triangle count in EBX, where retail assigns maskedTexData to EBX and
	// spills the count to EBP (pushed inside the loop guard). The loop body
	// also takes a negative-offset early-increment index strip
	// ([esi-4]/[esi-2]/[esi] then add esi,6) where retail reads
	// [esi]/[esi+2] then bumps twice before [esi]. Both compute identical
	// vertices; the divergence is register allocation plus the pointer-walk
	// transformation. Robust across 5 source forms (34.8-36.5%).
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
				UnkFunc22(vertices, 3, maskedTexData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
				indices += 3;
				remaining--;
			} while (remaining != 0);
		}
	}

	// STUB: TOY2 0x004C0320
	void UnkFunc22(Nu3D::VertexTL* vertices[3], int32_t vertexCount, uint32_t* texData, int32_t renderState, int32_t primitiveType, DWORD drawFlags) {}

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
			UnkFunc22(vertices, 4, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);

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
				UnkFunc22(vertices, 4, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
			}

			if ((dwIndexCount & 1) != 0)
			{
				vertices[0] = &vertexBase[*lpwIndices];
				vertices[1] = &vertexBase[previousIndex1];
				vertices[2] = &vertexBase[previousIndex0];
				UnkFunc22(vertices, 3, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
			}
		}
		else
		{
			vertices[0] = &vertexBase[lpwIndices[0]];
			vertices[1] = &vertexBase[lpwIndices[1]];
			vertices[2] = &vertexBase[lpwIndices[2]];
			UnkFunc22(vertices, 3, texData, Renderer::g_renderStateCache[0], g_softwarePrimitiveType, 0);
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

	// Locks the DirectDraw back buffer, clears it when the frame state requires
	// a clear, drains all software-render depth buckets from far to near, then
	// unlocks and presents the surface. The caller passes the display maximum x
	// and a zero clear value. Retail does not read either parameter.
	// FUNCTION: TOY2 0x0047D210 [PROVISIONAL]
	void RenderSoftwareFrame(int32_t displayMaxX, int32_t clearValue)
	{
		DDSURFACEDESC surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);

		HRESULT result;
		do
		{
			result = d3dappi.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (result == DDERR_WASSTILLDRAWING);

		if (result == DD_OK)
		{
			g_lockedBackBuffer = surfaceDesc.lpSurface;
		}
		else
		{
			g_lockedBackBuffer = NULL;
			Logger::Log("SOFT : ERROR - Failed to lock back buffer - %s.\n", D3DAppErrorToString(result));
		}

		if (Nu3D::Camera::g_cameraTintBlue != 0x80 && g_renderMode == RENDERMODE_SOFTWARE && g_bitsPerPixel != 8)
		{
			int32_t rowSkip = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 2;
			int32_t rowWidth = Toy2::g_destRectWidth / 2;
			uint32_t* pixel = static_cast<uint32_t*>(g_lockedBackBuffer);
			for (int32_t row = Toy2::g_destRectHeight; row != 0; row--)
			{
				for (int32_t count = rowWidth; count != 0; count--)
				{
					*pixel++ = 0;
				}
				uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowSkip;
				pixel = reinterpret_cast<uint32_t*>(nextRow);
			}
		}
		else
		{
			if (g_pendingBackBufferClears != 0 && g_backBufferClearComplete == 0)
			{
				uint32_t* pixel = static_cast<uint32_t*>(g_lockedBackBuffer);
				if (g_bitsPerPixel == 8)
				{
					int32_t rowSkip = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 4;
					int32_t rowWidth = Toy2::g_destRectWidth / 4;
					for (int32_t row = Toy2::g_destRectHeight; row != 0; row--)
					{
						for (int32_t count = rowWidth; count != 0; count--)
						{
							*pixel++ = 0;
						}
						uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowSkip;
						pixel = reinterpret_cast<uint32_t*>(nextRow);
					}
				}
				else
				{
					int32_t rowSkip = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 2;
					int32_t rowWidth = Toy2::g_destRectWidth / 2;
					for (int32_t row = Toy2::g_destRectHeight; row != 0; row--)
					{
						for (int32_t count = rowWidth; count != 0; count--)
						{
							*pixel++ = 0;
						}
						uint8_t* nextRow = reinterpret_cast<uint8_t*>(pixel) + rowSkip;
						pixel = reinterpret_cast<uint32_t*>(nextRow);
					}
				}
				g_backBufferClearComplete = 1;
				g_pendingBackBufferClears--;
			}

			for (int32_t bucket = 4095; bucket >= 0; bucket--)
			{
				SoftwareRenderItem* item = g_softwareRenderBuckets[bucket];
				while (item != NULL)
				{
					uint16_t flags = item->renderFlags;
					SoftwareRenderCallback callback = NULL;
					if (flags & 0x8000)
					{
						callback = g_softwareRenderDispatch->highPriority;
					}
					else if (flags & 0x80)
					{
						g_softwareRenderDispatch->flag80(item);
					}
					else
					{
						int32_t kind = (flags >> 9) & 3;
						if ((flags & SOFTWARE_RENDER_SUBTRACTIVE) && g_softwareRenderDispatch->subtractive != NULL)
						{
							callback = g_softwareRenderDispatch->subtractive;
						}
						else if ((flags & SOFTWARE_RENDER_ADDITIVE) && g_softwareRenderDispatch->additive != NULL)
						{
							callback = g_softwareRenderDispatch->additive;
						}
						else if ((flags & SOFTWARE_RENDER_COLOUR_OFFSET) && g_softwareRenderDispatch->colourOffset[kind] != NULL)
						{
							callback = g_softwareRenderDispatch->colourOffset[kind];
						}
						else
						{
							callback = g_softwareRenderDispatch->defaultCallback[kind];
						}
					}
					if (callback != NULL)
					{
						callback(item);
					}
					item = item->next;
				}
			}
		}

		result = d3dappi.lpBackBuffer->Unlock(NULL);
		g_lockedBackBuffer = NULL;
		if (result != DD_OK)
		{
			Logger::Log("SOFT : ERROR - Failed to unlock back buffer - %s.\n", D3DAppErrorToString(result));
		}
		D3DAppShowBackBuffer();
	}

	// Queues a transformed triangle for sorted (back-to-front) transparency
	// rasterization. Claims a slot from Renderer::g_primitiveBuffer, copies the
	// three transformed vertices and the per-call flags, derives the depth key
	// as the minimum vertex z scaled into the 1024-entry bucket range, and
	// inserts the slot into the Renderer::g_renderBuckets[depthKey & 0x3ff]
	// singly-linked list kept in descending depthKey order so the drain pass
	// renders farthest triangles first.
	//
	// The two early-out guards are written as separate sequential returns
	// (not a single &&) so the callee-saved register pushes are deferred past
	// them, matching retail (FIX-14). The depth key uses a MIN macro with no
	// intermediate local so MSVC keeps the candidate on the FPU stack and
	// recomputes the inner min in the else branch (FIX-14); the inline nested
	// ternary lowers to FCOMP-from-memory instead of retail's FLD/FCOMPP. The
	// for(;;) walk lowers to retail's single-body rotated loop, and the
	// splice/set-head if/else shares the record->next store rather than an
	// early return (an early return forces eager callee-saved pushes).
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

	// Submits a triangle list (indexCount/3 independent triangles) for sorted
	// transparency rasterization. Locks the vertex buffer, walks the index list
	// back-to-front in groups of three, and forwards each triangle to
	// SubmitSortedTriangle with textureIndex=0 (SubmitQuad instead passes textureIndex=
	// texture index and no render entry. The indexed submitters retain the render entry
	// and use texture index zero. The index buffer is read
	// via a pointer centered on the middle index of each triple so the three
	// vertex pointers come from p[-1], p[0], p[1] as p walks backward. The
	// count guard is written as an explicit if around a do-while so the pointer
	// setup (the indices load and the LEA) is deferred past the guard, matching
	// retail's callee-saved register scheduling; a plain while hoists the
	// pointer init before the guard.
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

	// Submits a triangle strip (indexCount-2 triangles) for sorted transparency
	// rasterization. Locks the vertex buffer, emits the first triangle from
	// indices[0..2], then walks the remaining indices keeping a rolling triple of
	// the last three indices (a,b,c) and flipping the vertex order on odd
	// iterations to preserve strip winding. It forwards texture index zero and the render entry to
	// SubmitSortedTriangle (same slot assignment as SubmitTriangleList, opposite
	// to SubmitQuad). Residual ~3% is CAP-17: MSVC hoists the loop-invariant
	// `remaining = indexCount - 3` init as `LEA EBX,[edx-3]` with an early count
	// load, where retail loads count into EBX late and SUBtracts in place.
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

	// Submits a triangle strip (indexCount-2 triangles) for sorted transparency
	// rasterization using an already-locked vertex buffer. The caller (RenderType8)
	// locks the vertex buffer and passes the locked base pointer directly, so this
	// variant performs no Lock/Unlock. It uses the same strip logic, render entry, and zero texture-index
	// assignment as SubmitTriangleStrip: emit the first triangle from
	// indices[0..2], then walk the remaining indices keeping a rolling triple
	// (a,b,c) and flipping the vertex order on odd iterations to preserve strip
	// winding.
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

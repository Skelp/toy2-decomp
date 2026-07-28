#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Toy2.h"
#include "Toy2/D3DApp.h"
#include "Nu3D/BmpDataNode.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Logger.h"
#include <stdlib.h>
#include <math.h>

namespace SoftwareRenderer
{
	// GLOBAL: TOY2 0x004F7400
	PointI g_unk4F7400 = { -32768, -32768 };

	// GLOBAL: TOY2 0x004F73A8
	int32_t g_backdropWidth = 0;

	// GLOBAL: TOY2 0x004F73D4
	int32_t g_staticBackdropWidth = 0;

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

	// GLOBAL: TOY2 0x00830C60
	int32_t g_unk830C60;

	// GLOBAL: TOY2 0x00559C40
	int32_t g_unk559C40;

	// GLOBAL: TOY2 0x00839278
	int32_t g_unk839278;

	// Active base of the 4096 software-render depth buckets.
	// GLOBAL: TOY2 0x00504D34
	SoftwareRenderItem** g_softwareRenderBuckets;

	// GLOBAL: TOY2 0x0087E50C
	SoftwareRenderItem* g_softwareRenderBucketStorage[4096];

	// GLOBAL: TOY2 0x00839280
	int32_t g_unk839280;

	// GLOBAL: TOY2 0x00E4D950
	int32_t g_softwarePrimitiveType;

	// GLOBAL: TOY2 0x009F6008
	int32_t g_unk9F6008;

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
	int32_t g_unkA4CC80;

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
	// state, and that UnkFunc57 and UnkFunc58 then call once per scanline. The
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
	int32_t g_unk9F6010;

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

	// The software queue uses a linked item header. The render payload before
	// this header depends on the item kind selected by renderFlags.
	struct SoftwareRenderItem
	{
		uint8_t payload[0x70];
		SoftwareRenderItem* next;
		uint16_t renderFlags;
	};

	STATIC_ASSERT(offsetof(SoftwareRenderItem, next) == 0x70);
	STATIC_ASSERT(offsetof(SoftwareRenderItem, renderFlags) == 0x74);

	typedef void (*SoftwareRenderCallback)(SoftwareRenderItem* item);

	struct SoftwareRenderDispatchTable
	{
		SoftwareRenderCallback highPriority;
		SoftwareRenderCallback unused04;
		SoftwareRenderCallback flag20;
		SoftwareRenderCallback flag1000;
		SoftwareRenderCallback flag40[4];
		SoftwareRenderCallback defaultCallback[4];
		SoftwareRenderCallback flag80;
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

	// FUNCTION: TOY2 0x004B2B80
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

	// FUNCTION: TOY2 0x00452130
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

	// FUNCTION: TOY2 0x004C1C40
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

	// FUNCTION: TOY2 0x004BCE00
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

	// FUNCTION: TOY2 0x004C1E60
	void InitialisePrimarySurface_T() { InitialisePrimarySurface(); }

	// STUB: TOY2 0x0040CD80
	void ShowBackBuffer() {}

	// FUNCTION: TOY2 0x0047D0F0
	void Destroy()
	{
		Logger::Log("QUIT : Destroying software renderer.\n");
		if (g_softwareRendererBuffer)
		{
			free(g_softwareRendererBuffer);
		}
	}

	// FUNCTION: TOY2 0x004C1E70
	void CommitZoom()
	{
		g_topOffsetF = (float)g_topOffset;
		g_leftOffsetF = (float)g_leftOffset;
		g_spanScaleV = (float)((g_bottomOffset - g_topOffset) + 1) * k_vSpanScale;
		g_spanScaleH = (float)((g_rightOffset - g_leftOffset) + 1) * k_hSpanScale;
		g_zoomScaleV = (float)g_zoomExtentV / g_screenDimV;
		g_zoomScaleH = (float)g_zoomExtentH / g_screenDimH;
	}

	// FUNCTION: TOY2 0x004C1FC0
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

	// FUNCTION: TOY2 0x004C1F00
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

	// FUNCTION: TOY2 0x004C17B0
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
		do
		{
			*reinterpret_cast<uint32_t*>(backRow) = 0;
			backRow += sizeof(uint32_t);
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
		do
		{
			*reinterpret_cast<uint32_t*>(backRow) = 0;
			backRow += sizeof(uint32_t);
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

	// FUNCTION: TOY2 0x00490410
	void UnkFunc67(int32_t x, int32_t y)
	{
		int32_t* piPitch = Toy2::g_hasStaticBackdrop ? &g_staticBackdropWidth : &g_backdropWidth;
		int32_t pitch = *piPitch;
		int32_t quotient = x / pitch;
		int32_t remainder = x - quotient * pitch;
		if (remainder < 0)
		{
			g_unk4F7400.x = (1 - quotient) * pitch + x;
			g_unk4F7400.y = y;
			return;
		}
		g_unk4F7400.x = remainder;
		g_unk4F7400.y = y;
	}

	// STUB: TOY2 0x0048FB70
	void UnkFunc2() {}

	// GLOBAL: TOY2 0x00547EE2
	int16_t g_backdropCameraYaw;

	// FUNCTION: TOY2 0x00490290
	int16_t UpdateBackdropScroll()
	{
		volatile int32_t clampedVisibleHeight;
		int32_t hasStaticBackdrop = Toy2::g_hasStaticBackdrop;
		int32_t* backdropWidth = hasStaticBackdrop ? &g_staticBackdropWidth : &g_backdropWidth;
		int32_t cameraYaw = g_backdropCameraYaw;
		int32_t textureColumn;

		if (g_backdropTextureColumn == -1)
		{
			cameraYaw = -cameraYaw;
			g_previousBackdropYaw = cameraYaw;
			g_backdropYawAccumulator = cameraYaw;
			textureColumn = (cameraYaw * 2240 / 8192) & 0xff;
			g_backdropScrollX = textureColumn % *backdropWidth;
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

			g_backdropScrollX = (g_backdropScrollX + scrollDelta) % *backdropWidth;
			while (g_backdropScrollX < 0)
				g_backdropScrollX += *backdropWidth;
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

		if (g_unk4F7400.x != -0x8000)
			g_backdropScrollX = g_unk4F7400.x;

		int32_t backdropHeight = backdropWidth[1];
		if (clippedTop <= backdropHeight && clippedTop >= 0 && g_backdropViewDepth > 0x800)
		{
			backdropHeight -= clippedTop;
			if (Toy2::g_levelFileIndex != 0 && ! hasStaticBackdrop && backdropHorizon + backdropHeight > 0xf0)
				clampedVisibleHeight = 0xf0 - backdropHorizon;
		}

		return 1;
	}

	// FUNCTION: TOY2 0x004BCAD0
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

	// FUNCTION: TOY2 0x004BCBE0 [MATCHED]
	void FlushRenderCommands()
	{
		for (int i = 0; i < g_renderCommandCount; i++)
		{
			RenderCommand& command = g_renderCommands[i];
			RasterizeRenderCommand(&command, command.vertexCount, command.renderState, command.texData, command.useAlternateSpans);
		}
		g_unkA4CC80 = 0;
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

	// FUNCTION: TOY2 0x004BC980
	void QueueSortedRenderCommand(Nu3D::VertexTL* vertices[4], int32_t vertexCount, uint32_t* texData, int32_t renderState, int32_t bucketGroup)
	{
		if (g_sortedRenderCommandCount >= 15000)
		{
			return;
		}

		float sortDepth = g_sortDepth;
		int32_t bucket;
		if (g_unk9F6008 == 1)
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

	// FUNCTION: TOY2 0x004BCFA0
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

	// FUNCTION: TOY2 0x0047C800
	void LockBackBuffer()
	{
		DDSURFACEDESC surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);

		HRESULT result;
		do
		{
			result = D3DApp::g_d3dAppI.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (result == DDERR_WASSTILLDRAWING);

		if (result == DD_OK)
		{
			g_lockedBackBuffer = surfaceDesc.lpSurface;
			return;
		}

		g_lockedBackBuffer = NULL;
		Logger::Log("SOFT : ERROR - Failed to lock back buffer - %s.\n", Logger::ErrorToMessage(result));
	}

	// FUNCTION: TOY2 0x0047C870 [MATCHED]
	void UnlockBackBuffer()
	{
		HRESULT result = D3DApp::g_d3dAppI.lpBackBuffer->Unlock(NULL);
		g_lockedBackBuffer = NULL;
		if (result != DD_OK)
		{
			Logger::Log("SOFT : ERROR - Failed to unlock back buffer - %s.\n", Logger::ErrorToMessage(result));
		}
	}

	// FUNCTION: TOY2 0x004C4640
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

	// STUB: TOY2 0x004C4370
	void UnkFunc48(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue)
	{}

	// Untextured opaque span for a 16-bit 555 surface. The rasterizer writes two
	// pixels at a time with one interpolated colour. It writes a single pixel at
	// each unaligned end of the span.
	// FUNCTION: TOY2 0x004C48E0
	void UnkFunc55(Nu3D::VertexTL* edgeA,
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

	// The 565 twin of UnkFunc55. It uses the same paired-pixel walk, but places
	// the five interpolated green bits at bit 6 and red at bit 11.
	// FUNCTION: TOY2 0x004C4A60
	void UnkFunc40(Nu3D::VertexTL* edgeA,
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
	// FUNCTION: TOY2 0x004C4BE0
	void UnkFunc49(Nu3D::VertexTL* edgeA,
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
	// FUNCTION: TOY2 0x004C4E00
	void UnkFunc52(Nu3D::VertexTL* edgeA,
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

	// The 16-bit 565 twin of UnkFunc52. Identical additive span; only the channel
	// positions move. Green starts at bit 6 and red at bit 11, and the rasterizer
	// still takes five bits per channel, so it uses the high five bits of the
	// six-bit green field.
	// FUNCTION: TOY2 0x004C4F30
	void UnkFunc36(Nu3D::VertexTL* edgeA,
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

	// The 565 twin of UnkFunc49. It uses the same texture sampling and additive
	// blend, but reads green at bit 6 and red at bit 11.
	// FUNCTION: TOY2 0x004C5060
	void UnkFunc41(Nu3D::VertexTL* edgeA,
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
	// FUNCTION: TOY2 0x004C5280
	void UnkFunc50(Nu3D::VertexTL* edgeA,
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
	// FUNCTION: TOY2 0x004C5490
	void UnkFunc53(Nu3D::VertexTL* edgeA,
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

	// The 16-bit 565 twin of UnkFunc53. Identical subtractive span; only the
	// channel positions move. Green starts at bit 6 and red at bit 11, and the
	// rasterizer still takes five bits per channel, so it uses the high five bits
	// of the six-bit green field.
	// FUNCTION: TOY2 0x004C55B0
	void UnkFunc37(Nu3D::VertexTL* edgeA,
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

	// The 565 twin of UnkFunc50. It uses the same texture sampling and
	// subtractive blend, but reads green at bit 6 and red at bit 11.
	// FUNCTION: TOY2 0x004C56D0
	void UnkFunc42(Nu3D::VertexTL* edgeA,
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
	// FUNCTION: TOY2 0x004C58E0
	void UnkFunc43(Nu3D::VertexTL* edgeA,
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

	// STUB: TOY2 0x004C5AC0
	void UnkFunc51(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue)
	{}

	// Blends an untextured span with a 555 destination. The source and
	// destination factors are in g_spanAlpha and g_spanInvAlpha.
	// FUNCTION: TOY2 0x004C5D80
	void UnkFunc56(Nu3D::VertexTL* edgeA,
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

	// The alternate-format twin of UnkFunc56. It uses the same blend and
	// interpolation, but extracts and packs channels for the other surface mode.
	// FUNCTION: TOY2 0x004C5F00
	void UnkFunc38(Nu3D::VertexTL* edgeA,
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

	// STUB: TOY2 0x004C6080
	void UnkFunc44(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue)
	{}

	// Whole-primitive rasterizers for an untextured quad.

	// STUB: TOY2 0x004C80D0
	void UnkFunc54(RenderCommand* command) {}

	// STUB: TOY2 0x004C8930
	void UnkFunc39(RenderCommand* command) {}

	// The primitive walkers: a textured quad, then the triangle and quad span
	// loops that call g_spanRasterizer once per scanline.

	// STUB: TOY2 0x004C6340
	void UnkFunc57(RenderCommand* command, uint32_t* texData) {}

	// STUB: TOY2 0x004C6B80
	void UnkFunc46(RenderCommand* command, uint32_t* texData) {}

	// STUB: TOY2 0x004C7630
	void UnkFunc45(RenderCommand* command, uint32_t* texData) {}

	// STUB: TOY2 0x004C9190
	void UnkFunc58(RenderCommand* command, uint32_t* texData) {}

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
					g_spanRasterizer = UnkFunc52;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
				{
					g_spanRasterizer = UnkFunc53;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
				{
					g_spanAlpha = command->vertices[0].diffuse.value >> 24;
					if (g_spanAlpha == 255)
					{
						g_spanRasterizer = UnkFunc55;
					}
					else
					{
						g_spanRasterizer = UnkFunc56;
						g_spanInvAlpha = 255 - g_spanAlpha;
					}
				}
				else if (commandVertexCount == 4)
				{
					UnkFunc54(command);
					return;
				}
				else
				{
					g_spanRasterizer = UnkFunc55;
				}
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
			{
				g_spanRasterizer = UnkFunc49;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = UnkFunc50;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = UnkFunc43;
				}
				else
				{
					g_spanRasterizer = UnkFunc51;
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
				g_spanRasterizer = UnkFunc36;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = UnkFunc37;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = UnkFunc40;
				}
				else
				{
					g_spanRasterizer = UnkFunc38;
					g_spanInvAlpha = 255 - g_spanAlpha;
				}
			}
			else if (commandVertexCount == 4)
			{
				UnkFunc39(command);
				return;
			}
			else
			{
				g_spanRasterizer = UnkFunc40;
			}
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
		{
			g_spanRasterizer = UnkFunc41;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
		{
			g_spanRasterizer = UnkFunc42;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
		{
			g_spanAlpha = command->vertices[0].diffuse.value >> 24;
			if (g_spanAlpha == 255)
			{
				g_spanRasterizer = UnkFunc43;
			}
			else
			{
				g_spanRasterizer = UnkFunc44;
				g_spanInvAlpha = 255 - g_spanAlpha;
			}
		}
		else
		{
		textured:
			if (commandVertexCount == 4)
			{
				UnkFunc46(command, commandTexData);
				return;
			}
			g_spanRasterizer = UnkFunc48;
		}

		if (commandVertexCount == 3)
		{
			UnkFunc57(command, commandTexData);
			return;
		}
		UnkFunc58(command, commandTexData);
	}

	// FUNCTION: TOY2 0x004BCC40
	void ResetRenderCommands()
	{
		if (g_unkA4CC80 == 0)
		{
			for (int i = 0; i < 30000; i++)
			{
				g_sortedRenderBuckets[i] = NULL;
			}
			g_sortedRenderCommandCount = 0;
			g_renderCommandCount = 0;
		}
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
					g_spanRasterizer = UnkFunc52;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
				{
					g_spanRasterizer = UnkFunc53;
				}
				else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
				{
					g_spanAlpha = command->vertices[0].diffuse.value >> 24;
					if (g_spanAlpha == 255)
					{
						g_spanRasterizer = UnkFunc55;
					}
					else
					{
						g_spanRasterizer = UnkFunc56;
						g_spanInvAlpha = 255 - g_spanAlpha;
					}
				}
				else if (commandVertexCount == 4)
				{
					UnkFunc54(command);
					return;
				}
				else
				{
					g_spanRasterizer = UnkFunc55;
				}
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
			{
				g_spanRasterizer = UnkFunc49;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = UnkFunc50;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = UnkFunc43;
				}
				else
				{
					g_spanRasterizer = UnkFunc51;
					g_spanInvAlpha = 255 - g_spanAlpha;
				}
			}
			else
			{
				if (commandVertexCount == 4)
				{
					if (commandUseAlternateSpans != 0)
					{
						UnkFunc45(command, commandTexData);
					}
					else
					{
						UnkFunc46(command, commandTexData);
					}
					return;
				}
				if (commandUseAlternateSpans != 0)
				{
					g_spanRasterizer = RasterizeTexturedSpanPairSample;
				}
				else
				{
					g_spanRasterizer = UnkFunc48;
				}
			}
		}
		else if (commandTexData == NULL)
		{
			if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
			{
				g_spanRasterizer = UnkFunc36;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
			{
				g_spanRasterizer = UnkFunc37;
			}
			else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
			{
				g_spanAlpha = command->vertices[0].diffuse.value >> 24;
				if (g_spanAlpha == 255)
				{
					g_spanRasterizer = UnkFunc40;
				}
				else
				{
					g_spanRasterizer = UnkFunc38;
					g_spanInvAlpha = 255 - g_spanAlpha;
				}
			}
			else if (commandVertexCount == 4)
			{
				UnkFunc39(command);
				return;
			}
			else
			{
				g_spanRasterizer = UnkFunc40;
			}
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_CUSTOM)
		{
			g_spanRasterizer = UnkFunc41;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_ALT)
		{
			g_spanRasterizer = UnkFunc42;
		}
		else if (commandRenderState & Renderer::RENDER_ALPHA_DEFAULT)
		{
			g_spanAlpha = command->vertices[0].diffuse.value >> 24;
			if (g_spanAlpha == 255)
			{
				g_spanRasterizer = UnkFunc43;
			}
			else
			{
				g_spanRasterizer = UnkFunc44;
				g_spanInvAlpha = 255 - g_spanAlpha;
			}
		}
		else
		{
			if (commandVertexCount == 4)
			{
				if (commandUseAlternateSpans != 0)
				{
					UnkFunc45(command, commandTexData);
				}
				else
				{
					UnkFunc46(command, commandTexData);
				}
				return;
			}
			if (commandUseAlternateSpans != 0)
			{
				g_spanRasterizer = RasterizeTexturedSpanPairSample;
			}
			else
			{
				g_spanRasterizer = UnkFunc48;
			}
		}

		if (commandVertexCount == 3)
		{
			UnkFunc57(command, commandTexData);
			return;
		}
		UnkFunc58(command, commandTexData);
	}

	// FUNCTION: TOY2 0x004BCB60 [MATCHED]
	void FlushSortedRenderCommands()
	{
		if (g_unkA4CC80 == 1)
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
		g_unkA4CC80++;
	}

	// FUNCTION: TOY2 0x00470BF0 [MATCHED]
	void SetPaletteOnAPI()
	{
		HRESULT result = D3DApp::g_d3dAppI.lpDD->CreatePalette(0x44, (LPPALETTEENTRY)g_paletteEntries, &g_lpPalette, NULL);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpDD->CreatePalette(0x00000004l|0x00000040l,&pal[0],&SonicRPalette,0)", result);
		}
		result = D3DApp::g_d3dAppI.lpFrontBuffer->SetPalette(g_lpPalette);
		if (result < 0)
		{
			Logger::LogDDError("d3dappi.lpFrontBuffer->SetPalette(SonicRPalette)", result);
		}
		result = D3DApp::g_d3dAppI.lpBackBuffer->SetPalette(g_lpPalette);
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

	// FUNCTION: TOY2 0x00470D00
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

	// FUNCTION: TOY2 0x004319E0
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

	// FUNCTION: TOY2 0x00470D60
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

	// FUNCTION: TOY2 0x00471190
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

	// FUNCTION: TOY2 0x00471250
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

	// FUNCTION: TOY2 0x004710C0
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

	// FUNCTION: TOY2 0x00470FB0
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
	int32_t UnkFunc20(TextureData* out)
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
	void UnkFunc18(float* primaryDistance, float* secondaryDistance)
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
	// FUNCTION: TOY2 0x004BCC70
	void UnkFunc17(int32_t top, int32_t bottom, int32_t left, int32_t right)
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

	// Software triangle-list rasterizer dispatch. Resolves the current bound
	// texture via UnkFunc20, groups the index stream into triples (one per
	// triangle), and forwards each triple to the inner rasterizer UnkFunc22
	// with the masked texture pointer (NULL when no texture is bound). Each
	// index addresses a 32-byte VertexTL into lpvVertices.
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
	// FUNCTION: TOY2 0x004C14A0
	void UnkFunc19(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags)
	{
		TextureData tex;
		int32_t noTexture = UnkFunc20(&tex);
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

	// FUNCTION: TOY2 0x004C1540
	void UnkFunc21(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags)
	{
		TextureData texture;
		uint32_t* texData = UnkFunc20(&texture) != 0 ? NULL : texture.texData;
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
	void UnkFunc16(D3DPRIMITIVETYPE d3dptPrimitiveType, LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags)
	{
		UnkFunc18(&g_primaryRenderDistance, &g_secondaryRenderDistance);
		switch (d3dptPrimitiveType)
		{
			case D3DPT_TRIANGLELIST:
				UnkFunc19(lpvVertices, lpwIndices, dwIndexCount, dwFlags);
				break;
			case D3DPT_TRIANGLESTRIP:
				UnkFunc21(lpvVertices, lpwIndices, dwIndexCount, dwFlags);
				break;
		}
		g_unk9F6008 = 0;
	}

	// Locks the DirectDraw back buffer, clears it when the frame state requires
	// a clear, drains all software-render depth buckets from far to near, then
	// unlocks and presents the surface. The caller passes the highest active
	// bucket and a zero clear value, but retail does not read either parameter.
	// FUNCTION: TOY2 0x0047D210
	void UnkFunc8(int32_t highestBucket, int32_t clearValue)
	{
		DDSURFACEDESC surfaceDesc;
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);

		HRESULT result;
		do
		{
			result = D3DApp::g_d3dAppI.lpBackBuffer->Lock(NULL, &surfaceDesc, 0, NULL);
		} while (result == DDERR_WASSTILLDRAWING);

		if (result == DD_OK)
		{
			g_lockedBackBuffer = surfaceDesc.lpSurface;
		}
		else
		{
			g_lockedBackBuffer = NULL;
			Logger::Log("SOFT : ERROR - Failed to lock back buffer - %s.\n", Logger::ErrorToMessage(result));
		}

		if (Nu3D::Camera::g_cameraTintBlue != 0x80 && D3DApp::g_renderMode == RENDERMODE_SOFTWARE && g_bitsPerPixel != 8)
		{
			int32_t rowSkip = (g_backBufferPitchPixels - Toy2::g_destRectWidth) / 2;
			int32_t rowWidth = Toy2::g_destRectWidth / 2;
			uint32_t* pixel = (uint32_t*)g_lockedBackBuffer;
			for (int32_t row = Toy2::g_destRectHeight; row != 0; row--)
			{
				for (int32_t count = rowWidth; count != 0; count--)
				{
					*pixel++ = 0;
				}
				pixel = (uint32_t*)((uint8_t*)pixel + rowSkip);
			}
		}
		else
		{
			if (g_pendingBackBufferClears != 0 && g_unk830C60 == 0)
			{
				uint32_t* pixel = (uint32_t*)g_lockedBackBuffer;
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
						pixel = (uint32_t*)((uint8_t*)pixel + rowSkip);
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
						pixel = (uint32_t*)((uint8_t*)pixel + rowSkip);
					}
				}
				g_unk830C60 = 1;
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
						if ((flags & 0x1000) && g_softwareRenderDispatch->flag1000 != NULL)
						{
							callback = g_softwareRenderDispatch->flag1000;
						}
						else if ((flags & 0x20) && g_softwareRenderDispatch->flag20 != NULL)
						{
							callback = g_softwareRenderDispatch->flag20;
						}
						else if ((flags & 0x40) && g_softwareRenderDispatch->flag40[kind] != NULL)
						{
							callback = g_softwareRenderDispatch->flag40[kind];
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

		result = D3DApp::g_d3dAppI.lpBackBuffer->Unlock(NULL);
		g_lockedBackBuffer = NULL;
		if (result != DD_OK)
		{
			Logger::Log("SOFT : ERROR - Failed to unlock back buffer - %s.\n", Logger::ErrorToMessage(result));
		}
		ShowBackBuffer();
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
		if (g_unk9F6010 != 0)
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
	// FUNCTION: TOY2 0x004B6040
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
			SoftwareRenderer::UnkFunc20(&scratch);
			if (SoftwareRenderer::g_viewportRect != NULL)
			{
				Nu3D::Viewport::ViewportRect* rect = SoftwareRenderer::g_viewportRect;
				SoftwareRenderer::UnkFunc17((int32_t)rect->top, (int32_t)rect->bottom, (int32_t)rect->left, (int32_t)rect->right);
			}
			SoftwareRenderer::UnkFunc16(primitiveType, lockedVertices, indices, indexCount, flags);
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
		SoftwareRenderer::UnkFunc20(&scratch);
		if (SoftwareRenderer::g_viewportRect != NULL)
		{
			Nu3D::Viewport::ViewportRect* rect = SoftwareRenderer::g_viewportRect;
			SoftwareRenderer::UnkFunc17((int32_t)rect->top, (int32_t)rect->bottom, (int32_t)rect->left, (int32_t)rect->right);
		}
		SoftwareRenderer::UnkFunc16(d3dptPrimitiveType, lpvVertices, lpwIndices, dwIndexCount, dwFlags);
		return 0;
	}

	// Vertex Methods

	// FUNCTION: TOY2 0x004B2B20
	HRESULT ReleaseVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer)
	{
		free(buffer);
		return 0;
	}

	// FUNCTION: TOY2 0x004B2B30
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

	// FUNCTION: TOY2 0x004B2BB0
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

	// FUNCTION: TOY2 0x004C19E0
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
		SoftwareRenderer::UnkFunc18(&SoftwareRenderer::g_primaryRenderDistance, &SoftwareRenderer::g_secondaryRenderDistance);

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

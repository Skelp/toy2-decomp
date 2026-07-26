#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "Toy2/MainMenu.h"
#include "Toy2/Toy2.h"
#include "Nu3D/BmpDataNode.h"
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
	int32_t g_unk500A1C = 0xFFFFFFFF;

	// GLOBAL: TOY2 0x00830C60
	int32_t g_unk830C60;

	// GLOBAL: TOY2 0x00559C40
	int32_t g_unk559C40;

	// GLOBAL: TOY2 0x00839278
	void* g_unk839278;

	// GLOBAL: TOY2 0x00504D34
	void* g_unk504D34;

	// GLOBAL: TOY2 0x00839280
	int32_t g_unk839280;

	// GLOBAL: TOY2 0x00E4D950
	int32_t g_unkE4D950;

	// GLOBAL: TOY2 0x009F6008
	int32_t g_unk9F6008;

	// Render-queue clear/reset state shared by the UnkFunc31/32/33 cluster
	// and the FUN_004bc980 / FUN_004bcad0 helpers: g_unkA4CC80 is an active
	// flag, g_unkB626E0 a 30000-dword buffer, g_unkDBB094 and g_unkDE20A8 are
	// counts. UnkFunc32 clears the buffer and resets the counts when inactive.
	// GLOBAL: TOY2 0x00A4CC80
	int32_t g_unkA4CC80;

	// Reset by UnkFunc33 after the sorted bucket walk. g_unkB626C0 sits just
	// below the bucket array; g_unkB7FBB8 sits just below g_clipLeft. Roles
	// not yet fully understood.
	// GLOBAL: TOY2 0x00B626C0
	int32_t g_unkB626C0;

	// GLOBAL: TOY2 0x00B626E0
	int32_t g_unkB626E0[30000];

	// GLOBAL: TOY2 0x00DBB094
	int32_t g_unkDBB094;

	// GLOBAL: TOY2 0x00DE20A8
	int32_t g_unkDE20A8;

	// A queued render command for the software rasterizer. UnkFunc29 enqueues
	// transformed vertices (3 for a triangle, 4 for a quad when vertexCount is
	// 4) and UnkFunc35 dequeues and rasterizes one. Stride 0x9C, capacity 1024
	// (g_unkDE20A8 is the live count). The same struct is reused by the sorted
	// path: UnkFunc33 walks the g_unkB626E0 depth buckets and threads nodes
	// through the next pointer (+0x8C), calling UnkFunc34 per node. The
	// metadata at +0x80 is only partially understood; refine the names when
	// UnkFunc29 and UnkFunc35 are reconstructed.
	struct RenderCommand
	{
		Nu3D::VertexTL vertices[4]; // +0x00
		int32_t field80; // +0x80 (mode/flags read by UnkFunc35)
		int32_t vertexCount; // +0x84 (3 = triangle, 4 = quad)
		int32_t field88; // +0x88
		RenderCommand* next; // +0x8C (sorted-path list link, read by UnkFunc33)
		int32_t field90; // +0x90
		int32_t field94; // +0x94 (passed to UnkFunc35)
		int32_t field98; // +0x98
	};

	// GLOBAL: TOY2 0x00DBB0A0
	RenderCommand g_renderQueue[1024];

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

	// GLOBAL: TOY2 0x00B7FBBC
	int32_t g_clipLeft;

	// GLOBAL: TOY2 0x00DE20B8
	int32_t g_clipRight;

	// GLOBAL: TOY2 0x00DE20A4
	int32_t g_clipTop;

	// GLOBAL: TOY2 0x00DBB088
	int32_t g_clipBottom;

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

	// GLOBAL: TOY2 0x00B626BC
	float g_spanScaleH;

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

	// STUB: TOY2 0x004C17B0
	void PresentFrame() {}

	// FUNCTION: TOY2 0x00490410
	void UnkFunc67(int32_t param1, int32_t param2)
	{
		int32_t* piPitch = Toy2::g_hasStaticBackdrop ? &g_staticBackdropWidth : &g_backdropWidth;
		int32_t pitch = *piPitch;
		int32_t quotient = param1 / pitch;
		int32_t remainder = param1 - quotient * pitch;
		if (remainder < 0)
		{
			g_unk4F7400.x = (1 - quotient) * pitch + param1;
			g_unk4F7400.y = param2;
			return;
		}
		g_unk4F7400.x = remainder;
		g_unk4F7400.y = param2;
	}

	// STUB: TOY2 0x0048FB70
	void UnkFunc2() {}

	// STUB: TOY2 0x00490290
	int16_t UnkFunc3() { return 0; }

	// FUNCTION: TOY2 0x004BCAD0
	void UnkFunc29(Nu3D::VertexTL* vertices[4], int32_t vertexCount, int32_t field80, int32_t field88)
	{
		if (g_unkDE20A8 < 0x400)
		{
			RenderCommand* command = &g_renderQueue[g_unkDE20A8];
			g_unkDE20A8++;
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
			command->field80 = field80;
			command->field88 = field88;
		}
	}

	// FUNCTION: TOY2 0x004BCBE0 [MATCHED]
	void UnkFunc31()
	{
		for (int i = 0; i < g_unkDE20A8; i++)
		{
			RenderCommand& command = g_renderQueue[i];
			UnkFunc35(&command, command.vertexCount, command.field88, command.field80, command.field94);
		}
		g_unkA4CC80 = 0;
	}

	// STUB: TOY2 0x004C9D00
	void UnkFunc35(RenderCommand* command, int32_t vertexCount, int32_t field88, int32_t field80, int32_t field94) {}

	// FUNCTION: TOY2 0x004BCC40
	void UnkFunc32()
	{
		if (g_unkA4CC80 == 0)
		{
			for (int i = 0; i < 30000; i++)
			{
				g_unkB626E0[i] = 0;
			}
			g_unkDBB094 = 0;
			g_unkDE20A8 = 0;
		}
	}

	// STUB: TOY2 0x004C9A50
	void UnkFunc34(RenderCommand* command, int32_t vertexCount, int32_t field88, int32_t field80, int32_t field94) {}

	// FUNCTION: TOY2 0x004BCB60 [MATCHED]
	void UnkFunc33()
	{
		if (g_unkA4CC80 == 1)
		{
			for (int i = 29999; i >= 0; i--)
			{
				RenderCommand* command = (RenderCommand*)g_unkB626E0[i];
				while (command != NULL)
				{
					UnkFunc34(command, command->vertexCount, command->field88, command->field80, command->field94);
					command = command->next;
				}
			}
		}
		g_unkB7FBB8 = 0;
		g_unkB626C0 = 0;
		g_unkA4CC80++;
	}

	// STUB: TOY2 0x00470C70
	void UnkFunc7() {}

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

	// STUB: TOY2 0x004BCC70
	void UnkFunc17(int32_t top, int32_t bottom, int32_t left, int32_t right) {}

	// STUB: TOY2 0x004C14A0
	void UnkFunc19(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags) {}

	// STUB: TOY2 0x004C1540
	void UnkFunc21(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags) {}

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

	// STUB: TOY2 0x0047D210
	void UnkFunc8(void* param1, int32_t param2) {}

	// STUB: TOY2 0x004B5E40
	void SubmitSortedTriangle(int32_t renderFlags, int32_t field10, int32_t fieldC, Nu3D::VertexTL* v0, Nu3D::VertexTL* v1, Nu3D::VertexTL* v2) {}

	// FUNCTION: TOY2 0x004B6220 [MATCHED]
	void SubmitQuad(int32_t renderFlags, int32_t textureIndex, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices)
	{
		Nu3D::VertexTL* lockedVertices;
		if (DrawingAPI::LockVertexBuffer(vertexBuffer, 0x801, (LPVOID*)&lockedVertices, 0) == 0)
		{
			SubmitSortedTriangle(renderFlags, 0, textureIndex, &lockedVertices[indices[0]], &lockedVertices[indices[1]], &lockedVertices[indices[2]]);
			SubmitSortedTriangle(renderFlags, 0, textureIndex, &lockedVertices[2], &lockedVertices[indices[1]], &lockedVertices[indices[3]]);
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

	// STUB: TOY2 0x004B9950
	HRESULT DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE primitiveType, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices, DWORD indexCount, DWORD flags)
	{ return DDERR_UNSUPPORTED; }

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

	// STUB: TOY2 0x004C19E0
	HRESULT ProcessVerticesOnBuffer(LPDIRECT3DVERTEXBUFFER destBuffer,
		DWORD dwVertexOp,
		DWORD dwDestIndex,
		DWORD dwCount,
		LPDIRECT3DVERTEXBUFFER srcBuffer,
		DWORD dwSrcIndex,
		DWORD dwFlags)
	{ return DDERR_UNSUPPORTED; }
}

#pragma once

#include "Common.h"
#include "Numerics.h"
#include "Nu3D/Nu3D.h"
#include "Nu3D/Viewport.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

namespace Renderer
{
	struct RenderEntry;
}

namespace SoftwareRenderer
{
	extern PointI g_unk4F7400;
	extern int32_t g_backdropWidth;
	extern int32_t g_staticBackdropWidth;
	extern int32_t g_backdropTextureColumn;
	extern int32_t g_backBufferClearComplete;
	extern int32_t g_unk559C40;
	extern int32_t g_unk839278;
	struct SoftwareRenderItem;
	extern SoftwareRenderItem** g_softwareRenderBuckets;
	extern SoftwareRenderItem* g_softwareRenderBucketStorage[4096];
	extern int32_t g_unk839280;
	extern int32_t g_softwarePrimitiveType;
	extern int32_t g_unk9F6008;

	// The software renderer's DirectDraw palette and backing entry buffers.
	// SetPaletteOnAPI (0x00470BF0) creates the palette from g_paletteEntries and
	// attaches it to the front/back buffers; UpdatePaletteTint (0x00470C70) rebuilds the
	// live entries from source entries 1..255 and then calls SetEntries.
	// The palette is stored B,G,R,X per entry (byte 0 = blue, 1 = green, 2 = red).
	extern LPDIRECTDRAWPALETTE g_lpPalette;
	extern uint8_t g_paletteEntries[0x400];
	extern uint8_t* g_rgbToPaletteIndex;
	extern uint8_t g_paletteSource[0x400];
	extern uint8_t g_paletteLightingTable[128][256];
	extern uint8_t* g_additivePaletteTable;
	extern uint8_t* g_paletteColourOffsetTable;
	extern uint8_t* g_subtractivePaletteTable;
	extern uint8_t* g_paletteBlend25Table;
	extern uint8_t* g_paletteBlend50Table;
	extern uint8_t* g_paletteBlend75Table;
	extern Nu3D::Viewport::ViewportRect* g_viewportRect;
	extern int32_t g_softwareClearColor;
	extern int32_t g_leftOffset;
	extern int32_t g_rightOffset;
	extern int32_t g_topOffset;
	extern int32_t g_bottomOffset;
	// Integer clip rect, kept in sync with the viewport offsets above by
	// ZoomIn/ZoomOut/InitialisePrimarySurface. The polygon clipper clips
	// against these bounds.
	extern int32_t g_clipLeft;
	extern int32_t g_clipRight;
	extern int32_t g_clipTop;
	extern int32_t g_clipBottom;
	// Last viewport-rect values seen by UnkFunc17. Each clip edge is recomputed
	// only when its source rect field changes, so repeated draws with the same
	// viewport skip the float scaling work. Named for the ViewportRect field each
	// caches (see GetViewClipRect: the bottom field holds the left value).
	extern int32_t g_cachedViewportTop;
	extern int32_t g_cachedViewportBottom;
	extern int32_t g_cachedViewportLeft;
	extern int32_t g_cachedViewportRight;
	// Zoom step counter clamped to [0,10]; the zoomed source extents below are
	// adjusted by ZoomIn/ZoomOut and CommitZoom derives the render scale from
	// extent / screen dimension.
	extern int32_t g_zoomLevel;
	extern int32_t g_zoomExtentV;
	extern int32_t g_zoomExtentH;
	extern int32_t g_bitsPerPixel;
	extern float g_topOffsetF;
	extern float g_leftOffsetF;
	extern float g_spanScaleV;
	extern float g_spanScaleH;
	extern float g_zoomScaleV;
	extern float g_zoomScaleH;
	extern int32_t g_screenDimV;
	extern int32_t g_screenDimH;
	// Square roots of Renderer::g_primaryRenderDistanceSquared and
	// g_secondaryRenderDistanceSquared, refreshed by UnkFunc18 before each draw
	// dispatch. The software rasterizer culls vertices against these distances.
	extern float g_primaryRenderDistance;
	extern float g_secondaryRenderDistance;
	extern const double k_vSpanScale;
	extern const double k_hSpanScale;
	// Viewport-to-clip-rect scale factors used by UnkFunc17. The vertical scale
	// maps the rect's top/bottom edges through the screen height, and the
	// horizontal scale maps left/right through the screen width.
	extern const double k_viewportScaleV;
	extern const double k_viewportScaleH;
	// Scales the per-triangle minimum vertex z into the 1024-entry depth bucket
	// range used by SubmitSortedTriangle: depthKey = minZ * k_depthSortScale,
	// bucket = (int32_t) depthKey & 0x3ff.
	extern const float k_depthSortScale;
	// Gate read by SubmitSortedTriangle; when nonzero, triangle submission is
	// skipped. No writer has been located yet (likely zero for the retail path).
	extern int32_t g_unk9F6010;
	extern void* g_softwareRendererBuffer;
	extern LPVOID g_primarySurfacePtr;
	extern int32_t g_primarySurfacePitch;
	extern int32_t g_pixelFormatMode;
	extern void* g_backBuffer;
	extern void* g_colourScaleTables;
	extern uint16_t* g_colourScaleTable0;
	extern uint16_t* g_colourScaleTable1;
	extern uint16_t* g_colourScaleTable2;
	extern uint16_t* g_colourScaleTable3;
	extern uint8_t* g_currentRenderBuffer;
	extern uint8_t* g_renderBufferPixels;
	extern uint8_t g_renderBufferA[];
	extern uint8_t g_renderBufferB[];
	extern float g_cameraNearZ;
	extern float g_cameraFarZ;

	void SwapRenderBuffer();
	void SetLevelFileIndex(int32_t index);
	void InitialisePrimarySurface();
	void InitialisePrimarySurface_T();
	void Destroy();
	void ClearBackBufferOnce();
	void FillRect32(uint32_t* dest, int32_t width, int32_t height, int32_t rowPaddingBytes, uint32_t value);
	void CommitZoom();
	void InitialiseColourScaleTables();
	void ZoomOut();
	void ZoomIn();
	void PresentFrame();
	void ShowBackBuffer();
	void LockBackBuffer();
	void UnlockBackBuffer();
	void SetCameraNearFarZ(float nearZ, float farZ);
	void UnpackColourChannels(uint32_t colour, int32_t* red, int32_t* green, int32_t* blue);
	void UnpackColourToFloats(uint32_t colour, float* red, float* green, float* blue, uint32_t* alphaMask);
	void QueueSortedRenderCommand(Nu3D::VertexTL* vertices[4], int32_t vertexCount, uint32_t* texData, int32_t renderState, int32_t bucketGroup);
	int32_t IsClockwiseWinding(const Nu3D::VertexTL* first, const Nu3D::VertexTL* second, const Nu3D::VertexTL* third);
	void ProjectVertex(Nu3D::VertexTL* vertex);

	int32_t GetStrideFromFVF(int32_t fvf);

	void SubmitQuad(int32_t renderFlags, int32_t textureIndex, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices);
	void SubmitTriangleList(int32_t renderFlags, LPDIRECT3DVERTEXBUFFER vertexBuffer, Renderer::RenderEntry* renderEntry, WORD* indices, int32_t indexCount);
	void SubmitTriangleStrip(int32_t renderFlags, LPDIRECT3DVERTEXBUFFER vertexBuffer, Renderer::RenderEntry* renderEntry, WORD* indices, int32_t indexCount);
	void SubmitTriangleStripRaw(int32_t renderFlags, Nu3D::VertexTL* lockedVertices, Renderer::RenderEntry* renderEntry, WORD* indices, int32_t indexCount);
	// SubmitTriangleList (0x004B5FB0) walks a triangle-list index buffer back-to-front
	// and forwards each triangle to SubmitSortedTriangle with textureIndex=0 and the same render entry.
	// (the opposite slot assignment from SubmitQuad).
	// SubmitTriangleStrip (0x004B6040) emits a triangle strip (indexCount-2 triangles)
	// with alternating winding. It also retains the render entry and uses textureIndex=0.
	// SubmitTriangleStripRaw (0x004B6140) is the same strip logic but operates on an
	// already-locked vertex buffer (caller pre-locks and passes the base pointer).
	// SubmitSortedTriangle (0x004B5E40) bucket-sorts a transformed triangle into g_renderBuckets
	// by depth. The render-entry and texture-index values map to sorted-record +0x10 and +0xc.
	// SubmitQuad sets the render entry to null. The indexed-strip submitters retain it.
	void SubmitSortedTriangle(
		int32_t renderFlags, Renderer::RenderEntry* renderEntry, int32_t textureIndex, Nu3D::VertexTL* v0, Nu3D::VertexTL* v1, Nu3D::VertexTL* v2);

	void UnkFunc67(int32_t x, int32_t y);
	void UnkFunc2();
	int16_t UpdateBackdropScroll();
	void FlushRenderCommands();
	void ResetRenderCommands();
	void FlushSortedRenderCommands();
	void SetPaletteOnAPI();
	void UpdatePaletteTint();
	void LoadPaletteEntries(const uint8_t* source);
	void BuildPaletteLightingTable();
	void BuildRGBToPaletteTable();
	void BuildAdditivePaletteTable();
	void BuildSubtractivePaletteTable();
	void BuildPaletteBlendTable(uint8_t* output, int32_t blendWeight);
	void BuildPaletteColourOffsetTable();
	void SetNewPalette(const uint8_t* source, uint32_t tableFlags);

	void UnkFunc8(int32_t highestBucket, int32_t clearValue);

	// A queued render command for the software rasterizer. QueueRenderCommand enqueues
	// transformed vertices (3 for a triangle, 4 for a quad when vertexCount is
	// 4) and RasterizeRenderCommand dequeues and rasterizes one. Stride 0x9C, capacity 1024.
	struct RenderCommand;
	void QueueRenderCommand(Nu3D::VertexTL* vertices[4], int32_t vertexCount, uint32_t* texData, int32_t renderState);

	// One scanline of a triangle or a quad. RasterizeSortedRenderCommand and RasterizeRenderCommand choose the
	// variant from the render state and publish it in g_spanRasterizer; UnkFunc57
	// and UnkFunc58 then call it per scanline. There are 21 variants, one per
	// combination of pixel format, texturing, and blend mode.
	//
	// Every variant shares one ten-argument __cdecl signature. The walkers push
	// ten dwords and clean up with a single `add esp, 0x28`, and each variant
	// reads the same [ebp+8..ebp+0x2c] slots, so the shared parameter list is:
	//
	//   edgeA, edgeB      the two span endpoints, in no particular order. Each is
	//                     a VertexTL, read for position.x (truncated by __ftol to
	//                     a pixel column) and, on the textured and modulated
	//                     paths, for uv. Each variant compares the two and works
	//                     from the one with the smaller x.
	//   destRow           the 16-bit destination pixel cursor for this row.
	//   texData           the texture pixel buffer, NULL on untextured paths.
	//   edgeA/edgeB RGB   the two endpoint colours in 16-bit fixed point, one
	//                     five-bit channel per component in the high half. The
	//                     rasterizer interpolates between them across the span.
	//
	// A variant that ignores a slot still receives it, because the walker calls
	// every variant through the same pointer.
	typedef void (*SpanRasterizer)(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	extern SpanRasterizer g_spanRasterizer;
	extern int32_t g_spanAlpha;
	extern int32_t g_spanInvAlpha;

	// The span variants themselves. Naming each one needs its body, so they keep
	// their map names for now; the selector below documents which state picks
	// which.
	void UnkFunc36(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc37(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc38(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc40(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc41(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc42(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue);
	void UnkFunc43(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue);
	void RasterizeTexturedAlphaBlendSpan565(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void RasterizeTexturedSpanPairSample(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void RasterizeTexturedSpan(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc49(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc50(Nu3D::VertexTL* edgeA,
		Nu3D::VertexTL* edgeB,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t edgeARed,
		int32_t edgeAGreen,
		int32_t edgeABlue,
		int32_t edgeBRed,
		int32_t edgeBGreen,
		int32_t edgeBBlue);
	void RasterizeTexturedAlphaBlendSpan555(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc52(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);

	// Subtractive span for a 16-bit 555 surface. See the definition.
	void UnkFunc53(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc55(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);
	void UnkFunc56(Nu3D::VertexTL* leftEdge,
		Nu3D::VertexTL* rightEdge,
		uint16_t* destRow,
		uint32_t* texData,
		int32_t leftRed,
		int32_t leftGreen,
		int32_t leftBlue,
		int32_t rightRed,
		int32_t rightGreen,
		int32_t rightBlue);

	// The whole-primitive paths. The two quad rasterizers take only the command;
	// the two triangle walkers also take the texture.
	void UnkFunc39(RenderCommand* command);
	void UnkFunc54(RenderCommand* command);
	void UnkFunc46(RenderCommand* command, uint32_t* texData);
	void UnkFunc45(RenderCommand* command, uint32_t* texData);
	void UnkFunc57(RenderCommand* command, uint32_t* texData);
	void UnkFunc58(RenderCommand* command, uint32_t* texData);

	void RasterizeRenderCommand(RenderCommand* command, int32_t vertexCount, int32_t renderState, uint32_t* texData, int32_t useAlternateSpans);
	void RasterizeSortedRenderCommand(RenderCommand* command, int32_t vertexCount, int32_t renderState, uint32_t* texData, int32_t useAlternateSpans);

	// Snapshot of the current texture's writable data: the pixel buffer and the
	// surface descriptor that holds its dimensions and pitch. UnkFunc20 fills
	// this from Nu3D::g_currentBmpDataNode; it returns 0 on success and 1 when
	// no texture is bound. Callers mask the texData pointer with the return so a
	// NULL texture becomes a NULL rasterizer source.
	struct TextureData
	{
		uint32_t* texData;
		DDSURFACEDESC2* surfaceDesc;
	};
	int32_t UnkFunc20(TextureData* out);
	void UnkFunc18(float* primaryDistance, float* secondaryDistance);
	void UnkFunc19(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags);
	void UnkFunc21(LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags);
	void UnkFunc22(Nu3D::VertexTL* vertices[3], int32_t vertexCount, uint32_t* texData, int32_t renderState, int32_t primitiveType, DWORD drawFlags);
	void UnkFunc17(int32_t top, int32_t bottom, int32_t left, int32_t right);
	void UnkFunc16(D3DPRIMITIVETYPE d3dptPrimitiveType, LPVOID lpvVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags);
}

namespace SoftwareDevice
{
	// Drawing Methods
	HRESULT DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE primitiveType, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices, DWORD indexCount, DWORD flags);
	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE d3dptPrimitiveType,
		DWORD dwVertexTypeDesc,
		LPVOID lpvVertices,
		DWORD dwVertexCount,
		LPWORD lpwIndices,
		DWORD dwIndexCount,
		DWORD dwFlags);

	// Vertex Methods
	HRESULT ReleaseVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer);
	HRESULT CreateVertexBuffer(D3DVERTEXBUFFERDESC* desc, LPDIRECT3DVERTEXBUFFER* outBuffer, DWORD flags);
	HRESULT LockVertexBuffer(LPDIRECT3DVERTEXBUFFER vertexBuffer, DWORD dwFlags, LPVOID* lplpData, DWORD* lpStride);
	HRESULT UnlockVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer);
	HRESULT OptimizeVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer, LPDIRECT3DDEVICE3 device, DWORD flags);
	HRESULT ProcessVerticesOnBuffer(LPDIRECT3DVERTEXBUFFER destBuffer,
		DWORD dwVertexOp,
		DWORD dwDestIndex,
		DWORD dwCount,
		LPDIRECT3DVERTEXBUFFER srcBuffer,
		DWORD dwSrcIndex,
		DWORD dwFlags);
}

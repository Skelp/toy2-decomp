#pragma once

#include "Common.h"
#include "Numerics.h"
#include "Nu3D/Nu3D.h"
#include "Nu3D/Viewport.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

namespace SoftwareRenderer
{
	extern PointI g_unk4F7400;
	extern int32_t g_backdropWidth;
	extern int32_t g_staticBackdropWidth;
	extern int32_t g_unk500A1C;
	extern int32_t g_unk830C60;
	extern int32_t g_unk559C40;
	extern void* g_unk839278;
	extern void* g_unk504D34;
	extern int32_t g_unk839280;
	extern int32_t g_unkE4D950;
	extern int32_t g_unk9F6008;

	// The software renderer's DirectDraw palette and backing entry buffers.
	// SetPaletteOnAPI (0x00470BF0) creates the palette from g_paletteEntries and
	// attaches it to the front/back buffers; UnkFunc7 (0x00470C70) rebuilds the
	// live entries by tinting g_paletteSource by the camera tint, then SetEntries.
	// The palette is stored B,G,R,X per entry (byte 0 = blue, 1 = green, 2 = red).
	extern LPDIRECTDRAWPALETTE g_lpPalette;
	extern uint8_t g_paletteEntries[0x400];
	extern uint8_t g_paletteSource[0x400];
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
	void CommitZoom();
	void InitialiseColourScaleTables();
	void ZoomOut();
	void ZoomIn();
	void PresentFrame();
	void SetCameraNearFarZ(float nearZ, float farZ);

	int32_t GetStrideFromFVF(int32_t fvf);

	void SubmitQuad(int32_t renderFlags, int32_t textureIndex, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices);
	// SubmitTriangleList (0x004B5FB0) walks a triangle-list index buffer back-to-front
	// and forwards each triangle to SubmitSortedTriangle with fieldC=0/field10=field10
	// (the opposite slot assignment from SubmitQuad).
	// SubmitTriangleStrip (0x004B6040) emits a triangle strip (indexCount-2 triangles)
	// with alternating winding; same fieldC=0/field10=field10 slot assignment.
	// SubmitTriangleStripRaw (0x004B6140) is the same strip logic but operates on an
	// already-locked vertex buffer (caller pre-locks and passes the base pointer).
	// SubmitSortedTriangle (0x004B5E40) bucket-sorts a transformed triangle into g_renderBuckets
	// by depth. param field10/fieldC map to sorted-record +0x10/+0xc; SubmitQuad populates
	// fieldC=textureIndex/field10=0 while the indexed-strip submitters swap them — refine the
	// names when SubmitSortedTriangle and its record struct are reconstructed.
	void SubmitSortedTriangle(int32_t renderFlags, int32_t field10, int32_t fieldC, Nu3D::VertexTL* v0, Nu3D::VertexTL* v1, Nu3D::VertexTL* v2);

	void UnkFunc67(int32_t param1, int32_t param2);
	void UnkFunc2();
	int16_t UnkFunc3();
	void UnkFunc31();
	void UnkFunc32();
	void UnkFunc33();
	void UnkFunc7();

	void UnkFunc8(void* param1, int32_t param2);

	// A queued render command for the software rasterizer. UnkFunc29 enqueues
	// transformed vertices (3 for a triangle, 4 for a quad when vertexCount is
	// 4) and UnkFunc35 dequeues and rasterizes one. Stride 0x9C, capacity 1024.
	// The metadata at +0x80 is only partially understood; refine the names when
	// UnkFunc29 and UnkFunc35 are reconstructed.
	struct RenderCommand;
	void UnkFunc29(Nu3D::VertexTL* vertices[4], int32_t vertexCount, int32_t field80, int32_t field88);
	void UnkFunc35(RenderCommand* command, int32_t vertexCount, int32_t field88, int32_t field80, int32_t field94);
	void UnkFunc34(RenderCommand* command, int32_t vertexCount, int32_t field88, int32_t field80, int32_t field94);

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

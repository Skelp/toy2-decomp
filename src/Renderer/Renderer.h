#pragma once

#include "Common.h"
#include "Renderer/RenderType.h"
#include "Nu3D/Patch.h"
#include <stddef.h>
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

#define D3DFVF_0x152 (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define D3DFVF_0x1C4 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)

namespace Nu3D
{
	struct BmpDataNode;
	struct Primitive;
	struct Material;
	struct InstanceData;
	struct VertexTL;
	struct Patch;
}

namespace DrawingAPI
{
	// Drawing Methods
	typedef HRESULT Device_DrawIndexedPrimitiveVB(
		D3DPRIMITIVETYPE primitiveType, LPDIRECT3DVERTEXBUFFER vertexBuffer, WORD* indices, DWORD indexCount, DWORD flags);
	typedef HRESULT Device_DrawIndexedPrimitive(D3DPRIMITIVETYPE d3dptPrimitiveType,
		DWORD dwVertexTypeDesc,
		LPVOID lpvVertices,
		DWORD dwVertexCount,
		LPWORD lpwIndices,
		DWORD dwIndexCount,
		DWORD dwFlags);

	// Vertex Methods
	typedef HRESULT Device_ReleaseVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer);
	typedef HRESULT Device_CreateVertexBuffer(D3DVERTEXBUFFERDESC* desc, LPDIRECT3DVERTEXBUFFER* outBuffer, DWORD flags);
	typedef HRESULT Device_LockVertexBuffer(LPDIRECT3DVERTEXBUFFER vertexBuffer, DWORD dwFlags, LPVOID* lplpData, DWORD* lpStride);
	typedef HRESULT Device_UnlockVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer);
	typedef HRESULT Device_OptimizeVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer, LPDIRECT3DDEVICE3 device, DWORD flags);
	typedef HRESULT Device_ProcessVerticesOnBuffer(LPDIRECT3DVERTEXBUFFER destBuffer,
		DWORD dwVertexOp,
		DWORD dwDestIndex,
		DWORD dwCount,
		LPDIRECT3DVERTEXBUFFER srcBuffer,
		DWORD dwSrcIndex,
		DWORD dwFlags);

	extern Device_DrawIndexedPrimitive* DrawIndexedPrimitive;
	extern Device_DrawIndexedPrimitiveVB* DrawIndexedPrimitiveVB;

	extern Device_ReleaseVertexBuffer* ReleaseVertexBuffer;
	extern Device_CreateVertexBuffer* CreateVertexBuffer;
	extern Device_LockVertexBuffer* LockVertexBuffer;
	extern Device_UnlockVertexBuffer* UnlockVertexBuffer;
	extern Device_OptimizeVertexBuffer* OptimizeVertexBuffer;
	extern Device_ProcessVerticesOnBuffer* ProcessVerticesOnBuffer;
}

namespace Renderer
{
	struct ViewportPreset
	{
		float secondaryPortalNearClip;
		float primaryFogFarClip;
		float secondaryNearClip;
		float primaryNearClip;
		float primaryRenderDistance;
		float secondaryRenderDistance;
	};

	struct RenderEntry
	{
		RenderEntry* next;
		Renderer::RenderType type;
		float distanceSquared;
		union
		{
			Nu3D::Primitive* primitive;
			Nu3D::Patch* patch;
		};
		Nu3D::InstanceData* instanceData;
		Nu3D::Material* material;

		static RenderEntry* AllocObj(Nu3D::Material* material, Nu3D::Primitive* primitive, Nu3D::InstanceData* instanceData);
		static RenderEntry* AllocPatch(Nu3D::Material* material, Nu3D::Patch* patch, Nu3D::InstanceData* instanceData);
		static void InsertIntoBucket(RenderEntry* entry);
	};

	extern float g_gammaCorrection;
	extern Nu3D::Patch::PatchVertices g_FVF_14C_Buffer_2;
	extern Nu3D::Patch::PatchVertices g_FVF_14C_Buffer_1;
	extern Nu3D::Patch::PatchVertices g_FVF_152_Buffer;
	extern int32_t g_drawingTransparentBuckets;
	extern int32_t g_isSoftwareRendering;
	extern int32_t g_frameDelta;
	extern int32_t g_renderStateCache[8];
	extern float g_virtualScreenWidth;
	extern float g_virtualScreenHeight;
	extern float g_parallaxCurHorizScroll;
	extern int32_t g_deviceBlendShadeCapsCpy;
	extern int32_t g_boundTextureIndices[8];
	extern Nu3D::Material* g_boundMaterial;
	extern int32_t g_additionalRenderFlags;
	extern float g_materialHorzOffset;
	extern float g_materialVertOffset;
	extern float g_primaryRenderDistanceSquared;
	extern float g_secondaryRenderDistanceSquared;
	extern LPDIRECT3DDEVICE3 g_drawDeviceD3DDevice;
	extern int32_t g_drawTriangleWireframes;
	extern int32_t g_submittedVertexCount;
	extern int32_t g_submittedTriangleCount;
	extern int32_t g_submittedPrimitiveCount;

	void Cleanup();
	void Init();
	void SetIsSoftwareRendering(int32_t value);
	void SetVirtualRatioTo54();
	void DoFrameDelay(int32_t isGameplayFrame);
	RGBA ApplyGammaCorrection(RGBA color);
	void ConfigureFog(float start, float end, RGBA color);
	void SetFogEnable(int32_t enable);
	int32_t GetIsSoftwareRendering();
	float BuildGammaCorrectionLUT(float gammaCorrection);
	void GetBlendShadeCaps(int32_t* capsOut);

	// Draw Methods
	void ClearScreen(RGBA clearColor, int32_t clearFlags);
	int32_t BeginScene();
	void EndScene(int32_t presentFrame);
	void ShowBlackFrames();
	void BlitBitmapWithWrapping(
		Nu3D::BmpDataNode* bitmap, int32_t sourceX, int32_t sourceY, int32_t width, int32_t height, int32_t wrapX, int32_t wrapY, int32_t destX, int32_t destY);
	void BlitTextureByIndex(
		uint32_t textureIndex, int32_t destX, int32_t destY, int32_t width, int32_t height, int32_t wrapX, int32_t wrapY, int32_t sourceX, int32_t sourceY);
	void BlitTextureByIndexOffset(uint32_t textureIndex,
		int32_t destX,
		int32_t destY,
		int32_t width,
		int32_t height,
		int32_t wrapX,
		int32_t wrapY,
		int32_t sourceOffsetX,
		int32_t sourceOffsetY);

	// Render Methods
	void DrawBitmapText(const char* text, int32_t screenY, int32_t screenX, uint32_t red, uint32_t green, uint32_t blue, uint32_t flags);
	void DrawFormattedText(int32_t screenX, int32_t screenY, const char* format, ...);
	void DrawMainMenuText(int16_t yPos, char* text, int32_t fadeAlpha);
	void DrawChar(int32_t xPos, int32_t yPos, uint8_t character, uint32_t red, uint32_t green, uint32_t blue, int32_t fullWidthLayout);
	void DrawString(int32_t yPos, const char* text, uint32_t red, uint32_t green, uint32_t blue, int32_t fullWidthLayout);
	void DrawBlackBorderBox(int32_t xPos, int32_t yPos, int32_t width, int32_t height, uint32_t red, uint32_t green, uint32_t blue);
	void DrawTintOverlay();

	namespace Beam
	{
		struct Command
		{
			uint32_t spriteSheetIndex;
			uint32_t width;
			int32_t segmentLength;
			Vector4I position;
			Vector4I direction;
			RGB32 color;
		};

		void QueueBeam(uint32_t spriteSheetIndex,
			uint32_t width,
			int32_t segmentLength,
			const Vector4I* position,
			const Vector4I* direction,
			uint32_t red,
			uint32_t green,
			uint32_t blue);

		STATIC_ASSERT(sizeof(Command) == 0x38);
		STATIC_ASSERT(offsetof(Command, position) == 0x0C);
		STATIC_ASSERT(offsetof(Command, direction) == 0x1C);
		STATIC_ASSERT(offsetof(Command, color) == 0x2C);

		extern int32_t g_queueHead;
		extern Command g_commands[100];
	}

	void InitRenderState(int32_t newStage);
	int32_t SetupMaterialRenderState(Nu3D::Material* material, int32_t stateFlags);
	void ResetParallax();
	void RenderParallaxBackground(int32_t forceRender);
	void FlushRenderQueues();
	RGBA ModulateColorByAlpha(RGBA color, int32_t flags);
	int32_t SetAdditionalRenderFlags(int32_t flags);
	void SetVertexColorModulation(int32_t red, int32_t green, int32_t blue);
	int32_t EnableVertexColorModulation(int32_t enable);
	int32_t Set508718(int32_t value);
	int32_t Set9F5FF8(int32_t value);
	void SetRenderDistance(float primaryDistance, float secondaryDistance);
	void SetViewportPresetByDetail(int32_t detail);
	void SetViewportPreset();
	void RenderPrimitive(Nu3D::Primitive* primitive, const D3DMATRIX* transform, int32_t renderFlags);
	void ProcessPrimitive(Nu3D::InstanceData* instanceData, Nu3D::Primitive* primitive);
	void RenderPatchList(Nu3D::Patch* patch, const D3DMATRIX* matrices, int32_t* flags, int32_t renderFlags);
	void ProcessPatch(Nu3D::InstanceData* instanceData, Nu3D::Patch* patch);
	void BindTexture(int32_t texIndex);
	void BindMaterial(Nu3D::Material* material, int32_t force);
	void UnbindMaterial();
	void DrawSingleTexturedTriangle(Nu3D::VertexTL* vertices, int32_t texIndex, int32_t renderFlags);

	STATIC_ASSERT(sizeof(RenderEntry) == 0x18);
	STATIC_ASSERT(sizeof(ViewportPreset) == 0x18);
	STATIC_ASSERT(offsetof(RenderEntry, primitive) == 0x0C);
	STATIC_ASSERT(offsetof(RenderEntry, instanceData) == 0x10);
	STATIC_ASSERT(offsetof(RenderEntry, material) == 0x14);

	// A transformed triangle queued for sorted (back-to-front) transparency
	// rasterization. SubmitSortedTriangle fills a slot from g_primitiveBuffer,
	// computes depthKey = min(v0.z, v1.z, v2.z) * k_depthSortScale, and inserts
	// the slot into the g_renderBuckets[depthKey & 0x3ff] linked list kept in
	// descending depthKey order. SubmitSortedTriangle does not write the
	// reserved field at +0x04.
	struct SortedPrimitive
	{
		SortedPrimitive* next;
		int32_t reserved;
		int32_t renderFlags;
		int32_t textureIndex;
		RenderEntry* renderEntry;
		Nu3D::VertexTL v0;
		Nu3D::VertexTL v1;
		Nu3D::VertexTL v2;
		float depthKey;
	};

	extern int32_t g_primitiveBufferFreeCount;
	extern void* g_renderBuckets[1024];
	extern SortedPrimitive g_primitiveBuffer[3000];

	STATIC_ASSERT(sizeof(SortedPrimitive) == 0x78);
}

namespace DevDraw
{
	extern int32_t g_vertexCount;

	// Opaque indexed draw buffer. Stores per-slot vertex-pointer, vertex-count,
	// index, and index-count arrays for the main (opaque) geometry pass. The
	// retail binary names the instance `drawb`.
	//
	// Layout confirmed by the arithmetic in FlushDrawBufferSlot:
	//   Vertice[65] at +0x000, VerticeCount[65] at +0x104,
	//   Index[65][1000] at +0x186, IndexCount[65] at +0x1FD56.
	//
	// The shared vertex pool follows the slot arrays at +0x1FDD8, and its count
	// is at +0x9FDD8. The function at 0x00490D10 confirms both: it appends two
	// vertices per call with `LEA edi,[edx + esi*1 + 0x1FDD8]` after `esi =
	// count << 5`, so the element stride is 0x20 = sizeof(VertexTL). Its guard
	// `cmp cx, 0x3FFE` keeps room for that pair, which makes the highest index
	// 0x3FFF and the capacity 16384. (0x9FDD8 - 0x1FDD8) / 0x20 = 16384 closes
	// the arithmetic exactly.
	struct DrawBuffer
	{
		void* Vertice[65];
		int16_t VerticeCount[65];
		WORD Index[65][1000];
		int16_t IndexCount[65];
		Nu3D::VertexTL VerticePool[16384];
		int16_t VerticePoolCount;
	};

	// Transparent indexed draw buffer. Same layout as DrawBuffer for the vertex
	// arrays but with a larger index block (6000 WORD per slot) and only 32
	// index slots. The retail binary names the instance `drawtranb`.
	//
	// Layout confirmed by the arithmetic in FlushTransparentDrawBufferSlot:
	//   Vertice[65] at +0x000, VerticeCount[65] at +0x104,
	//   Index[32][6000] at +0x186, IndexCount[32] at +0x5DD86.
	//   Total size 0x5DDC6.
	struct TransparentDrawBuffer
	{
		void* Vertice[65];
		int16_t VerticeCount[65];
		WORD Index[32][6000];
		int16_t IndexCount[32];
	};

	STATIC_ASSERT(offsetof(DrawBuffer, VerticeCount) == 0x104);
	STATIC_ASSERT(offsetof(DrawBuffer, Index) == 0x186);
	STATIC_ASSERT(offsetof(DrawBuffer, IndexCount) == 0x1FD56);
	STATIC_ASSERT(offsetof(DrawBuffer, VerticePool) == 0x1FDD8);
	STATIC_ASSERT(offsetof(DrawBuffer, VerticePoolCount) == 0x9FDD8);

	STATIC_ASSERT(offsetof(TransparentDrawBuffer, VerticeCount) == 0x104);
	STATIC_ASSERT(offsetof(TransparentDrawBuffer, Index) == 0x186);
	STATIC_ASSERT(offsetof(TransparentDrawBuffer, IndexCount) == 0x5DD86);

	int16_t DrawSlots();

	int16_t FlushDrawBufferSlot(int16_t slot);
	int16_t FlushTransparentDrawBufferSlot(int16_t slot);
}

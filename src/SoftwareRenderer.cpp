#include "SoftwareRenderer.h"
#include "Renderer/Renderer.h"

namespace SoftwareRenderer
{
	// GLOBAL: TOY2 0x004F7400
	PointI g_unk4F7400 = { -32768, -32768 };

	// GLOBAL: TOY2 0x00500A1C
	int32_t g_unk500A1C = 0xFFFFFFFF;

	// GLOBAL: TOY2 0x00830C60
	int32_t g_unk830C60;

	// GLOBAL: TOY2 0x00E4D950
	int32_t g_unkE4D950;

	// GLOBAL: TOY2 0x009F6008
	int32_t g_unk9F6008;

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

	// GLOBAL: TOY2 0x00882910
	int32_t g_bitsPerPixel;

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

	// STUB: TOY2 0x00452130
	void SwapRenderBuffer() {}

	// FUNCTION: TOY2 0x004C20E0 [MATCHED]
	void SetLevelFileIndex(int32_t index) { g_levelFileIndex = index; }

	// STUB: TOY2 0x004BCE00
	void InitialisePrimarySurface() {}

	// STUB: TOY2 0x004C1E60
	void InitialisePrimarySurface_T() {}

	// STUB: TOY2 0x0047D0F0
	void Destroy() {}

	// STUB: TOY2 0x004C1E70
	void CommitZoom() {}

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

	// STUB: TOY2 0x00490410
	void UnkFunc67(int32_t param1, int32_t param2) {}

	// STUB: TOY2 0x0048FB70
	void UnkFunc2() {}

	// STUB: TOY2 0x00490290
	int16_t UnkFunc3() { return 0; }

	// STUB: TOY2 0x004BCBE0
	void UnkFunc31() {}

	// STUB: TOY2 0x004BCC40
	void UnkFunc32() {}

	// STUB: TOY2 0x004BCB60
	void UnkFunc33() {}

	// STUB: TOY2 0x00470C70
	void UnkFunc7() {}

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

	// STUB: TOY2 0x004B99F0
	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE d3dptPrimitiveType,
		DWORD dwVertexTypeDesc,
		LPVOID lpvVertices,
		DWORD dwVertexCount,
		LPWORD lpwIndices,
		DWORD dwIndexCount,
		DWORD dwFlags)
	{ return DDERR_UNSUPPORTED; }

	// Vertex Methods

	// STUB: TOY2 0x004B2B20
	HRESULT ReleaseVertexBuffer(LPDIRECT3DVERTEXBUFFER buffer) { return DDERR_UNSUPPORTED; }

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

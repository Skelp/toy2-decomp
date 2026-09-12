#include "SoftwareRenderer.h"
#include "SoftwareRendererInternal.h"
#include "DrawingDevice.h"
#include "Renderer/Renderer.h"
#include "Toy2/Toy2.h"
#include "Nu3D/Math.h"
#include "Nu3D/Viewport.h"
#include "D3DApp/d3dapp.h"
#include "D3DApp/d3dappi.h"
#include <stdlib.h>
#include <math.h>

// The primitive submission path of the software device and the vertex buffers it
// draws from. Renderer hands triangles, strips and quads to the Submit*
// functions, which queue them; namespace SoftwareDevice holds the
// LPDIRECT3DVERTEXBUFFER entry points that stand in for the Direct3D ones.
//
// Retail band 0x004B2B20-0x004B99F0.
namespace SoftwareRenderer
{
	// GLOBAL: TOY2 0x009F5FF4
	Nu3D::Viewport::ViewportRect* g_viewportRect;

	// .rdata depth-sort scale: 1023.0 (= 1024 - 1). SubmitSortedTriangle maps
	// the triangle's minimum vertex z into the 1024-entry bucket range.
	// GLOBAL: TOY2 0x004DDAC4
	extern const float k_depthSortScale = 1023.0f;

	// GLOBAL: TOY2 0x009F6010
	int32_t g_disableSortedPrimitiveSubmission;

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

#undef NU_FMIN

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

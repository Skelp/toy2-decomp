#include "Nu3D/Patch.h"
#include "Logger.h"
#include "Renderer/Renderer.h"

namespace Nu3D
{
	// FUNCTION: TOY2 0x004B2EB0 [MATCHED]
	void Patch::PatchVertices::ReleaseBuffer(PatchVertices* patchVertices)
	{
		if (patchVertices->vertexBuffer)
		{
			DrawingAPI::ReleaseVertexBuffer(patchVertices->vertexBuffer);
			patchVertices->vertexBuffer = 0;
		}
	}

	// FUNCTION: TOY2 0x004B2ED0 [PROVISIONAL]
	BOOL Patch::PatchVertices::CreateVertexBuffer(PatchVertices* patchVertices, int32_t flags)
	{
		patchVertices->bufferFlags = 0;

		if (patchVertices->vertexBuffer)
			ReleaseBuffer(patchVertices);

		D3DVERTEXBUFFERDESC bufferDesc;
		memset(&bufferDesc, 0, sizeof(bufferDesc));
		bufferDesc.dwSize = sizeof(bufferDesc);

		if (Renderer::g_isSoftwareRendering)
			flags |= SYSTEM_MEMORY;

		if (flags & WRITE_ONLY)
			bufferDesc.dwCaps |= D3DVBCAPS_WRITEONLY;

		if (flags & SYSTEM_MEMORY)
			bufferDesc.dwCaps |= D3DVBCAPS_SYSTEMMEMORY;

		bufferDesc.dwFVF = patchVertices->format;
		bufferDesc.dwNumVertices = patchVertices->vertexCount;

		DWORD createFlags = (flags & DO_NOT_CLIP) >> 2;
		HRESULT result = DrawingAPI::CreateVertexBuffer(&bufferDesc, &patchVertices->vertexBuffer, createFlags);

		if (result == DD_OK && (flags & UPLOAD_DATA))
		{
			void* bufferData;
			result = DrawingAPI::LockVertexBuffer(patchVertices->vertexBuffer, DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK, &bufferData, 0);

			if (result == DD_OK)
			{
				switch (patchVertices->format)
				{
					case D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1:
						memcpy(bufferData, patchVertices->data.verticesUncolored, sizeof(VertexUncolored) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x1C4:
						memcpy(bufferData, patchVertices->data.verticesTL, sizeof(VertexTL) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x152:
						memcpy(bufferData, patchVertices->data.vertices, sizeof(Vertex) * patchVertices->vertexCount);
						break;
				}

				DrawingAPI::UnlockVertexBuffer(patchVertices->vertexBuffer);

				// The retail code applied ! before &, making this unreachable.
				// Keep the original expression because changing it alters behavior.
				if (! flags & DYNAMIC)
				{
					DrawingAPI::OptimizeVertexBuffer(patchVertices->vertexBuffer, Renderer::g_drawDeviceD3DDevice, 0);
				}
			}
		}

		if (result != DD_OK)
		{
			Logger::GetErrorHandler("C:\\projects\\nu3d\\objcore.c", 654)("vertex buffer allocation failure");
		}

		patchVertices->bufferFlags = flags;
		return result == DD_OK;
	}

	// FUNCTION: TOY2 0x004B3050 [PROVISIONAL]
	BOOL Patch::PatchVertices::UpdateVertexBuffer(PatchVertices* patchVertices)
	{
		HRESULT result = -1;

		if (patchVertices->bufferFlags & DYNAMIC)
		{
			void* bufferData;
			result = DrawingAPI::LockVertexBuffer(patchVertices->vertexBuffer, DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK, &bufferData, 0);

			if (result == DD_OK)
			{
				switch (patchVertices->format)
				{
					case D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1:
						memcpy(bufferData, patchVertices->data.verticesUncolored, sizeof(VertexUncolored) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x1C4:
						memcpy(bufferData, patchVertices->data.verticesTL, sizeof(VertexTL) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x152:
						memcpy(bufferData, patchVertices->data.vertices, sizeof(Vertex) * patchVertices->vertexCount);
						break;
				}

				DrawingAPI::UnlockVertexBuffer(patchVertices->vertexBuffer);

				// See CreateVertexBuffer: this retains the original precedence bug.
				if (! patchVertices->bufferFlags & DYNAMIC)
				{
					DrawingAPI::OptimizeVertexBuffer(patchVertices->vertexBuffer, Renderer::g_drawDeviceD3DDevice, 0);
				}
			}
		}

		return result == DD_OK;
	}
}

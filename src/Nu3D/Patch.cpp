#include "Nu3D/Patch.h"
#include "Logger.h"
#include "Renderer/Renderer.h"

namespace Nu3D
{
	// GLOBAL: TOY2 0x00884480
	Patch* g_patchListHead;

	// FUNCTION: TOY2 0x004B2EB0 [MATCHED]
	void Patch::PatchVertices::ReleaseBuffer(PatchVertices* patchVertices)
	{
		if (patchVertices->vertexBuffer)
		{
			DrawingAPI::ReleaseVertexBuffer(patchVertices->vertexBuffer);
			patchVertices->vertexBuffer = 0;
		}
	}

	// FUNCTION: TOY2 0x004B2ED0
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
			result = DrawingAPI::LockVertexBuffer(
				patchVertices->vertexBuffer, DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK, &bufferData, 0);

			if (result == DD_OK)
			{
				switch (patchVertices->format)
				{
					case D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1:
						memcpy(bufferData, patchVertices->data.verticesUncolored,
							sizeof(VertexUncolored) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x1C4:
						memcpy(bufferData, patchVertices->data.verticesTL,
							sizeof(VertexTL) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x152:
						memcpy(bufferData, patchVertices->data.vertices, sizeof(Vertex) * patchVertices->vertexCount);
						break;
				}

				DrawingAPI::UnlockVertexBuffer(patchVertices->vertexBuffer);

				// The retail code applied ! before &, making this unreachable.
				// Keep the original expression because changing it alters behavior.
				if (!flags & DYNAMIC)
				{
					DrawingAPI::OptimizeVertexBuffer(
						patchVertices->vertexBuffer, Renderer::g_drawDeviceD3DDevice, 0);
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

	// FUNCTION: TOY2 0x004B3050
	BOOL Patch::PatchVertices::UpdateVertexBuffer(PatchVertices* patchVertices)
	{
		HRESULT result = -1;

		if (patchVertices->bufferFlags & DYNAMIC)
		{
			void* bufferData;
			result = DrawingAPI::LockVertexBuffer(
				patchVertices->vertexBuffer, DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK, &bufferData, 0);

			if (result == DD_OK)
			{
				switch (patchVertices->format)
				{
					case D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1:
						memcpy(bufferData, patchVertices->data.verticesUncolored,
							sizeof(VertexUncolored) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x1C4:
						memcpy(bufferData, patchVertices->data.verticesTL,
							sizeof(VertexTL) * patchVertices->vertexCount);
						break;

					case D3DFVF_0x152:
						memcpy(bufferData, patchVertices->data.vertices, sizeof(Vertex) * patchVertices->vertexCount);
						break;
				}

				DrawingAPI::UnlockVertexBuffer(patchVertices->vertexBuffer);

				// See CreateVertexBuffer: this retains the original precedence bug.
				if (!patchVertices->bufferFlags & DYNAMIC)
				{
					DrawingAPI::OptimizeVertexBuffer(
						patchVertices->vertexBuffer, Renderer::g_drawDeviceD3DDevice, 0);
				}
			}
		}

		return result == DD_OK;
	}

	// FUNCTION: TOY2 0x004B3170 [MATCHED]
	void Patch::PatchVertices::FreeVertices(PatchVertices* patchVertices)
	{
		if (patchVertices->data.vertices)
			free(patchVertices->data.vertices);
	}

	// FUNCTION: TOY2 0x004B3210 [MATCHED]
	void Patch::PatchVertices::Resize(PatchVertices* patchVertices, int32_t vertexCount)
	{
		Vertex* vertices = (Vertex*)malloc(sizeof(Vertex) * vertexCount);

		if (vertices && patchVertices)
		{
			patchVertices->format = D3DFVF_0x152;
			patchVertices->data.vertices = vertices;
			patchVertices->vertexCount = vertexCount;
		}
	}

	// FUNCTION: TOY2 0x004B34F0 [MATCHED]
	void Patch::Destroy(Patch* patch)
	{
		if (patch->listNext)
			Destroy(patch->listNext);

		PatchVertices::ReleaseBuffer(&patch->patchVertices);
		PatchVertices::FreeVertices(&patch->patchVertices);
		Remove(patch);
	}

	// FUNCTION: TOY2 0x004B3530 [MATCHED]
	void Patch::Remove(Patch* patch)
	{
		if (patch->next)
			patch->next->prev = patch->prev;

		if (patch->prev)
			patch->prev->next = patch->next;
		else
			g_patchListHead = patch->next;

		free(patch);
	}

	// FUNCTION: TOY2 0x004B3570 [MATCHED]
	Patch* Patch::AllocAndResize(int32_t vertexCount, int32_t controlPointCount)
	{
		Patch* patch = Alloc();

		if (patch)
		{
			PatchVertices::Resize(&patch->patchVertices, vertexCount);
			patch->controlPointCount = controlPointCount;
		}

		return patch;
	}

	// FUNCTION: TOY2 0x004B35A0 [MATCHED]
	Patch* Patch::Alloc()
	{
		Patch* patch = (Patch*)malloc(sizeof(Patch));

		if (patch)
		{
			memset(patch, 0, sizeof(Patch));

			patch->next = g_patchListHead;
			g_patchListHead = patch;
			patch->prev = 0;

			if (patch->next)
				patch->next->prev = patch;
		}

		return patch;
	}

	// FUNCTION: TOY2 0x004B35E0 [MATCHED]
	void Patch::ReleaseAllVertexBuffers(Patch* patch)
	{
		for (; patch; patch = patch->listNext)
			PatchVertices::ReleaseBuffer(&patch->patchVertices);
	}

	// FUNCTION: TOY2 0x004B3600 [MATCHED]
	BOOL Patch::CreateAllVertexBuffers(Patch* patch)
	{
		BOOL allCreated = TRUE;

		for (; patch; patch = patch->listNext)
			allCreated &= PatchVertices::CreateVertexBuffer(
				&patch->patchVertices, PatchVertices::UPLOAD_DATA | PatchVertices::WRITE_ONLY);

		return allCreated;
	}
}

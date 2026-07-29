#include "Nu3D/Patch.h"
#include "Logger.h"
#include "Renderer/Renderer.h"

namespace Nu3D
{
	// GLOBAL: TOY2 0x00884480
	Patch* g_patchListHead;

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
			allCreated &= PatchVertices::CreateVertexBuffer(&patch->patchVertices, PatchVertices::UPLOAD_DATA | PatchVertices::WRITE_ONLY);

		return allCreated;
	}
}

#include "Nu3D/Primitive.h"
#include "Renderer/Renderer.h"

namespace Nu3D
{
	// GLOBAL: TOY2 0x0088447C
	Primitive* g_primListHead;

	// STUB: TOY2 0x004B32E0
	int32_t Primitive::CreateAllVertexBuffers(Primitive* prim, int32_t flags) { return 0; }

	// STUB: TOY2 0x004CC480
	void Primitive::ComputeBounds(Primitive* primitive) {}

	// FUNCTION: TOY2 0x004B3280
	Primitive* Primitive::Alloc()
	{
		Primitive* next;
		Primitive* prim = (Primitive*)malloc(sizeof(Primitive));

		if (prim)
		{
			memset(prim, 0, sizeof(Primitive));

			prim->next = g_primListHead;

			g_primListHead = prim;

			next = prim->next;
			prim->prev = 0;

			if (next)
				next->prev = prim;
		}

		return prim;
	}

	// FUNCTION: TOY2 0x004B3240
	Primitive::Header* Primitive::BuildHeader(int32_t size)
	{
		Primitive::Header* headerList = (Primitive::Header*)malloc(sizeof(Primitive::Header) * size);

		if (headerList)
		{
			memset(headerList, 0, sizeof(Primitive::Header) * size);
			return headerList;
		}
		else
		{
			return 0;
		}
	}

	// FUNCTION: TOY2 0x004B31D0
	Primitive* Primitive::Build(int32_t verticesSize, int32_t headerCount)
	{
		Primitive* prim = Primitive::Alloc();

		if (prim)
		{
			Patch::PatchVertices::Resize(&prim->patchVerts, verticesSize);

			prim->headerCount = headerCount;
			prim->header = BuildHeader(headerCount);
			prim->originRadiusSq = 0.0;
			prim->originRadius = 0.0;
		}

		return prim;
	}

	// FUNCTION: TOY2 0x004B2E60
	int32_t Primitive::InitHeader(Header* primHeader, int32_t drawType, int32_t indexCount)
	{
		primHeader->indices = (uint16_t*)malloc(sizeof(uint16_t) * indexCount);

		Plane* triMeta = (Plane*)malloc(sizeof(Plane) * indexCount / 3);
		uint16_t* indices = primHeader->indices;

		primHeader->indexCount = indexCount;
		primHeader->triMeta = triMeta;
		primHeader->drawType = drawType;

		return indices && triMeta;
	}
}

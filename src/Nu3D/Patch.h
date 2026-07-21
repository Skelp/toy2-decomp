#pragma once

#include "Nu3D/Nu3D.h"
#include <directx6/ddraw.h>
#include <directx6/d3d.h>

namespace Nu3D
{
	struct Patch
	{
		struct PatchVertices
		{
			enum BufferFlags
			{
				UPLOAD_DATA = 1,
				WRITE_ONLY = 2,
				SYSTEM_MEMORY = 4,
				DYNAMIC = 8,
				DO_NOT_CLIP = 16,
			};

			int32_t format;
			int32_t vertexCount;

			union
			{
				Vertex* vertices;
				VertexUncolored* verticesUncolored;
				VertexTL* verticesTL;
			} data;

			int32_t bufferFlags;
			LPDIRECT3DVERTEXBUFFER vertexBuffer;

			static void ReleaseBuffer(PatchVertices* patchVertices);
			static BOOL CreateVertexBuffer(PatchVertices* patchVertices, int32_t flags);
			static BOOL UpdateVertexBuffer(PatchVertices* patchVertices);
			static void FreeVertices(PatchVertices* patchVertices);
			static void Resize(PatchVertices* patchVertices, int32_t vertexCount);
		};

		Patch* next;
		Patch* prev;
		Patch* listNext;
		int32_t unkVar4;
		int32_t materialId;
		PatchVertices patchVertices;
		int32_t controlPointCount;
		int32_t controlPointIndices[4];

		static void Destroy(Patch* patch);
		static void Remove(Patch* patch);
		static Patch* AllocAndResize(int32_t vertexCount, int32_t controlPointCount);
		static Patch* Alloc();
		static void ReleaseAllVertexBuffers(Patch* patch);
		static BOOL CreateAllVertexBuffers(Patch* patch);
	};

	extern Patch* g_patchListHead;

	STATIC_ASSERT(sizeof(Patch) == 0x3C);
	STATIC_ASSERT(sizeof(Patch::PatchVertices) == 0x14);
}

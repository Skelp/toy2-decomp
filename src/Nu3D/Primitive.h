#pragma once

#include "Common.h"
#include "Nu3D/Material.h"
#include "Nu3D/Patch.h"

namespace Nu3D
{
	struct Primitive
	{
		struct Header
		{
			int32_t drawType;
			int32_t indexCount;
			uint16_t* indices;
			Plane* triMeta;
		};

		Primitive* next;
		Primitive* prev;
		Primitive* listNext;
		int32_t renderFlags;
		int32_t materialIndex;
		Patch::PatchVertices patchVerts;
		int32_t headerCount;
		Header* header;
		float originRadius;
		float originRadiusSq;
		Vector3F boundsCenter;
		float boundSphereRadius;
		float boundSphereRadiusSq;

		static void DestroyHeader(Header* primHeader);
		static void Destroy(Primitive* primitive);
		static void Remove(Primitive* primitive);
		static void ReleaseAllVertexBuffers(Primitive* primitive);
		static int32_t CreateAllVertexBuffers(Primitive* primitive, int32_t flags);
		static int32_t UpdateAllVertexBuffers(Primitive* primitive);
		static void ComputeBounds(Primitive* primitive);
		static Primitive* Alloc();
		static Header* BuildHeader(int32_t headerCount);
		static Primitive* Build(int32_t verticesSize, int32_t headerCount);
		static int32_t InitHeader(Header* primHeader, int32_t drawType, int32_t indexCount);
	};

	extern Primitive* g_primListHead;

	STATIC_ASSERT(sizeof(Primitive) == 0x4C);
	STATIC_ASSERT(sizeof(Primitive::Header) == 0x10);
}

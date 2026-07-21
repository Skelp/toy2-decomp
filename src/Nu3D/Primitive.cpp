#include "Nu3D/Primitive.h"
#include "Nu3D/Math.h"
#include "Renderer/Renderer.h"
#include <float.h>
#include <math.h>

namespace Nu3D
{
	// GLOBAL: TOY2 0x0088447C
	Primitive* g_primListHead;

	// FUNCTION: TOY2 0x004B2E30
	void Primitive::DestroyHeader(Header* primHeader)
	{
		if (primHeader->indices)
			free(primHeader->indices);

		if (primHeader->triMeta)
			free(primHeader->triMeta);

		primHeader->indices = 0;
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

	// FUNCTION: TOY2 0x004B3110
	void Primitive::Destroy(Primitive* primitive)
	{
		if (primitive->listNext)
			Destroy(primitive->listNext);

		if (primitive->header)
		{
			for (int32_t i = 0; i < primitive->headerCount; i++)
				DestroyHeader(&primitive->header[i]);
		}

		Patch::PatchVertices::ReleaseBuffer(&primitive->patchVerts);
		Patch::PatchVertices::FreeVertices(&primitive->patchVerts);
		Remove(primitive);
	}

	// FUNCTION: TOY2 0x004B3190
	void Primitive::Remove(Primitive* primitive)
	{
		if (primitive->next)
			primitive->next->prev = primitive->prev;

		if (primitive->prev)
			primitive->prev->next = primitive->next;
		else
			g_primListHead = primitive->next;

		free(primitive);
	}

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
	Primitive::Header* Primitive::BuildHeader(int32_t headerCount)
	{
		Primitive::Header* headerList = (Primitive::Header*)malloc(sizeof(Primitive::Header) * headerCount);

		if (headerList)
			memset(headerList, 0, sizeof(Primitive::Header) * headerCount);

		return headerList;
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

	// FUNCTION: TOY2 0x004B32C0
	void Primitive::ReleaseAllVertexBuffers(Primitive* primitive)
	{
		for (; primitive; primitive = primitive->listNext)
			Patch::PatchVertices::ReleaseBuffer(&primitive->patchVerts);
	}

	// FUNCTION: TOY2 0x004B32E0
	int32_t Primitive::CreateAllVertexBuffers(Primitive* primitive, int32_t flags)
	{
		int32_t allCreated = 1;

		for (; primitive; primitive = primitive->listNext)
			allCreated &= Patch::PatchVertices::CreateVertexBuffer(&primitive->patchVerts, flags);

		return allCreated;
	}

	// FUNCTION: TOY2 0x004B3320
	int32_t Primitive::UpdateAllVertexBuffers(Primitive* primitive)
	{
		int32_t allUpdated = 1;

		for (; primitive; primitive = primitive->listNext)
			allUpdated &= Patch::PatchVertices::UpdateVertexBuffer(&primitive->patchVerts);

		return allUpdated;
	}

	// FUNCTION: TOY2 0x004CC480
	void Primitive::ComputeBounds(Primitive* primitive)
	{
		float originRadiusSq = 0.0f;
		Vector3F minimum = { FLT_MAX, FLT_MAX, FLT_MAX };
		Vector3F maximum = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		Primitive* current;
		for (current = primitive; current; current = current->listNext)
		{
			Vertex* vertices = current->patchVerts.data.vertices;

			// Draw types 4 and 5 encode a radial four-vertex envelope in each header.
			// The first index is its origin and the second is the first envelope vertex.
			if (current->header->drawType >= 4 && current->header->drawType <= 5)
			{
				for (int32_t headerIndex = 0; headerIndex < current->headerCount; headerIndex++)
				{
					Header* header = &current->header[headerIndex];
					uint16_t* indices = header->indices;
					float envelopeRadiusSq = 0.0f;

					for (int32_t vertexIndex = indices[1]; vertexIndex < indices[1] + 4; vertexIndex++)
					{
						Vector3F* position = &vertices[vertexIndex].position;
						float distanceSq = position->x * position->x + position->y * position->y + position->z * position->z;

						if (distanceSq > envelopeRadiusSq)
							envelopeRadiusSq = distanceSq;
					}

					Vector3F* envelopeOrigin = &vertices[indices[0]].position;
					Vector3F radialOffset;
					Vector3F boundPoint;
					Math::VectorNormalize(&radialOffset, envelopeOrigin);
					Math::ScaleVector(&radialOffset, &radialOffset, sqrt(envelopeRadiusSq));
					Math::VertexAdd(&boundPoint, &radialOffset, envelopeOrigin);

					if (boundPoint.x < minimum.x)
						minimum.x = boundPoint.x;
					if (boundPoint.y < minimum.y)
						minimum.y = boundPoint.y;
					if (boundPoint.z < minimum.z)
						minimum.z = boundPoint.z;
					if (boundPoint.x > maximum.x)
						maximum.x = boundPoint.x;
					if (boundPoint.y > maximum.y)
						maximum.y = boundPoint.y;
					if (boundPoint.z > maximum.z)
						maximum.z = boundPoint.z;

					float distanceSq = boundPoint.x * boundPoint.x + boundPoint.y * boundPoint.y + boundPoint.z * boundPoint.z;
					if (distanceSq > originRadiusSq)
						originRadiusSq = distanceSq;
				}
			}
			else
			{
				for (int32_t vertexIndex = 0; vertexIndex < current->patchVerts.vertexCount; vertexIndex++)
				{
					Vector3F* position = &vertices[vertexIndex].position;

					if (position->x < minimum.x)
						minimum.x = position->x;
					if (position->y < minimum.y)
						minimum.y = position->y;
					if (position->z < minimum.z)
						minimum.z = position->z;
					if (position->x > maximum.x)
						maximum.x = position->x;
					if (position->y > maximum.y)
						maximum.y = position->y;
					if (position->z > maximum.z)
						maximum.z = position->z;

					float distanceSq = position->x * position->x + position->y * position->y + position->z * position->z;
					if (distanceSq > originRadiusSq)
						originRadiusSq = distanceSq;
				}
			}
		}

		primitive->originRadiusSq = originRadiusSq;
		primitive->originRadius = sqrt(originRadiusSq);
		primitive->boundsCenter.x = (maximum.x + minimum.x) * 0.5f;
		primitive->boundsCenter.y = (maximum.y + minimum.y) * 0.5f;
		primitive->boundsCenter.z = (maximum.z + minimum.z) * 0.5f;

		float boundSphereRadiusSq = 0.0f;

		for (current = primitive; current; current = current->listNext)
		{
			Vertex* vertices = current->patchVerts.data.vertices;

			if (current->header->drawType >= 4 && current->header->drawType <= 5)
			{
				for (int32_t headerIndex = 0; headerIndex < current->headerCount; headerIndex++)
				{
					Header* header = &current->header[headerIndex];
					uint16_t* indices = header->indices;
					float envelopeRadiusSq = 0.0f;

					for (int32_t vertexIndex = indices[1]; vertexIndex < indices[1] + 4; vertexIndex++)
					{
						Vector3F* position = &vertices[vertexIndex].position;
						float distanceSq = position->x * position->x + position->y * position->y + position->z * position->z;

						if (distanceSq > envelopeRadiusSq)
							envelopeRadiusSq = distanceSq;
					}

					Vector3F* envelopeOrigin = &vertices[indices[0]].position;
					Vector3F centerOffset;
					Vector3F radialOffset;
					Vector3F boundPoint;
					Math::VertexSubtract(&centerOffset, envelopeOrigin, &primitive->boundsCenter);
					Math::VectorNormalize(&radialOffset, &centerOffset);
					Math::ScaleVector(&radialOffset, &radialOffset, sqrt(envelopeRadiusSq));
					Math::VertexAdd(&boundPoint, &radialOffset, envelopeOrigin);

					float x = boundPoint.x - primitive->boundsCenter.x;
					float y = boundPoint.y - primitive->boundsCenter.y;
					float z = boundPoint.z - primitive->boundsCenter.z;
					float distanceSq = x * x + y * y + z * z;

					if (distanceSq > boundSphereRadiusSq)
						boundSphereRadiusSq = distanceSq;
				}
			}
			else
			{
				for (int32_t vertexIndex = 0; vertexIndex < current->patchVerts.vertexCount; vertexIndex++)
				{
					Vector3F offset;
					Math::VertexSubtract(&offset, &vertices[vertexIndex].position, &primitive->boundsCenter);
					float distanceSq = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;

					if (distanceSq > boundSphereRadiusSq)
						boundSphereRadiusSq = distanceSq;
				}
			}
		}

		primitive->boundSphereRadiusSq = boundSphereRadiusSq;
		primitive->boundSphereRadius = sqrt(boundSphereRadiusSq);
	}
}

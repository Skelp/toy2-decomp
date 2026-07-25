#include "Renderer/Vertices.h"
#include "Renderer/Renderer.h"
#include "Nu3D/Patch.h"
#include "Nu3D/Nu3D.h"
#include <directx6/ddraw.h>

namespace Renderer
{
	namespace Vertices
	{
		// FUNCTION: TOY2 0x004B6ED0
		void ApplyOffset(Nu3D::Patch::PatchVertices* vertices, float du, float dv)
		{
			if (g_FVF_14C_Buffer_2.vertexBuffer != 0)
			{
				void* lockedData;
				if (DrawingAPI::LockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, &lockedData, 0) == DD_OK)
				{
					Nu3D::Vertex* src = vertices->data.vertices;
					Nu3D::VertexTL* dst = (Nu3D::VertexTL*)lockedData;
					int count = vertices->vertexCount;
					for (int i = 0; i < count; i++)
					{
						dst[i].uv.x = du + src[i].coords.x;
						dst[i].uv.y = dv + src[i].coords.y;
					}
					DrawingAPI::UnlockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer);
				}
			}
		}
	}
}

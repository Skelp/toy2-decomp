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
					for (int i = 0; i < vertices->vertexCount; i++)
					{
						dst[i].uv.x = du + src[i].coords.x;
						dst[i].uv.y = dv + src[i].coords.y;
					}
					DrawingAPI::UnlockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer);
				}
			}
		}

		// FUNCTION: TOY2 0x004B7830
		void ModuleColor(Nu3D::Patch::PatchVertices* vertices, int32_t red, int32_t green, int32_t blue)
		{
			if (g_FVF_14C_Buffer_2.vertexBuffer != 0)
			{
				void* lockedData;
				if (DrawingAPI::LockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, &lockedData, 0) == DD_OK)
				{
					Nu3D::VertexTL* dst = (Nu3D::VertexTL*)lockedData;
					for (int i = 0; i < vertices->vertexCount; i++)
					{
						int r = dst[i].diffuse.r * red;
						if ((r & 0xfffff000) <= 0xff000)
							dst[i].diffuse.r = (uint8_t)(r >> 12);
						else
							dst[i].diffuse.r = 0xff;

						int g = dst[i].diffuse.g * green;
						if ((g & 0xfffff000) <= 0xff000)
							dst[i].diffuse.g = (uint8_t)(g >> 12);
						else
							dst[i].diffuse.g = 0xff;

						int b = dst[i].diffuse.b * blue;
						if ((b & 0xfffff000) <= 0xff000)
							dst[i].diffuse.b = (uint8_t)(b >> 12);
						else
							dst[i].diffuse.b = 0xff;
					}
					DrawingAPI::UnlockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer);
				}
			}
		}
	}
}

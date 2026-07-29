#include "Renderer/Vertices.h"
#include "Renderer/Renderer.h"
#include "Nu3D/Camera.h"
#include "Nu3D/Math.h"
#include "Nu3D/Patch.h"
#include "Nu3D/Nu3D.h"
#include <directx6/ddraw.h>

namespace Renderer
{
	namespace Vertices
	{
		// FUNCTION: TOY2 0x004B6ED0 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x004B6F50 [PROVISIONAL]
		void ProjectToScreen(Nu3D::Patch::PatchVertices* vertices, D3DMATRIX* transforms, int32_t transformCount)
		{
			if (g_FVF_14C_Buffer_2.vertexBuffer != 0)
			{
				Nu3D::VertexTL* destVertices;
				if (DrawingAPI::LockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, (LPVOID*)&destVertices, 0) == DD_OK)
				{
					int32_t verticesPerTransform = transformCount == 2 ? vertices->vertexCount / 2 : 1;
					int32_t sourceOffset = 0;
					int32_t destOffset = 0;

					for (int32_t transformIndex = 0; transformIndex < transformCount; ++transformIndex)
					{
						D3DMATRIX combined;
						Nu3D::Math::MultiplyMatrix3x4(&combined, &transforms[transformIndex], Nu3D::Camera::GetViewMatrix());
						float m11 = combined._11;
						float m21 = combined._21;
						float m31 = combined._31;
						float m12 = combined._12;
						float m22 = combined._22;
						float m32 = combined._32;

						Nu3D::Vertex* sourceVertices = vertices->data.vertices + sourceOffset;
						for (int32_t vertexIndex = 0; vertexIndex < verticesPerTransform; ++vertexIndex)
						{
							Vector3F& normal = sourceVertices[vertexIndex].normals;
							Nu3D::VertexTL& dest = destVertices[destOffset + vertexIndex];
							dest.uv.x = (normal.x * m11 + normal.y * m21 + normal.z * m31 + 1.0f) * 0.5f;
							dest.uv.y = (1.0f - (normal.x * m12 + normal.y * m22 + normal.z * m32)) * 0.5f;
							dest.diffuse.a = 0x80;
						}

						if (transformCount == 2)
						{
							sourceOffset += verticesPerTransform;
							destOffset = 32;
						}
						else
						{
							++sourceOffset;
							++destOffset;
						}
					}

					DrawingAPI::UnlockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer);
				}
			}
		}

		// FUNCTION: TOY2 0x004B75E0 [PROVISIONAL]
		void ProjectCustomTextureCoordinates(
			Nu3D::Patch::PatchVertices* vertices, D3DMATRIX* transform, const Nu3D::InstanceData::TextureProjectionData* projection)
		{
			if (g_FVF_14C_Buffer_2.vertexBuffer != 0)
			{
				Nu3D::VertexTL* destVertices;
				if (DrawingAPI::LockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, (LPVOID*)&destVertices, 0) == DD_OK)
				{
					D3DMATRIX combined;
					Nu3D::Math::FullMatrixMultiply(&combined, transform, &projection->matrix);

					Vector3F projectedReference;
					Nu3D::Math::TransformPointPerspective(&projectedReference, &projection->referencePoint, &combined);

					Nu3D::Vertex* sourceVertex = vertices->data.vertices;
					for (int32_t vertexIndex = 0; vertexIndex < vertices->vertexCount; ++vertexIndex)
					{
						Vector3F projectedPosition;
						Nu3D::Math::ProjectPoint(&projectedPosition, &sourceVertex->position, &combined);
						destVertices[vertexIndex].uv.x = (projectedPosition.x + 1.0f) * 0.5f;
						destVertices[vertexIndex].uv.y = 1.0f - (projectedPosition.y + 1.0f) * 0.5f;

						Nu3D::Math::TransformPointPerspective(&projectedPosition, &sourceVertex->normals, &combined);
						destVertices[vertexIndex].uv.x += (projectedReference.x - projectedPosition.x) * projection->scale;
						destVertices[vertexIndex].uv.y -= (projectedReference.y - projectedPosition.y) * projection->scale;
						++sourceVertex;
					}

					DrawingAPI::UnlockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer);
				}
			}
		}

		// FUNCTION: TOY2 0x004B7710 [PROVISIONAL]
		void ProjectTex14Coordinates(Nu3D::Patch::PatchVertices* vertices, D3DMATRIX* transform)
		{
			if (g_FVF_14C_Buffer_2.vertexBuffer != 0)
			{
				Nu3D::VertexTL* destVertex;
				if (DrawingAPI::LockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer, DDLOCK_WAIT | DDLOCK_NOSYSLOCK, (LPVOID*)&destVertex, 0) == DD_OK)
				{
					D3DMATRIX combined;
					Nu3D::Math::MultiplyMatrix3x4(&combined, transform, Nu3D::Camera::GetViewMatrix());

					Nu3D::Vertex* sourceVertex = vertices->data.vertices;
					for (int32_t vertexIndex = 0; vertexIndex < vertices->vertexCount; ++vertexIndex)
					{
						Vector3F& normal = sourceVertex->normals;
						destVertex->uv.x = (normal.x * combined._11 + normal.y * combined._21 + normal.z * combined._31 + 1.0f) * 0.5f;
						destVertex->uv.y = (1.0f - (normal.x * combined._12 + normal.y * combined._22 + normal.z * combined._32)) * 0.5f;
						destVertex->diffuse.a = 0x60;
						++sourceVertex;
						++destVertex;
					}

					DrawingAPI::UnlockVertexBuffer(g_FVF_14C_Buffer_2.vertexBuffer);
				}
			}
		}

		// FUNCTION: TOY2 0x004B7830 [PROVISIONAL]
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

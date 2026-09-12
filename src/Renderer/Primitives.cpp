#include "Renderer/Renderer.h"
#include "Renderer/RendererInternal.h"
#include "SoftwareRenderer.h"
#include "DrawingDevice.h"
#include "Nu3D/Light.h"
#include "Nu3D/Font.h"
#include "Nu3D/Camera.h"
#include "Nu3D/BmpDataNode.h"
#include "Nu3D/Material.h"
#include "Nu3D/Math.h"
#include "NGNLoader/NGNLoader.h"
#include "Nu3D/Patch.h"
#include "Nu3D/Primitive.h"
#include "Nu3D/Particles.h"
#include "Nu3D/Scene.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"
#include "Toy2/Toy2.h"
#include "Toy2/Weather.h"
#include "Renderer/Glue.h"
#include "Toy2/Camera.h"
#include "Toy2/Collision.h"
#include "Toy2/Direct6.h"
#include "Logger.h"
#include <cstdarg>
#include <cstring>
#include <cstdio>

// The primitive flush and draw: retail holds 0x004B5CE0 through 0x004B5E20 as
// one object, in the order of this file.
namespace Renderer
{
	// GLOBAL: TOY2 0x0094FCE0
	Nu3D::VertexTL g_primitiveVertices[1500];

	// FUNCTION: TOY2 0x004B5CE0 [MATCHED]
	int32_t Set508718(int32_t value)
	{
		int32_t previousValue = g_primitiveRenderFlags;
		g_primitiveRenderFlags = value;
		return previousValue;
	}

	// FUNCTION: TOY2 0x004B5CF0 [PROVISIONAL]
	void FlushPrimitives()
	{
		int32_t vertexCount = 0;
		int32_t currentRenderFlags;
		int32_t currentTextureIndex;
		Nu3D::Material* currentMaterial;

		void** bucket = &g_renderBuckets[1024];
		do
		{
			SortedPrimitive* primitive = static_cast<SortedPrimitive*>(*--bucket);
			while (primitive != 0)
			{
				bool stateChanged = vertexCount >= 1500;
				if (vertexCount != 0 && ! stateChanged)
				{
					if (primitive->renderEntry != 0)
					{
						stateChanged = currentMaterial != primitive->renderEntry->material || currentRenderFlags != primitive->renderFlags;
					}
					else
					{
						stateChanged = currentRenderFlags != primitive->renderFlags || currentTextureIndex != primitive->textureIndex || currentMaterial != 0;
					}
				}

				if (stateChanged)
				{
					DrawPrimitive(g_primitiveVertices, vertexCount);
					vertexCount = 0;
				}

				if (vertexCount == 0)
				{
					if (primitive->renderEntry != 0)
					{
						BindMaterial(primitive->renderEntry->material, 0);
						SetupMaterialRenderState(primitive->renderEntry->material, primitive->renderFlags);
						currentRenderFlags = primitive->renderFlags;
						currentMaterial = primitive->renderEntry->material;
					}
					else
					{
						InitRenderState(primitive->renderFlags);
						BindTexture(primitive->textureIndex);
						currentRenderFlags = primitive->renderFlags;
						currentTextureIndex = primitive->textureIndex;
						currentMaterial = 0;
					}
				}

				memcpy(&g_primitiveVertices[vertexCount], &primitive->v0, sizeof(Nu3D::VertexTL) * 3);
				vertexCount += 3;
				primitive = primitive->next;
			}

			*bucket = 0;
		} while (bucket != g_renderBuckets);

		DrawPrimitive(g_primitiveVertices, vertexCount);
		g_primitiveBufferFreeCount = 3000;
	}

	// FUNCTION: TOY2 0x004B5E20 [MATCHED]
	void DrawPrimitive(void* vertices, DWORD vertexCount)
	{
		if (vertexCount != 0)
			DrawingDevice::DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_0x1C4, vertices, vertexCount, 0x10);
	}
}

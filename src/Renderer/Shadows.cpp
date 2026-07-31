#include "Renderer/Shadows.h"
#include "NGNLoader/NGNLoader.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"

namespace Renderer
{
	namespace Shadows
	{
		enum
		{
			DEVICE_BLEND_CAP_INV_SRC_COLOR = 0x4,
		};

		// GLOBAL: TOY2 0x0054DEB0
		int16_t g_shadowCount;

		// GLOBAL: TOY2 0x0054DD64
		int32_t g_unusedShadowVar;

		// GLOBAL: TOY2 0x0054F098
		ShadowInstance g_shadowInstances[MAX_SHADOWS];

		// GLOBAL: TOY2 0x0054DEC8
		ShadowProjection g_shadowProjections[MAX_SHADOWS];

		// FUNCTION: TOY2 0x00445720 [PROVISIONAL]
		void DrawAll()
		{
			uint32_t bitmapWidth = 0xFF;
			uint32_t bitmapHeight = 0xFF;
			RGBA color;
			color.value = (g_deviceBlendShadeCapsCpy & DEVICE_BLEND_CAP_INV_SRC_COLOR) != 0 ? 0x00FFFFFF : 0x88000000;

			int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(31);
			if (textureDataIndex != 0)
				NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

			for (int32_t shadowIndex = 0; shadowIndex < g_shadowCount; shadowIndex++)
			{
				Vector3F position;
				position.x = (float)(g_shadowInstances[shadowIndex].pos.x >> 5);
				position.y = (float)(g_shadowInstances[shadowIndex].pos.y >> 5) - 10.0f;
				position.z = (float)(g_shadowInstances[shadowIndex].pos.z >> 5);
				float size = (float)g_shadowInstances[shadowIndex].size;

				Vector3F vertices[4];
				vertices[0].x = position.x - size;

				if (g_shadowInstances[shadowIndex].size > 0)
				{
					color.a = 0x20;
					vertices[0].y = position.y;
					vertices[0].z = position.z + size;
					vertices[1].x = vertices[0].x;
					vertices[1].y = position.y;
					vertices[1].z = position.z - size;
					vertices[2].x = position.x + size;
					vertices[2].y = position.y;
					vertices[3].x = position.x + size;
					vertices[2].z = vertices[0].z;
					vertices[3].y = position.y;
					vertices[3].z = vertices[1].z;
				}
				else
				{
					vertices[0].z = position.z - size;
					vertices[1].x = vertices[0].x;
					vertices[1].y = position.y + (float)g_shadowProjections[shadowIndex].cornerYOffsets[0];
					vertices[1].z = position.z + size;
					vertices[2].x = position.x + size;
					vertices[2].y = position.y + (float)g_shadowProjections[shadowIndex].cornerYOffsets[1];
					vertices[0].y = position.y + (float)g_shadowProjections[shadowIndex].cornerYOffsets[2];
					color.a = (uint8_t)g_shadowProjections[shadowIndex].opacity;
					vertices[2].z = vertices[0].z;
					vertices[3].x = vertices[2].x;
					vertices[3].y = position.y;
					vertices[3].z = vertices[1].z;
				}
				Vector2F uvTopLeft = { 96.0f / (float)(int32_t)bitmapWidth, 96.0f / (float)(int32_t)bitmapHeight };
				Vector2F uvBottomRight = { 127.0f / (float)(int32_t)bitmapWidth, 127.0f / (float)(int32_t)bitmapHeight };
				if ((g_deviceBlendShadeCapsCpy & DEVICE_BLEND_CAP_INV_SRC_COLOR) != 0)
				{
					Sprite::QueueQuadSpriteFromVerts(
						vertices, &uvTopLeft, &uvBottomRight, textureDataIndex, color, RENDER_ZWRITE | RENDER_ZBIAS_2 | RENDER_CULL_NONE | RENDER_ALPHA_ALT);
				}
				else
				{
					Sprite::QueueQuadSpriteFromVerts(vertices,
						&uvTopLeft,
						&uvBottomRight,
						textureDataIndex,
						color,
						RENDER_ZWRITE | RENDER_ZBIAS_2 | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT);
				}
			}
		}
	}
}

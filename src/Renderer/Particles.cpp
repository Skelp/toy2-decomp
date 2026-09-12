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

// The particle draw of the renderer: retail holds 0x00445980 as one object.
namespace Renderer
{
	namespace Particles
	{
		// FUNCTION: TOY2 0x00445980 [PROVISIONAL]
		void DrawParticles()
		{
			uint32_t bitmapWidth = 255;
			uint32_t bitmapHeight = 255;
			for (Nu3D::Particles::ParticleInstance* particle = Nu3D::Particles::g_particleInstances; particle < Nu3D::Particles::g_particleInstances + 64;
				particle++)
			{
				if (particle->lifetime == 0 || particle->spriteSheet == 0)
					continue;

				particle->renderFlags &= ~RENDER_BILINEAR_FILTER;
				SpriteSheet* sheet = g_spriteSheets[particle->spriteSheet];
				int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
				if (textureDataIndex != 0)
					NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

				Vector2F uvTopLeft;
				uvTopLeft.x = (float)sheet->tiles[particle->tileIndex].x / (float)(int32_t)bitmapWidth;
				uvTopLeft.y = (float)sheet->tiles[particle->tileIndex].y / (float)(int32_t)bitmapHeight;

				Vector2F uvBottomRight;
				uvBottomRight.x = ((float)sheet->tileWidth + (float)sheet->tiles[particle->tileIndex].x) / (float)(int32_t)bitmapWidth;
				uvBottomRight.y = ((float)sheet->tileHeight + (float)sheet->tiles[particle->tileIndex].y) / (float)(int32_t)bitmapHeight;

				RGBA color;
				color.r = particle->colourR;
				color.g = particle->colourG;
				color.b = particle->colourB;
				color.a = 255;

				Vector3F position;
				position.x = (float)particle->pos.x * LensFlare::k_positionScale;
				position.y = (float)particle->pos.y * LensFlare::k_positionScale;
				position.z = (float)particle->pos.z * LensFlare::k_positionScale;
				int32_t trigIndex = particle->groundAlignRot * 16;
				uint16_t particleRenderFlags = particle->renderFlags;
				int32_t flags;
				switch (particleRenderFlags & Nu3D::Particles::PARTICLE_RENDER_ALPHA_MODE_MASK)
				{
					case 0:
						color.a = 64;
						flags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;
						break;
					case 0x20:
						color.a = 255;
						flags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_CUSTOM;
						break;
					case 0x40:
						color.a = 255;
						flags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_ALT;
						break;
					case 0x60:
						color.a = 255;
						flags = RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT;
						break;
				}

				if ((particleRenderFlags & RENDER_CULL_FRONT) != 0)
				{
					position.y -= 10.0f;
					particle->renderFlags = particleRenderFlags + RENDER_BILINEAR_FILTER;
					Sprite::QueueGroundAlignedSprite(
						&position, -trigIndex, (float)particle->width, (float)particle->height, &uvTopLeft, &uvBottomRight, textureDataIndex, color, flags);
				}
				else
				{
					particle->renderFlags = particleRenderFlags + RENDER_BILINEAR_FILTER;
					Sprite::QueueQuadSprite(
						&position, trigIndex, (float)particle->width, (float)particle->height, &uvTopLeft, &uvBottomRight, textureDataIndex, color, flags);
				}
			}
		}
	}
}

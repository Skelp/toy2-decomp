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

// The falling particles and the lens flares: retail holds 0x0044F010 through
// 0x0044F580 as one object, in the order of this file.
namespace Renderer
{
	// FUNCTION: TOY2 0x0044F010 [PROVISIONAL]
	void DrawFallingParticles()
	{
		uint32_t bitmapWidth = 0xFF;
		uint32_t bitmapHeight = 0xFF;
		if (Toy2::Weather::g_precipitationParticles != 0)
		{
			SpriteSheet* sheet = g_spriteSheets[Toy2::Weather::g_precipitationSpriteSheetIndex];
			if (sheet != 0)
			{
				RGBA color = { 0x40, 0x40, 0x40, 0xFF };
				int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
				if (textureDataIndex != 0)
					NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

				Vector2F uvTopLeft;
				uvTopLeft.x = (float)sheet->tiles[0].x / (int32_t)bitmapWidth;
				float bitmapHeightFloat = (float)(int32_t)bitmapHeight;
				uvTopLeft.y = (float)sheet->tiles[0].y / bitmapHeightFloat;
				Vector2F uvBottomRight;
				uvBottomRight.x = ((float)sheet->tileWidth + sheet->tiles[0].x) / (int32_t)bitmapWidth;
				uvBottomRight.y = ((float)sheet->tileHeight + sheet->tiles[0].y) / bitmapHeightFloat;

				for (int32_t particleIndex = 0; particleIndex < 64; particleIndex++)
				{
					Toy2::Weather::PrecipitationParticle* particle = &Toy2::Weather::g_precipitationParticles[particleIndex];
					if (particle->terminalY != 0)
					{
						Vector3F position;
						position.x = (float)(particle->position.x >> 5);
						position.y = (float)(particle->position.y >> 5);
						position.z = (float)(particle->position.z >> 5);
						Sprite::QueueBillboardSprite(&position,
							0,
							30.0f,
							200.0f,
							&uvTopLeft,
							&uvBottomRight,
							textureDataIndex,
							color,
							RENDER_CULL_NONE | RENDER_ZWRITE | RENDER_ALPHA_CUSTOM);
					}
				}
			}
		}
	}

	// FUNCTION: TOY2 0x0044F190 [MATCHED]
	void DrawLensFlares()
	{
		const uint32_t lensFlaresEnabled = 1;
		if ((Toy2::g_toyCfgData.flags & lensFlaresEnabled) != 0)
		{
			LensFlare::CullAndQueue();
			LensFlare::g_bufferIndex = 0;
			if (LensFlare::g_bufferActive[0] != 0)
			{
				for (int32_t slotIndex = 0; slotIndex < LensFlare::g_slotCounts[LensFlare::g_bufferIndex]; slotIndex++)
				{
					LensFlare::RenderSlot(slotIndex);
				}
			}
		}

		LensFlare::g_bufferActive[LensFlare::g_bufferIndex] = 0;
		LensFlare::g_slotCounts[LensFlare::g_bufferIndex] = 0;
		LensFlare::g_registeredLightCount = 0;
	}

}

namespace Renderer
{
	namespace LensFlare
	{
		// FUNCTION: TOY2 0x0044F200 [PROVISIONAL]
		void RegisterLight(int32_t x, int32_t y, int32_t z, int32_t red, int32_t green, int32_t blue, int32_t scaleOffset)
		{
			if (g_registeredLightCount >= 8)
				return;

			Vector3F projected;
			Vector3F position;
			Vector3I movement;
			Vector3I cameraPosition;
			cameraPosition = Toy2::Camera::g_renderCameraTransform.pos;
			movement.x = (x - cameraPosition.x) * 9 / 10;
			movement.y = (y - cameraPosition.y) * 9 / 10;
			movement.z = (z - cameraPosition.z) * 9 / 10;
			if (Toy2::Collision::SweepAndSlide(&cameraPosition, &movement, 0x8000, 0, 1) != 0)
				return;

			position.x = (float)x * k_positionScale;
			position.y = (float)y * k_positionScale;
			position.z = (float)z * k_positionScale;
			Nu3D::TransformPointProjective(&projected, &position, 1, 0);

			int32_t screenX = (int32_t)(projected.x * g_virtualScreenWidth / (float)DrawingDevice::GetDestWidth());
			int32_t screenY = (int32_t)(projected.y * g_virtualScreenHeight / (float)DrawingDevice::GetDestHeight());
			int32_t depth = (int32_t)((Nu3D::Camera::g_currentCamera->farClip - Nu3D::Camera::g_currentCamera->nearClip) * projected.z
				+ Nu3D::Camera::g_currentCamera->nearClip);
			if (screenX > 512 || screenX < 0 || screenY > 256 || screenY < 0 || depth <= 128 || projected.z <= 0.0f || projected.z >= 1.0f)
				return;

			const int32_t lightIndex = g_registeredLightCount;
			g_registeredLights[lightIndex].position.x = x;
			g_registeredLights[lightIndex].position.y = y;
			g_registeredLights[lightIndex].position.z = z;
			g_registeredLights[lightIndex].screenX = screenX;
			g_registeredLights[lightIndex].screenY = screenY;
			g_registeredLights[lightIndex].depth = depth;
			g_registeredLights[lightIndex].red = red;
			g_registeredLights[lightIndex].green = green;
			g_registeredLights[lightIndex].blue = blue;
			g_registeredLights[lightIndex].scaleOffset = scaleOffset;
			g_registeredLightCount = lightIndex + 1;
		}

		// FUNCTION: TOY2 0x0044F420 [EFFECTIVE]
		void CullAndQueue()
		{
			if (g_registeredLightCount > 0)
			{
				const int32_t bufferIndex = g_bufferIndex;
				RegisteredLight* light = g_registeredLights;
				int32_t remainingLights = g_registeredLightCount;
				do
				{
					const int32_t screenX = light->screenX;
					const int32_t screenY = light->screenY;
					const int32_t depth = light->depth;
					if (screenX <= 512 && screenX >= 0 && screenY <= 256 && screenY >= 0 && depth > 128)
					{
						const int32_t screenXFixed = screenX * 8;
						const int32_t screenYFixed = screenY * 8;
						const int32_t centerOffsetX = 256 - screenX;
						const int32_t centerOffsetY = 128 - screenY;
						int32_t distanceSquared = centerOffsetY * centerOffsetY + centerOffsetX * centerOffsetX;
						if (distanceSquared > 0xFFFF)
							distanceSquared = 0xFFFF;

						const int32_t slotIndex = g_slotCounts[bufferIndex];
						if (slotIndex < 8 && 0x103FF - distanceSquared > 0 && depth - 0x60 > 0x100)
						{
							g_bufferActive[bufferIndex] = 1;
							g_slots[bufferIndex][slotIndex].position.x = light->position.x;
							g_slots[bufferIndex][slotIndex].position.y = light->position.y;
							g_slots[bufferIndex][slotIndex].position.z = light->position.z;
							g_slots[bufferIndex][slotIndex].screenXFixed = screenXFixed;
							g_slots[bufferIndex][slotIndex].screenYFixed = screenYFixed;
							g_slots[bufferIndex][slotIndex].centerOffsetX = centerOffsetX;
							g_slots[bufferIndex][slotIndex].centerOffsetY = centerOffsetY;
							g_slots[bufferIndex][slotIndex].red = light->red;
							g_slots[bufferIndex][slotIndex].green = light->green;
							g_slots[bufferIndex][slotIndex].blue = light->blue;
							g_slots[bufferIndex][slotIndex].scaleOffset = light->scaleOffset;
							g_slotCounts[bufferIndex] = slotIndex + 1;
						}
					}

					light++;
					remainingLights--;
				} while (remainingLights != 0);
			}
		}

		// FUNCTION: TOY2 0x0044F580 [PROVISIONAL]
		void RenderSlot(int32_t slotIndex)
		{
			Vector3F position = {
				(float)g_slots[g_bufferIndex][slotIndex].position.x * k_positionScale,
				(float)g_slots[g_bufferIndex][slotIndex].position.y * k_positionScale,
				(float)g_slots[g_bufferIndex][slotIndex].position.z * k_positionScale,
			};
			Vector3F projected;
			Nu3D::TransformPointProjective(&projected, &position, 1, 0);

			int32_t screenX = (int32_t)(projected.x * g_virtualScreenWidth / (float)DrawingDevice::GetDestWidth());
			int32_t screenY = (int32_t)(projected.y * g_virtualScreenHeight / (float)DrawingDevice::GetDestHeight());
			int32_t depth = (int32_t)(projected.z * k_depthScale + k_depthOffset);
			int32_t screenXFixed = screenX * 8;
			int32_t screenYFixed = screenY * 8;
			int32_t centerOffsetX = 256 - screenX;
			int32_t centerOffsetY = 128 - screenY;
			int32_t distanceSquared = centerOffsetY * centerOffsetY + centerOffsetX * centerOffsetX;
			if (distanceSquared > 0xFFFF)
				distanceSquared = 0xFFFF;

			const int32_t brightness = 0x103FF - distanceSquared;
			const int32_t flareScale = brightness / (depth / 2 + 1) + g_slots[g_bufferIndex][slotIndex].scaleOffset;
			const int32_t red = g_slots[g_bufferIndex][slotIndex].red * brightness;
			const int32_t green = g_slots[g_bufferIndex][slotIndex].green * brightness;
			const int32_t blue = g_slots[g_bufferIndex][slotIndex].blue * brightness;
			const Element* element = g_elements;
			int32_t remainingElements = 17;
			do
			{
				if (element->spriteSheetIndex != 0)
				{
					const int32_t scaleX = element->scaleX * flareScale;
					const int32_t scaleY = element->scaleY * flareScale;
					Sprite::DrawScaled((int16_t)(screenXFixed >> 3) - (int16_t)(scaleX / 256),
						(int16_t)(screenYFixed >> 3) - (int16_t)(scaleY / 256),
						element->spriteSheetIndex,
						0,
						element->red * red >> 23,
						element->green * green >> 23,
						element->blue * blue >> 23,
						0x20,
						scaleX,
						scaleY);
				}

				screenXFixed += centerOffsetX;
				screenYFixed += centerOffsetY;
				element++;
				remainingElements--;
			} while (remainingElements != 0);
		}
	}
}

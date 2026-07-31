#include "Renderer/Renderer.h"
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

namespace Renderer
{
	// FUNCTION: TOY2 0x0049D390 [PROVISIONAL]
	void DrawBitmapText(const char* text, int32_t screenY, int32_t screenX, uint32_t red, uint32_t green, uint32_t blue, uint32_t flags)
	{
		int32_t textLength = 0;
		while (text[textLength] != '\0')
			++textLength;
		int32_t penX = screenX - textLength * 13 / 2;

		for (int32_t characterIndex = 0; characterIndex < textLength; ++characterIndex, penX += 13)
		{
			uint8_t character = text[characterIndex];
			int32_t glyphX;
			int32_t glyphY;

			switch (character)
			{
				case '!':
					glyphX = 96;
					glyphY = 160;
					break;
				case '\'':
					glyphX = 224;
					glyphY = 160;
					break;
				case '(':
					glyphX = 160;
					glyphY = 160;
					break;
				case ')':
					glyphX = 192;
					glyphY = 160;
					break;
				case ',':
					glyphX = 160;
					glyphY = 128;
					break;
				case '.':
					glyphX = 128;
					glyphY = 128;
					break;
				case '/':
					glyphX = 32;
					glyphY = 160;
					break;
				case ':':
					glyphX = 224;
					glyphY = 128;
					break;
				case ';':
					glyphX = 192;
					glyphY = 128;
					break;
				case '?':
					glyphX = 128;
					glyphY = 160;
					break;
				case '\\':
					glyphX = 0;
					glyphY = 160;
					break;
				case 0xb2:
					glyphX = 96;
					glyphY = 64;
					break;
				case 0xb3:
					glyphX = 112;
					glyphY = 64;
					break;
				case 0xb9:
					glyphX = 80;
					glyphY = 64;
					break;
				case 0xba:
					glyphX = 64;
					glyphY = 64;
					break;
				default: {
					int32_t glyphIndex;
					if (character >= 'a' && character <= 'z')
						glyphIndex = character - 'a';
					else if (character >= 'A' && character <= 'Z')
						glyphIndex = character - 'A';
					else if (character >= '0' && character <= '9')
						glyphIndex = character - 22;
					else
						continue;

					glyphX = (glyphIndex % 8) * 32;
					glyphY = (glyphIndex / 8) * 32;
					break;
				}
			}

			const float atlasScale = 1.0f / 255.0f;
			Vector2F uvTopLeft;
			uvTopLeft.x = glyphX * atlasScale;
			uvTopLeft.y = glyphY * atlasScale;
			Vector2F uvBottomRight;
			uvBottomRight.x = (glyphX + 31.0f) * atlasScale;
			uvBottomRight.y = (glyphY + 31.0f) * atlasScale;

			RGBA color;
			color.r = (uint8_t)red;
			color.g = (uint8_t)green;
			color.b = (uint8_t)blue;
			color.a = 255;

			int32_t textureIndex = NGNLoader::GetTextureDataIndex(31);
			Sprite::Queue2DSprite(penX * (1.0f / g_virtualScreenWidth),
				screenY * (1.0f / g_virtualScreenHeight),
				(1.0f / g_virtualScreenWidth) * 16.0f,
				(1.0f / g_virtualScreenHeight) * 16.0f,
				&uvTopLeft,
				&uvBottomRight,
				textureIndex,
				color,
				RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_DEFAULT);
		}
	}

	// FUNCTION: TOY2 0x0049B990 [MATCHED]
	void DrawFormattedText(int32_t screenX, int32_t screenY, const char* format, ...)
	{
		char text[512];
		va_list arguments;

		va_start(arguments, format);
		vsprintf(text, format, arguments);
		DrawBitmapText(text, screenY, screenX, 0xFF, 0xFF, 0xFF, 0x60);
	}

	namespace Beam
	{
		// GLOBAL: TOY2 0x004F72D4
		int32_t g_queueHead;

		// GLOBAL: TOY2 0x0054F640
		Command g_commands[100];

		// FUNCTION: TOY2 0x0044E100 [MATCHED]
		void QueueBeam(uint32_t spriteSheetIndex,
			uint32_t width,
			int32_t segmentLength,
			const Vector4I* position,
			const Vector4I* direction,
			uint32_t red,
			uint32_t green,
			uint32_t blue)
		{
			if (g_queueHead != 0)
			{
				g_queueHead--;
				g_commands[g_queueHead].spriteSheetIndex = spriteSheetIndex;
				g_commands[g_queueHead].width = width;
				g_commands[g_queueHead].segmentLength = segmentLength;
				g_commands[g_queueHead].position = *position;
				g_commands[g_queueHead].direction = *direction;
				g_commands[g_queueHead].color.r = red;
				g_commands[g_queueHead].color.g = green;
				g_commands[g_queueHead].color.b = blue;
			}
		}

		// FUNCTION: TOY2 0x0044E1A0 [PROVISIONAL]
		void DrawSegmentedBeam(uint32_t spriteSheetIndex,
			int32_t width,
			int32_t segmentLength,
			const Vector4I* position,
			const Vector4I* direction,
			uint32_t red,
			uint32_t green,
			uint32_t blue)
		{
			SpriteSheet* sheet = g_spriteSheets[spriteSheetIndex];
			uint32_t bitmapWidth = 255;
			uint32_t bitmapHeight = 255;
			int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
			if (textureDataIndex != 0)
				NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

			RGBA color;
			color.r = (uint8_t)red;
			color.g = (uint8_t)green;
			color.b = (uint8_t)blue;
			color.a = 255;

			Vector3F cameraPosition;
			Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &cameraPosition);

			Vector3F start;
			start.x = (float)position->x * LensFlare::k_positionScale;
			start.y = (float)position->y * LensFlare::k_positionScale;
			start.z = (float)position->z * LensFlare::k_positionScale;

			Vector3F beamDirection;
			beamDirection.x = (float)direction->x * LensFlare::k_positionScale;
			beamDirection.y = (float)direction->y * LensFlare::k_positionScale;
			beamDirection.z = (float)direction->z * LensFlare::k_positionScale;

			Nu3D::Math::VertexSubtract(&cameraPosition, &cameraPosition, &start);

			Vector3F halfWidth;
			Nu3D::Math::VertexCrossProduct(&halfWidth, &cameraPosition, &beamDirection);
			Nu3D::Math::VectorNormalize(&halfWidth, &halfWidth);
			Nu3D::Math::ScaleVector(&halfWidth, &halfWidth, (float)width);

			int32_t beamLength = (int32_t)Vector3F::Length(&beamDirection);
			float remainingLength = (float)beamLength;
			Nu3D::Math::ScaleVector(&beamDirection, &beamDirection, 1.0f / remainingLength);

			Vector3F segmentPoint;
			Nu3D::Math::ScaleVector(&segmentPoint, &beamDirection, remainingLength);
			Nu3D::Math::VertexAdd(&segmentPoint, &segmentPoint, &start);

			Vector3F vertices[4];
			Nu3D::Math::VertexSubtract(&vertices[2], &segmentPoint, &halfWidth);
			Nu3D::Math::VertexAdd(&vertices[3], &segmentPoint, &halfWidth);

			float segmentLengthF = (float)segmentLength;
			int32_t tileIndex = 2;
			do
			{
				float nextLength = remainingLength - segmentLengthF;
				if (nextLength < 0.0f)
					remainingLength = 0.0f;
				else
					remainingLength = nextLength;

				Nu3D::Math::ScaleVector(&segmentPoint, &beamDirection, remainingLength);
				Nu3D::Math::VertexAdd(&segmentPoint, &segmentPoint, &start);
				Nu3D::Math::VertexSubtract(&vertices[0], &segmentPoint, &halfWidth);
				Nu3D::Math::VertexAdd(&vertices[1], &segmentPoint, &halfWidth);

				Vector2F uvTopLeft;
				uvTopLeft.x = (float)sheet->tiles[tileIndex].x / (float)(int32_t)bitmapWidth;
				uvTopLeft.y = (float)sheet->tiles[tileIndex].y / (float)(int32_t)bitmapHeight;

				Vector2F uvBottomRight;
				uvBottomRight.x = ((float)sheet->tileWidth + (float)sheet->tiles[tileIndex].x) / (float)(int32_t)bitmapWidth;
				uvBottomRight.y = ((float)sheet->tiles[tileIndex].y + (float)sheet->tileHeight) / (float)(int32_t)bitmapHeight;

				Sprite::QueueQuadSpriteFromVerts(
					vertices, &uvTopLeft, &uvBottomRight, textureDataIndex, color, RENDER_ZWRITE | RENDER_CULL_NONE | RENDER_ALPHA_CUSTOM);

				vertices[2] = vertices[0];
				vertices[3] = vertices[1];
				tileIndex = 1;
			} while (remainingLength > 0.0f);
		}
	}

	namespace LensFlare
	{
		// GLOBAL: TOY2 0x004DC054
		const float k_positionScale = 0.03125f;

		// GLOBAL: TOY2 0x004DC084
		const float k_depthOffset = 50.0f;

		// GLOBAL: TOY2 0x004DC088
		const float k_depthScale = 47950.0f;

		// GLOBAL: TOY2 0x004F72D8
		const Element g_elements[17] = {
			{ 5, 72, 48, 96, 96, 96 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 7, 42, 28, 4, 4, 56 },
			{ 4, 36, 24, 24, 24, 72 },
			{ 4, 24, 16, 64, 24, 24 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 4, 6, 4, 64, 64, 64 },
			{ 4, 12, 8, 64, 64, 64 },
			{ 4, 36, 24, 64, 64, 24 },
			{ 7, 66, 44, 48, 16, 16 },
			{ 4, 36, 24, 32, 32, 72 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0 },
			{ 7, 96, 64, 12, 32, 12 },
		};

		// GLOBAL: TOY2 0x0054DD50
		int32_t g_slotCounts[2];

		// GLOBAL: TOY2 0x0054E050
		int16_t g_bufferActive[2];

		// GLOBAL: TOY2 0x005543E0
		Slot g_slots[2][8];

		// GLOBAL: TOY2 0x00557710
		int32_t g_bufferIndex;

		// GLOBAL: TOY2 0x00554DC8
		RegisteredLight g_registeredLights[8];

		// GLOBAL: TOY2 0x00559C64
		int32_t g_registeredLightCount;

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

	// GLOBAL: TOY2 0x00508D28
	ViewportPreset g_viewportPresets[] = {
		{ 3000.0f, 3500.0f, 50.0f, 55.0f, 10000.0f, 0.0f },
		{ 5000.0f, 6500.0f, 40.0f, 60.0f, 10000.0f, 0.0f },
		{ 10000.0f, 12000.0f, 40.0f, 60.0f, 12000.0f, 1000.0f },
	};

	// GLOBAL: TOY2 0x00884484
	int32_t g_isSoftwareRendering;

	// GLOBAL: TOY2 0x00508218
	int32_t g_textureBlitsEnabled = 1;

	// GLOBAL: TOY2 0x0052F2D4
	int32_t g_frameDelta;

	// GLOBAL: TOY2 0x00884488
	int32_t g_rendererValid;

	// GLOBAL: TOY2 0x00E4D964
	LPDIRECT3D3 g_drawDeviceD3D;

	// GLOBAL: TOY2 0x00E4D968
	LPDIRECT3DDEVICE3 g_drawDeviceD3DDevice;

	// GLOBAL: TOY2 0x0088448C
	int32_t g_fogEnabled;

	// GLOBAL: TOY2 0x00508514
	float g_fogStart = 1.0;

	// GLOBAL: TOY2 0x00508518
	float g_fogEnd = 100.0;

	// GLOBAL: TOY2 0x00884478
	DWORD g_fogColor;

	// GLOBAL: TOY2 0x00E4D95C
	int32_t g_deviceBlendShadeCaps;

	// GLOBAL: TOY2 0x00884AB8
	int32_t g_vertexColorModBlue;

	// GLOBAL: TOY2 0x00884ABC
	int32_t g_vertexColorModGreen;

	// GLOBAL: TOY2 0x0088C7C8
	int32_t g_vertexColorModRed;

	// GLOBAL: TOY2 0x0094FCD8
	int32_t g_additionalRenderFlags;

	// GLOBAL: TOY2 0x00508718
	int32_t g_primitiveRenderFlags = 5;

	// GLOBAL: TOY2 0x0050871C
	int32_t g_hardwareTransparencyEnabled = 1;

	// GLOBAL: TOY2 0x005088B0
	float g_primaryRenderDistanceSquared = 144000000.0f;

	// GLOBAL: TOY2 0x005088B4
	float g_secondaryRenderDistanceSquared = 1000000.0f;

	// GLOBAL: TOY2 0x009F2F24
	int32_t g_instanceDataFreeCount;

	// GLOBAL: TOY2 0x0088C7D0
	Nu3D::InstanceData g_instanceDataPool[1000];

	// GLOBAL: TOY2 0x0094FCD0
	int32_t g_renderEntryFreeCount;

	// GLOBAL: TOY2 0x0094FCE0
	Nu3D::VertexTL g_primitiveVertices[1500];

	// GLOBAL: TOY2 0x00508728
	int32_t g_primitiveBufferFreeCount = 3000;

	// GLOBAL: TOY2 0x0095C860
	RenderEntry g_renderEntryPool[3000];

	// GLOBAL: TOY2 0x0095B860
	void* g_renderBuckets[1024];

	// GLOBAL: TOY2 0x008F7E90
	SortedPrimitive g_primitiveBuffer[3000];

	// FUNCTION: TOY2 0x004B92B0 [MATCHED]
	int32_t SetAdditionalRenderFlags(int32_t flags)
	{
		int32_t previousFlags = g_additionalRenderFlags;
		g_additionalRenderFlags = flags;
		return previousFlags;
	}

	// FUNCTION: TOY2 0x004B5CE0 [MATCHED]
	int32_t Set508718(int32_t value)
	{
		int32_t previousValue = g_primitiveRenderFlags;
		g_primitiveRenderFlags = value;
		return previousValue;
	}

	// FUNCTION: TOY2 0x004BC410 [MATCHED]
	void SetRenderDistance(float primaryDistance, float secondaryDistance)
	{
		g_primaryRenderDistanceSquared = primaryDistance * primaryDistance;
		g_secondaryRenderDistanceSquared = secondaryDistance * secondaryDistance;
	}

	// FUNCTION: TOY2 0x004CDD10 [MATCHED]
	void SetViewportPresetByDetail(int32_t detail)
	{
		if (detail <= 2)
		{
			if (detail < 0)
				detail = 0;
		}
		else
		{
			detail = 2;
		}

		SetRenderDistance(g_viewportPresets[detail].primaryRenderDistance, g_viewportPresets[detail].secondaryRenderDistance);
		Nu3D::Scene::g_secondaryPortalNearClip = g_viewportPresets[detail].secondaryPortalNearClip;
		Nu3D::Scene::g_primaryFogFarClip = g_viewportPresets[detail].primaryFogFarClip;
		Nu3D::Scene::g_primaryNearClip = g_viewportPresets[detail].primaryNearClip;
		Nu3D::Scene::g_secondaryNearClip = g_viewportPresets[detail].secondaryNearClip;
	}

	// FUNCTION: TOY2 0x004CDD80 [MATCHED]
	void SetViewportPreset() { SetViewportPresetByDetail(Toy2::g_toyCfgData.detail); }

	// GLOBAL: TOY2 0x0094FCD4
	float g_lodFactor;

	// GLOBAL: TOY2 0x009F5FE0
	Nu3D::Material* g_whiteMaterial;

	// GLOBAL: TOY2 0x009F5FF0
	int32_t g_drawingTransparentBuckets;

	// GLOBAL: TOY2 0x009F5FF8
	int32_t g_vertexLightingEnabled;

	// FUNCTION: TOY2 0x004B9A60 [MATCHED]
	int32_t EnableVertexLighting(int32_t enable)
	{
		int32_t previousValue = g_vertexLightingEnabled;
		g_vertexLightingEnabled = enable;
		return previousValue;
	}

	// GLOBAL: TOY2 0x009F6000
	int32_t g_useVertexColorMod;

	// FUNCTION: TOY2 0x004B92E0 [MATCHED]
	void SetVertexColorModulation(int32_t red, int32_t green, int32_t blue)
	{
		g_vertexColorModRed = red;
		g_vertexColorModGreen = green;
		g_vertexColorModBlue = blue;
	}

	// FUNCTION: TOY2 0x004B9300 [MATCHED]
	int32_t EnableVertexColorModulation(int32_t enable)
	{
		int32_t previousValue = g_useVertexColorMod;
		g_useVertexColorMod = enable;
		return previousValue;
	}

	// GLOBAL: TOY2 0x009F5FD8
	float g_materialHorzOffset;

	// GLOBAL: TOY2 0x009F5FDC
	float g_materialVertOffset;

	// GLOBAL: TOY2 0x009F2EA0
	Nu3D::Sprite g_instanceSpriteTemplate;

	// GLOBAL: TOY2 0x009F5FB0
	Nu3D::Patch::PatchVertices g_FVF_14C_Buffer_2;

	// GLOBAL: TOY2 0x009F5FC4
	Nu3D::Patch::PatchVertices g_FVF_14C_Buffer_1;

	// GLOBAL: TOY2 0x009F2F28
	Nu3D::Patch::PatchVertices g_FVF_152_Buffer;

	// GLOBAL: TOY2 0x009F5FFC
	int32_t g_drawTriangleWireframes;

	// GLOBAL: TOY2 0x00E4D8FC
	int32_t g_submittedVertexCount;

	// GLOBAL: TOY2 0x00E4D944
	int32_t g_submittedTriangleCount;

	// GLOBAL: TOY2 0x00E4D94C
	int32_t g_submittedPrimitiveCount;

	// GLOBAL: TOY2 0x00E4D920
	int32_t g_renderStateCache[8];

	// GLOBAL: TOY2 0x00E4D8F8
	int32_t g_maxSimultaneousTextures;

	// GLOBAL: TOY2 0x00508704
	int32_t g_maxSimultaneousTexturesMax = 1;

	// GLOBAL: TOY2 0x00508708
	int32_t g_srcBlendMode = 5;

	// GLOBAL: TOY2 0x0050870C
	int32_t g_destBlendMode = 2;

	// GLOBAL: TOY2 0x00508710
	int32_t g_alphaBlendSrc = 1;

	// GLOBAL: TOY2 0x00508714
	int32_t g_alphaBlendDest = 4;

	// GLOBAL: TOY2 0x00830C54
	int32_t g_lastFrameTimestamp;

	// GLOBAL: TOY2 0x00830C58
	int32_t g_frameStabilityCounter;

	// GLOBAL: TOY2 0x00500AA4
	int32_t g_maxSpeedMultiplier = 4;

	// GLOBAL: TOY2 0x00500AAC
	int32_t g_startupDelayFrames = 120;

	// GLOBAL: TOY2 0x00500AA8
	int32_t g_targetSpeedMultiplier = 1;

	// GLOBAL: TOY2 0x0050851C
	uint8_t g_gammaLUT[256] = {
		// clang-format off
		0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
		16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
		32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
		64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
		80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
		96,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
		112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
		128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
		144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
		160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
		176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
		192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
		208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
		224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
		240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
		// clang-format on
	};

	// GLOBAL: TOY2 0x005084F4
	float g_gammaCorrection = 1.0;

	// GLOBAL: TOY2 0x005084F8
	int32_t g_gammaFixedPoint = 0x10000;

	// GLOBAL: TOY2 0x005087F4
	int32_t g_drawMaterialBuckets = 1;

	// GLOBAL: TOY2 0x005087F8
	int32_t g_drawTransparentBuckets = 1;

	// GLOBAL: TOY2 0x004F7414
	float g_virtualScreenWidth = 512.0;

	// GLOBAL: TOY2 0x004F7418
	float g_virtualScreenHeight = 256.0;

	// GLOBAL: TOY2 0x00A4CC90
	Nu3D::Material* g_boundMaterial;

	// GLOBAL: TOY2 0x00AAD778
	int32_t g_boundTextureIndices[8];

	// GLOBAL: TOY2 0x00500A54
	int32_t g_drawParallaxTexture = 1;

	// GLOBAL: TOY2 0x0072EF90
	float g_parallaxHorizOffset;

	// GLOBAL: TOY2 0x00830C50
	float g_parallaxCurHorizScroll;

	// GLOBAL: TOY2 0x00830C4C
	int32_t g_previousParallaxYaw;

	// GLOBAL: TOY2 0x00731F24
	float g_parallaxScrollStep;

	const float kParallaxVerticalScale = 0.238732412457466f;
	const float kParallaxYawScale = 4.1887903213501f;
	const float kCameraAngleScale = 0.0000152587890625f;

	// GLOBAL: TOY2 0x00731CD0
	float g_parallaxTexHeightRatio;

	// GLOBAL: TOY2 0x00731CD4
	float g_parallaxTexWidthRatio;

	// GLOBAL: TOY2 0x00731D68
	RGBA g_parallaxTexFirstPixel;

	// GLOBAL: TOY2 0x00731CC8
	RGBA g_parallaxTexLastPixel;

	// GLOBAL: TOY2 0x00E4D96C
	int32_t g_deviceBlendShadeCapsCpy;
}

namespace DrawingAPI
{
	// GLOBAL: TOY2 0x00508724
	Device_DrawIndexedPrimitive* DrawIndexedPrimitive;

	// GLOBAL: TOY2 0x00508720
	Device_DrawIndexedPrimitiveVB* DrawIndexedPrimitiveVB;

	// GLOBAL: TOY2 0x005084FC
	Device_ReleaseVertexBuffer* ReleaseVertexBuffer;

	// GLOBAL: TOY2 0x00508500
	Device_CreateVertexBuffer* CreateVertexBuffer;

	// GLOBAL: TOY2 0x00508504
	Device_LockVertexBuffer* LockVertexBuffer;

	// GLOBAL: TOY2 0x00508508
	Device_UnlockVertexBuffer* UnlockVertexBuffer;

	// GLOBAL: TOY2 0x0050850C
	Device_OptimizeVertexBuffer* OptimizeVertexBuffer;

	// GLOBAL: TOY2 0x00508510
	Device_ProcessVerticesOnBuffer* ProcessVerticesOnBuffer;

	// FUNCTION: TOY2 0x004B2BF0 [MATCHED]
	void SetVertexAPIs(int32_t isSoftwareRendering)
	{
		if (isSoftwareRendering)
		{
			ReleaseVertexBuffer = SoftwareDevice::ReleaseVertexBuffer;
			CreateVertexBuffer = SoftwareDevice::CreateVertexBuffer;
			LockVertexBuffer = SoftwareDevice::LockVertexBuffer;
			UnlockVertexBuffer = SoftwareDevice::UnlockVertexBuffer;
			OptimizeVertexBuffer = SoftwareDevice::OptimizeVertexBuffer;
			ProcessVerticesOnBuffer = SoftwareDevice::ProcessVerticesOnBuffer;
		}
		else
		{
			ReleaseVertexBuffer = HardwareDevice::ReleaseVertexBuffer;
			CreateVertexBuffer = HardwareDevice::CreateVertexBuffer;
			LockVertexBuffer = HardwareDevice::LockVertexBuffer;
			UnlockVertexBuffer = HardwareDevice::UnlockVertexBuffer;
			OptimizeVertexBuffer = HardwareDevice::OptimizeVertexBuffer;
			ProcessVerticesOnBuffer = HardwareDevice::ProcessVerticesOnBuffer;
		}
	}
}

namespace Renderer
{
	// FUNCTION: TOY2 0x004B2CE0 [MATCHED]
	void DisableFog() { g_fogEnabled = 0; }

	// FUNCTION: TOY2 0x004B2CF0 [MATCHED]
	void ConfigureFog(float start, float end, RGBA color)
	{
		g_fogEnabled = 1;
		g_fogStart = start;
		g_fogEnd = end;
		g_fogColor = ApplyGammaCorrection(color).value;
	}

	// FUNCTION: TOY2 0x004B2D20 [MATCHED]
	void SetFogEnable(int32_t enable)
	{
		if (g_fogEnabled)
		{
			if (enable)
				DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
			else
				DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
		}
	}

	// FUNCTION: TOY2 0x004B6320 [PROVISIONAL]
	void SetRenderState(int32_t newStateFlags)
	{
		if (g_isSoftwareRendering)
		{
			g_renderStateCache[0] = newStateFlags;
		}
		else
		{
			uint32_t flagsToDisable = g_renderStateCache[0] & (newStateFlags ^ g_renderStateCache[0]);

			if (flagsToDisable)
			{
				if ((flagsToDisable & RENDER_ALPHA_DEFAULT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_CUSTOM) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_ALT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_TEX_MODULATE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_TEX_MOD_CUSTOM) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_ALPHA_TEX_MOD_ALT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);

				if ((flagsToDisable & RENDER_Z) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZENABLE, 1);

				if ((flagsToDisable & RENDER_DISABLE_PERSPECTIVE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, 1);

				if ((flagsToDisable & RENDER_BILINEAR_FILTER) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAG, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMIN, 1);
				}

				if ((flagsToDisable & RENDER_ZWRITE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 1);

				if ((flagsToDisable & RENDER_COLOR_VERTEX) != 0)
					DrawingDevice::SetLightState(D3DLIGHTSTATE_COLORVERTEX, 0);

				if ((flagsToDisable & RENDER_ZBIAS_1) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 0);

				if ((flagsToDisable & RENDER_ZBIAS_2) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 0);
			}

			uint32_t flagsToEnable = newStateFlags & (newStateFlags ^ g_renderStateCache[0]);

			if (flagsToEnable)
			{
				if ((flagsToEnable & RENDER_Z) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZENABLE, 0);

				if ((flagsToEnable & RENDER_ZWRITE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, 0);

				if ((flagsToEnable & RENDER_DISABLE_PERSPECTIVE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, 0);

				if ((flagsToEnable & RENDER_CULL_BACK) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_CULLMODE, 2);

				if ((flagsToEnable & RENDER_CULL_FRONT) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_CULLMODE, 3);

				if ((flagsToEnable & RENDER_CULL_NONE) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_CULLMODE, 1);

				if ((flagsToEnable & RENDER_ALPHA_DEFAULT) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, 5);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, 6);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 4);
				}

				if ((flagsToEnable & RENDER_ALPHA_CUSTOM) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_srcBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_destBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 4);
				}

				if ((flagsToEnable & RENDER_ALPHA_ALT) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_alphaBlendSrc);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_alphaBlendDest);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 4);
				}

				if ((flagsToEnable & RENDER_ALPHA_TEX_MODULATE) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, 5);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, 6);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 3);
				}

				if ((flagsToEnable & RENDER_ALPHA_TEX_MOD_CUSTOM) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_srcBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_destBlendMode);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 3);
				}

				if ((flagsToEnable & RENDER_ALPHA_TEX_MOD_ALT) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SRCBLEND, g_alphaBlendSrc);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_DESTBLEND, g_alphaBlendDest);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, 3);
				}

				if ((flagsToEnable & RENDER_BILINEAR_FILTER) != 0)
				{
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMAG, 2);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREMIN, 2);
				}

				if ((flagsToEnable & RENDER_COLOR_VERTEX) != 0)
				{
					DrawingDevice::SetLightState(D3DLIGHTSTATE_COLORVERTEX, 1);
					DrawingDevice::SetRenderState(D3DRENDERSTATE_SHADEMODE, 2);
				}

				if ((flagsToEnable & RENDER_ZBIAS_1) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 1);

				if ((flagsToEnable & RENDER_ZBIAS_2) != 0)
					DrawingDevice::SetRenderState(D3DRENDERSTATE_ZBIAS, 2);
			}

			g_renderStateCache[0] = newStateFlags;
		}
	}

	// FUNCTION: TOY2 0x004B6660 [PROVISIONAL]
	void SetTextureStageState(int32_t newState, DWORD textureStage)
	{
		uint32_t stateFlagsToDisable = g_renderStateCache[textureStage + 1] & (newState ^ g_renderStateCache[textureStage + 1]);

		if (stateFlagsToDisable)
		{
			if ((stateFlagsToDisable & (RENDER_TEXTURE_WRAP_UV | RENDER_TEXTURE_CLAMP_U)) != 0)
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 1);

			if ((stateFlagsToDisable & (RENDER_COLOR_MODULATE | RENDER_COLOR_BLEND_FACTOR)) != 0)
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLOROP, 1);
		}

		uint32_t flagsToEnable = newState & (newState ^ g_renderStateCache[textureStage + 1]);

		if (flagsToEnable)
		{
			if ((flagsToEnable & RENDER_TEXTURE_WRAP_UV) != 0)
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 3);

			if ((flagsToEnable & RENDER_TEXTURE_CLAMP_U) != 0)
			{
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 2);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_ADDRESS, 4);
			}

			if ((flagsToEnable & RENDER_COLOR_MODULATE) != 0)
			{
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLOROP, 4);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG1, 2);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG2, 1);
			}

			if ((flagsToEnable & RENDER_COLOR_BLEND_FACTOR) != 0)
			{
				DrawingDevice::SetRenderState(D3DRENDERSTATE_TEXTUREFACTOR, 0x80FFFFFF);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLOROP, 14);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG1, 2);
				DrawingDevice::SetTextureStageState(textureStage, D3DTSS_COLORARG2, 1);
			}
		}

		g_renderStateCache[textureStage + 1] = newState;
	}

	// FUNCTION: TOY2 0x004B6760 [MATCHED]
	int32_t SetupMaterialRenderState(Nu3D::Material* material, int32_t stateFlags)
	{
		int32_t textureStage = 0;
		while (textureStage < g_maxSimultaneousTextures)
		{
			int32_t newStateFlags = stateFlags;
			if (material != 0)
			{
				int32_t metadata = material->metadata;
				if (metadata & 0x2)
					newStateFlags |= RENDER_ALPHA_DEFAULT | RENDER_ZWRITE;
				if (metadata & 0x10)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_CUSTOM;
				if (metadata & 0x20)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_ALT;
				if (metadata & 0x200)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MODULATE;
				if (metadata & 0x400)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MOD_CUSTOM;
				if (metadata & 0x800)
					newStateFlags |= RENDER_ZWRITE | RENDER_ALPHA_TEX_MOD_ALT;
				if (metadata & 0x8)
					newStateFlags = (newStateFlags & ~(RENDER_CULL_FRONT | RENDER_CULL_BACK)) | RENDER_CULL_NONE;
				if (material == NGNLoader::g_tex14Materials[0] || material == NGNLoader::g_tex14Materials[1] || material == NGNLoader::g_tex14Materials[2])
					newStateFlags &= ~(RENDER_TEXTURE_WRAP_UV | RENDER_TEXTURE_CLAMP_U);
				if (textureStage != 0)
				{
					if (metadata & 0x40000)
						newStateFlags |= RENDER_COLOR_MODULATE;
					if (metadata & 0x80000)
						newStateFlags |= RENDER_COLOR_BLEND_FACTOR;
				}
			}
			if (textureStage == 0)
			{
				newStateFlags |= RENDER_COLOR_MODULATE;
				SetRenderState(newStateFlags);
			}
			SetTextureStageState(newStateFlags, textureStage);
			if (material != 0)
				material = material->nextPass;
			textureStage++;
		}

		return textureStage;
	}

	// FUNCTION: TOY2 0x004B6850 [MATCHED]
	void InitRenderState(int32_t newStage)
	{
		SetRenderState(newStage | RENDER_COLOR_MODULATE);
		SetTextureStageState(newStage | RENDER_COLOR_MODULATE, 0);

		for (int32_t texture = 1; texture < g_maxSimultaneousTextures; ++texture)
			SetTextureStageState(0, texture);
	}

	// FUNCTION: TOY2 0x004B62C0 [MATCHED]
	void ResetRenderPools()
	{
		memset(Nu3D::g_spriteBuckets, 0, sizeof(Nu3D::g_spriteBuckets));
		memset(g_renderBuckets, 0, sizeof(g_renderBuckets));
		g_instanceDataFreeCount = 1000;
		g_primitiveBufferFreeCount = 3000;
		g_renderEntryFreeCount = 3000;
		Renderer::Sprite::g_spriteBuffer3DCount = 2000;
	}

	// FUNCTION: TOY2 0x004B9710 [PROVISIONAL]
	void InitResources()
	{
		g_vertexColorModBlue = 4096;
		g_vertexColorModGreen = 4096;
		g_vertexColorModRed = 4096;
		g_additionalRenderFlags = 0;
		g_lodFactor = 1.0;

		RGBColor whiteColor;
		whiteColor.b = 1.0;
		whiteColor.g = 1.0;
		whiteColor.r = 1.0;

		g_whiteMaterial = Nu3D::Material::CreateFromColor(&whiteColor);

		g_vertexLightingEnabled = 0;
		g_useVertexColorMod = 0;

		SoftwareRenderer::g_viewportRect = 0;

		Set508718(5);

		g_FVF_14C_Buffer_2.format = D3DFVF_0x1C4;
		g_FVF_14C_Buffer_2.vertexCount = 1000;
		g_FVF_14C_Buffer_2.vertexBuffer = 0;
		g_FVF_14C_Buffer_2.data.verticesTL = (Nu3D::VertexTL*)malloc(sizeof(Nu3D::VertexTL) * 1000);

		Nu3D::Patch::PatchVertices::CreateVertexBuffer(&g_FVF_14C_Buffer_2, 12);

		g_FVF_14C_Buffer_1.format = D3DFVF_0x1C4;
		g_FVF_14C_Buffer_1.vertexCount = 1000;
		g_FVF_14C_Buffer_1.vertexBuffer = 0;
		g_FVF_14C_Buffer_1.data.verticesTL = (Nu3D::VertexTL*)malloc(sizeof(Nu3D::VertexTL) * 1000);

		Nu3D::Patch::PatchVertices::CreateVertexBuffer(&g_FVF_14C_Buffer_1, 2);

		g_FVF_152_Buffer.format = D3DFVF_0x152;
		g_FVF_152_Buffer.vertexCount = 1000;
		g_FVF_152_Buffer.vertexBuffer = 0;
		g_FVF_152_Buffer.data.vertices = (Nu3D::Vertex*)malloc(sizeof(Nu3D::Vertex) * 1000);

		Nu3D::Patch::PatchVertices::CreateVertexBuffer(&g_FVF_152_Buffer, 14);

		D3DDEVICEDESC outSurfaceDesc;
		memcpy(&outSurfaceDesc, DrawingDevice::CopySurfaceDesc(&outSurfaceDesc), sizeof(outSurfaceDesc));

		memset(g_renderStateCache, 0, sizeof(g_renderStateCache));

		g_maxSimultaneousTextures = outSurfaceDesc.wMaxSimultaneousTextures;

		if (outSurfaceDesc.wMaxSimultaneousTextures >= g_maxSimultaneousTexturesMax)
			g_maxSimultaneousTextures = g_maxSimultaneousTexturesMax;

		if ((outSurfaceDesc.dpcTriCaps.dwShadeCaps & 0x4000) != 0)
		{
			if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & 2) == 0)
				g_destBlendMode = 6;

			if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & 8) == 0)
			{
				g_alphaBlendSrc = 5;
				g_alphaBlendDest = 6;
			}
		}
		else
		{
			g_srcBlendMode = 2;
			g_destBlendMode = 2;

			if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & 8) == 0)
			{
				g_alphaBlendSrc = 5;
				g_alphaBlendDest = 6;
			}
		}

		InitRenderState(3078);
		InitRenderState(4752);

		DrawingAPI::SetVertexAPIs(g_isSoftwareRendering);

		if (g_isSoftwareRendering == 1)
		{
			DrawingAPI::DrawIndexedPrimitiveVB = SoftwareDevice::DrawIndexedPrimitiveVB;
			DrawingAPI::DrawIndexedPrimitive = SoftwareDevice::DrawIndexedPrimitive;
		}
		else
		{
			DrawingAPI::DrawIndexedPrimitiveVB = HardwareDevice::DrawIndexedPrimitiveVB;
			DrawingAPI::DrawIndexedPrimitive = HardwareDevice::DrawIndexedPrimitive;
		}

		ResetRenderPools();
		Renderer::Sprite::ResetQueue();

		SoftwareRenderer::InitialisePrimarySurface();

		DECOMP_PRINT(("Finished Renderer::InitResources\n"));
	}

	// FUNCTION: TOY2 0x004B37F0 [MATCHED]
	void Cleanup()
	{
		while (Nu3D::g_patchListHead != NULL)
		{
			Nu3D::g_patchListHead->listNext = NULL;
			Nu3D::Patch::Destroy(Nu3D::g_patchListHead);
		}
		while (Nu3D::g_primListHead != NULL)
		{
			Nu3D::g_primListHead->listNext = NULL;
			Nu3D::Primitive::Destroy(Nu3D::g_primListHead);
		}
		Nu3D::Material::Close();
		NGNLoader::ReleaseAllTextures();
		Nu3D::Light::DestroyAllLights();
		g_rendererValid = 0;
	}

	// FUNCTION: TOY2 0x004B3630 [PROVISIONAL]
	void Init()
	{
		if (g_rendererValid)
			Cleanup();

		g_drawDeviceD3D = DrawingDevice::GetD3D();
		g_drawDeviceD3DDevice = DrawingDevice::GetD3DDevice();

		Nu3D::g_primListHead = 0;
		Nu3D::g_patchListHead = 0;

		Nu3D::SetIsSoftwareRendering(g_isSoftwareRendering);
		Nu3D::SetMinTexSize(g_isSoftwareRendering != 0 ? 256 : 0);

		Nu3D::Viewport::Init();
		Nu3D::Light::InitPool(16);
		NGNLoader::Init();
		Nu3D::Material::Init();

		InitResources();
		g_rendererValid = 1;
		Nu3D::Font::BuildFontTextures();
		DisableFog();

		g_deviceBlendShadeCaps = 0;

		D3DDEVICEDESC outSurfaceDesc;
		memcpy(&outSurfaceDesc, DrawingDevice::CopySurfaceDesc(&outSurfaceDesc), sizeof(outSurfaceDesc));

		if ((outSurfaceDesc.dpcTriCaps.dwShadeCaps & D3DPSHADECAPS_ALPHAGOURAUDBLEND) != 0)
			g_deviceBlendShadeCaps |= 1;

		if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & D3DPBLENDCAPS_ONE) != 0)
			g_deviceBlendShadeCaps |= 2;

		if ((outSurfaceDesc.dpcTriCaps.dwDestBlendCaps & D3DPBLENDCAPS_INVSRCCOLOR) != 0)
			g_deviceBlendShadeCaps |= 4;

		if ((outSurfaceDesc.dpcTriCaps.dwTextureBlendCaps & D3DPTEXTURECAPS_TRANSPARENCY) != 0)
			g_deviceBlendShadeCaps |= 8;
	}

	// FUNCTION: TOY2 0x004B3860 [MATCHED]
	void SetIsSoftwareRendering(int32_t value) { g_isSoftwareRendering = value; }

	// FUNCTION: TOY2 0x00453CD0 [MATCHED]
	void SetVirtualRatioTo54()
	{
		g_virtualScreenWidth = 320.0;
		g_virtualScreenHeight = 256.0;
	}

	// FUNCTION: TOY2 0x00490860 [EFFECTIVE]
	void DoFrameDelay(int32_t isGameplayFrame)
	{
		int32_t hrt = Nu3D::GetHighResolutionTime();
		int32_t elapsedMs = hrt - g_lastFrameTimestamp;

		if (Toy2::g_demoMode)
		{
			g_frameDelta = 2;

			if (elapsedMs < 33)
			{
				Nu3D::PrecisionSleep(33 - elapsedMs);
			}
		}
		else
		{
			int32_t calculatedMultiplier = 60 * elapsedMs / 1000;
			g_frameDelta = calculatedMultiplier;

			if (1000 * calculatedMultiplier / 60 != elapsedMs)
				g_frameDelta = ++calculatedMultiplier;

			int32_t minMultiplier;

			if (calculatedMultiplier < 1)
				minMultiplier = 1;
			else
				minMultiplier = calculatedMultiplier;

			if (g_maxSpeedMultiplier < minMultiplier)
			{
				calculatedMultiplier = g_maxSpeedMultiplier;
			}
			else
			{
				if (calculatedMultiplier >= 1)
					goto LBL_STARTUP_DELAY;

				calculatedMultiplier = 1;
			}

			g_frameDelta = calculatedMultiplier;

		LBL_STARTUP_DELAY:

			int32_t delayFrames = g_startupDelayFrames;
			if (g_startupDelayFrames > 0)
			{
				delayFrames = g_startupDelayFrames - calculatedMultiplier;
				g_startupDelayFrames -= calculatedMultiplier;
			}

			if (isGameplayFrame)
			{
				if (delayFrames <= 0)
				{
					int32_t stabilityCounter = g_frameStabilityCounter;
					int32_t targetSpeedMultiplier = g_targetSpeedMultiplier;

					if (g_frameStabilityCounter > 0)
					{
						stabilityCounter = g_frameStabilityCounter - targetSpeedMultiplier;
						g_frameStabilityCounter -= targetSpeedMultiplier;
					}

					if (calculatedMultiplier < targetSpeedMultiplier)
					{
						if (stabilityCounter > 0)
						{
							calculatedMultiplier = targetSpeedMultiplier;
							targetSpeedMultiplier = calculatedMultiplier;
							g_frameDelta = calculatedMultiplier;
						}
					}
					else
					{
						g_frameStabilityCounter = 60;
					}

					g_targetSpeedMultiplier = calculatedMultiplier;
				}
			}
			else
			{
				g_targetSpeedMultiplier = 1;
				g_frameStabilityCounter = 0;
				g_startupDelayFrames = 120;
			}

			int32_t sleepTimeMs = 1000 * calculatedMultiplier / 60 - elapsedMs;

			if (sleepTimeMs > 0)
				Nu3D::PrecisionSleep(sleepTimeMs);
		}

		g_lastFrameTimestamp = Nu3D::GetHighResolutionTime();
	}

	// FUNCTION: TOY2 0x004C2080 [PROVISIONAL]
	int32_t ConvertRGBATo16Bit(RGBA color)
	{
		if (SoftwareRenderer::g_pixelFormatMode == 0)
			return ((uint16_t)(color.g & 0xF8) << 2) + ((uint16_t)(color.b & 0xF8) << 7) + (uint16_t)(color.r >> 3);

		return ((uint16_t)(color.g & 0xF8) << 3) + ((uint16_t)(color.b & 0xF8) << 8) + (uint16_t)(color.r >> 3);
	}

	// FUNCTION: TOY2 0x004B37B0 [MATCHED]
	RGBA ApplyGammaCorrection(RGBA color)
	{
		color.r = g_gammaLUT[color.r];
		color.g = g_gammaLUT[color.g];
		color.b = g_gammaLUT[color.b];

		return color;
	}

	// FUNCTION: TOY2 0x004B2C80 [EFFECTIVE]
	void ClearScreen(RGBA clearColor, int32_t clearFlags)
	{
		RGBA color = ApplyGammaCorrection(clearColor);

		if (! g_isSoftwareRendering)
		{
			DrawingDevice::ClearScreen(clearFlags, color.value);
			return;
		}

		uint16_t convertedColor = Renderer::ConvertRGBATo16Bit(color);
		SoftwareRenderer::g_softwareClearColor = (convertedColor << 16) | convertedColor;
	}

	// FUNCTION: TOY2 0x004B2D80 [MATCHED]
	void ApplyFogSettings()
	{
		if (g_fogEnabled)
		{
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 1);
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGCOLOR, g_fogColor & 0xFFFFFF);

			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGMODE, 3);
			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGSTART, *(DWORD*)&g_fogStart);
			DrawingDevice::SetLightState(D3DLIGHTSTATE_FOGEND, *(DWORD*)&g_fogEnd);
		}
		else
		{
			DrawingDevice::SetRenderState(D3DRENDERSTATE_FOGENABLE, 0);
		}
	}

	// FUNCTION: TOY2 0x004B2D50 [MATCHED]
	int32_t BeginScene()
	{
		if (! DrawingDevice::BeginScene())
		{
			Renderer::ApplyFogSettings();
			Nu3D::Viewport::Reset();
			Nu3D::Viewport::SetViewClipRect();

			if (g_isSoftwareRendering)
				SoftwareRenderer::ResetRenderCommands();

			return 1;
		}

		return 0;
	}

	// FUNCTION: TOY2 0x004B2DE0 [MATCHED]
	void EndScene(int32_t presentFrame)
	{
		DrawingDevice::EndScene();

		if (presentFrame)
		{
			if (g_isSoftwareRendering)
				SoftwareRenderer::PresentFrame();
			else
				DrawingDevice::PresentFrameAndRestore();
		}
	}

	// FUNCTION: TOY2 0x004CE5B0 [MATCHED]
	void ShowBlackFrames()
	{
		RGBA black;
		black.value = 0;
		ClearScreen(black, 3);
		if (BeginScene())
		{
			EndScene(1);
		}
		ClearScreen(black, 3);
		if (BeginScene())
		{
			EndScene(1);
		}
	}

	// FUNCTION: TOY2 0x004AFD30 [PROVISIONAL]
	void BlitBitmapWithWrapping(
		Nu3D::BmpDataNode* bitmap, int32_t sourceX, int32_t sourceY, int32_t width, int32_t height, int32_t wrapX, int32_t wrapY, int32_t destX, int32_t destY)
	{
		if (g_textureBlitsEnabled == 0)
		{
			return;
		}

		DDBLTFX blitEffects;
		blitEffects.dwSize = sizeof(DDBLTFX);
		blitEffects.dwROP = SRCCOPY;
		LPDIRECTDRAWSURFACE4 surface = bitmap->surface;

		RECT sourceRect;
		RECT destRect;
		if (wrapY == 0)
		{
			int32_t sourceRight = sourceX + width;
			int32_t sourceBottom = sourceY + height;
			int32_t destRight = destX + width;
			int32_t destBottom = destY + height;

			sourceRect.left = sourceX + wrapX;
			sourceRect.top = sourceY;
			sourceRect.right = sourceRight - wrapX;
			sourceRect.bottom = sourceBottom;

			destRect.left = destX;
			destRect.top = destY;
			destRect.right = destRight - wrapX;
			destRect.bottom = destBottom;
			surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);

			if (wrapX != 0)
			{
				sourceRect.left = sourceX;
				sourceRect.right = sourceX + wrapX;
				destRect.left = destRight - wrapX;
				destRect.right = destRight;
				surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);
			}
		}
		else
		{
			int32_t sourceRight = sourceX + width;
			int32_t sourceBottom = sourceY + height;
			int32_t destRight = destX + width;
			int32_t destBottom = destY + height;

			sourceRect.left = sourceX;
			sourceRect.top = sourceY + wrapY;
			sourceRect.right = sourceRight;
			sourceRect.bottom = sourceBottom;

			destRect.left = destX;
			destRect.top = destY;
			destRect.right = destRight;
			destRect.bottom = destBottom - wrapY;
			surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);

			sourceRect.top = sourceY;
			sourceRect.bottom = sourceY + wrapY;
			destRect.top = destBottom - wrapY;
			destRect.bottom = destBottom;
			surface->Blt(&destRect, surface, &sourceRect, DDBLT_ROP | DDBLT_WAIT, &blitEffects);
		}
	}

	// FUNCTION: TOY2 0x004CE510 [MATCHED]
	void BlitTextureByIndex(
		uint32_t textureIndex, int32_t destX, int32_t destY, int32_t width, int32_t height, int32_t wrapX, int32_t wrapY, int32_t sourceX, int32_t sourceY)
	{
		if ((Toy2::g_toyCfgData.flags & 4) != 0)
		{
			uint32_t textureDataIndex = NGNLoader::GetTextureDataIndex(textureIndex);
			if (textureDataIndex != 0)
			{
				NGNLoader::NGNTextureData* textureData = NGNLoader::GetTextureDataByIndex(textureDataIndex);
				BlitBitmapWithWrapping(textureData->bmpDataNode, sourceX, sourceY, width, height, wrapX, wrapY, destX, destY);
			}
		}
	}

	// FUNCTION: TOY2 0x0049B260 [MATCHED]
	void BlitTextureByIndexOffset(uint32_t textureIndex,
		int32_t destX,
		int32_t destY,
		int32_t width,
		int32_t height,
		int32_t wrapX,
		int32_t wrapY,
		int32_t sourceOffsetX,
		int32_t sourceOffsetY)
	{ BlitTextureByIndex(textureIndex, destX, destY, width, height, wrapX, wrapY, destX + sourceOffsetX, destY + sourceOffsetY); }

	// FUNCTION: TOY2 0x0049B2A0 [PROVISIONAL]
	void DrawMenuText(int16_t yPos, char* text, int32_t dimmed)
	{
		int32_t textLength = 0;

		if (*text)
			while (text[++textLength]) {};

		int32_t xPos = (40 - textLength) * 4;

		if (textLength > 0)
		{
			do
			{
				uint8_t currentChar = *text++;

				if (currentChar == '1')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 2);
				else if (currentChar == '2')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 3);
				else if (currentChar == '3')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 1);
				else if (currentChar == '4')
					Sprite::DrawTile(xPos - 4, yPos - 3, 68, 0);
				else if (currentChar != ' ')
				{
					if (currentChar != '\'')
						currentChar += 0x9F;
					else
						currentChar = 47;

					if (dimmed)
						Sprite::DrawScaled(xPos, yPos, 67, currentChar, 128, 128, 128, 0, 2048, 2048);
					else
						Sprite::DrawScaled(xPos, yPos, 67, currentChar, 128, 128, 128, 255, 2048, 2048);
				}

				xPos += 8;
			} while (--textLength != 0);
		}
	}

	// FUNCTION: TOY2 0x0049B3B0 [PROVISIONAL]
	void DrawMenuTextScaled(int32_t xPos, int32_t yPos, char* text, int32_t dimmed, int32_t alignment, int32_t scale)
	{
		int32_t textLength = strlen(text);
		int32_t drawX;

		switch (alignment)
		{
			case 0:
				drawX = xPos;
				break;
			case 2:
				drawX = xPos - textLength * scale * 16 / 4096;
				break;
			default:
				drawX = xPos - textLength * scale * 16 / 8192;
				break;
		}

		if (textLength > 0)
		{
			int32_t charAdvance = scale * 16 / 4096;
			xPos = textLength;
			do
			{
				uint8_t currentChar = *text++;
				if (currentChar != ' ')
				{
					switch (currentChar)
					{
						case '\'':
							currentChar = 47;
							break;
						case ',':
							currentChar = 26;
							break;
						case '.':
							currentChar = 27;
							break;
						case '/':
							currentChar = 28;
							break;
						case '\\':
							currentChar = 29;
							break;
						case '?':
							currentChar = 30;
							break;
						case ':':
							currentChar = 31;
							break;
						case ';':
							currentChar = 32;
							break;
						case '(':
							currentChar = 33;
							break;
						case ')':
							currentChar = 34;
							break;
						case '0':
							currentChar += 0xFC;
							break;
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
							currentChar += 0xF2;
							break;
						case 'A':
						case 'B':
						case 'C':
						case 'D':
							currentChar += 0xEC;
							break;
						default:
							currentChar += 0x9F;
							break;
					}

					if (dimmed)
						Sprite::DrawScaled(drawX, yPos, 67, currentChar, 128, 128, 128, 0, scale, scale);
					else
						Sprite::DrawScaled(drawX, yPos, 67, currentChar, 128, 128, 128, 255, scale, scale);
				}

				drawX += charAdvance;
			} while (--xPos != 0);
		}
	}

	// FUNCTION: TOY2 0x00401B60 [MATCHED]
	void DrawBlackBorderBox(int32_t xPos, int32_t yPos, int32_t width, int32_t height, uint32_t red, uint32_t green, uint32_t blue)
	{
		Sprite::DrawScaled(xPos, yPos, 6, 1, 0, 0, 0, 0x60, 0x2000, height);
		Sprite::DrawScaled((width >> 12) + xPos - 2, yPos, 6, 1, 0, 0, 0, 0x60, 0x2000, height);
		Sprite::DrawScaled(xPos, yPos, 6, 1, 0, 0, 0, 0x60, width, 0x1000);
		Sprite::DrawScaled(xPos, (height >> 12) + yPos - 1, 6, 1, 0, 0, 0, 0x60, width, 0x1000);
		Sprite::DrawScaled(xPos + 2, yPos + 1, 6, 1, red, green, blue, 0, width - 0x4000, height - 0x2000);
	}

	// FUNCTION: TOY2 0x00401FB0 [PROVISIONAL]
	void DrawString(int32_t yPos, const char* text, uint32_t red, uint32_t green, uint32_t blue, int32_t fullWidthLayout)
	{
		int32_t centerX = fullWidthLayout != 0 ? 256 : 160;
		int32_t textLength = 0;
		while (text[textLength] != '\0')
			textLength++;

		int32_t xPos = centerX - textLength * 4;
		if (textLength > 0)
		{
			do
			{
				DrawChar(xPos, yPos, *text, red, green, blue, fullWidthLayout);
				text++;
				xPos += 8;
			} while (--textLength != 0);
		}
	}

	// FUNCTION: TOY2 0x0049B630 [PROVISIONAL]
	void DrawChar(int32_t xPos, int32_t yPos, uint8_t character, uint32_t red, uint32_t green, uint32_t blue, int32_t fullWidthLayout)
	{
		if (character == '@')
		{
			Sprite::DrawTiledFixed((int16_t)xPos, (int16_t)(yPos - 2), 38, 1);
			return;
		}
		if (character == '~')
		{
			Sprite::DrawTiledFixed((int16_t)xPos, (int16_t)(yPos - 2), 38, 0);
			return;
		}
		if (character == ' ')
			return;

		switch (character)
		{
			case '!':
				character = 41;
				break;
			case '\'':
				character = 48;
				break;
			case '*':
				character = 50;
				break;
			case ',':
				character = 37;
				break;
			case '-':
				character = 49;
				break;
			case '.':
				character = 36;
				break;
			case '>':
				character = 45;
				break;
			case '?':
				character = 40;
				break;
			default:
				if (character <= '9')
					character += 0xEA;
				else
					character += 0x9F;
				break;
		}

		if (fullWidthLayout)
			Sprite::DrawScaled((int16_t)xPos, (int16_t)yPos, 20, character, red, green, blue, 255, 0x800, 0x800);
		else
			Sprite::DrawScaledFixed((int16_t)xPos, (int16_t)yPos, 20, character, red, green, blue, 255, 0x800, 0x800);
	}

	// FUNCTION: TOY2 0x0049B580 [PROVISIONAL]
	void DrawMainMenuText(int16_t yPos, char* text, int32_t fadeAlpha)
	{
		char* charPtr = text;
		int32_t strLength = 0;

		if (*text)
			while (text[++strLength]) {};

		int16_t xPos = 160 - 6 * strLength;

		if (strLength > 0)
		{
			int32_t remaining = strLength;

			do
			{
				char currentChar = *charPtr++;

				if (currentChar != ' ')
				{
					uint8_t tileIndex;

					if (currentChar == '\'')
						tileIndex = 47;
					else
						tileIndex = currentChar - 97;

					if (fadeAlpha == 128)
						Sprite::DrawScaled(xPos, yPos, 50, tileIndex, 128, 128, 128, 255, 2048, 2048);
					else
						Sprite::DrawScaled(xPos, yPos, 50, tileIndex, 255, 255, 255, (fadeAlpha << 9) + 96, 2048, 2048);
				}

				xPos += 12;
				--remaining;

			} while (remaining);
		}
	}

	// FUNCTION: TOY2 0x0044DD80 [PROVISIONAL]
	void DrawTintOverlay()
	{
		Vector2F uvTopLeft;
		Vector2F uvBottomRight;

		if (Nu3D::Camera::g_cameraTintBlue == 128 && Nu3D::Camera::g_cameraTintGreen == 128 && Nu3D::Camera::g_cameraTintRed == 128)
			return;

		if (Nu3D::Camera::g_cameraTintBlue > 128 || Nu3D::Camera::g_cameraTintGreen > 128 || Nu3D::Camera::g_cameraTintRed > 128)
		{
			int32_t brightenGreen = 2 * Nu3D::Camera::g_cameraTintGreen - 256;
			int32_t brightenBlue = 2 * Nu3D::Camera::g_cameraTintBlue - 256;
			int32_t brightenRed = 2 * Nu3D::Camera::g_cameraTintRed - 256;

			if (brightenBlue > 255)
			{
				brightenBlue = 255;
			}
			else if (brightenBlue < 0)
			{
				brightenBlue = 0;
			}

			if (brightenGreen > 255)
			{
				brightenGreen = 255;
			}
			else if (brightenGreen < 0)
			{
				brightenGreen = 0;
			}

			if (brightenRed > 255)
			{
				brightenRed = 255;
			}
			else if (brightenRed < 0)
			{
				brightenRed = 0;
			}

			RGBA brightenColor;
			brightenColor.b = brightenBlue;
			brightenColor.g = brightenGreen;
			brightenColor.a = -1 - brightenGreen;
			brightenColor.r = brightenRed;

			uvTopLeft.x = 0.0;
			uvTopLeft.y = 0.0;

			uvBottomRight.x = 1.0;
			uvBottomRight.y = 1.0;

			Sprite::Queue2DSprite(0.0, 0.0, 1.0, 1.0, &uvTopLeft, &uvBottomRight, 0, brightenColor, RENDER_PRESET_COLOR_OVERLAY);

			if (g_renderMode == RENDERMODE_SOFTWARE && SoftwareRenderer::g_bitsPerPixel == 8)
				SoftwareRenderer::UpdatePaletteTint();
		}
		else
		{
			int32_t darkenBlue = 2 * (128 - Nu3D::Camera::g_cameraTintBlue);
			int32_t darkenGreen = 2 * (128 - Nu3D::Camera::g_cameraTintGreen);
			int32_t darkenRed = 2 * (128 - Nu3D::Camera::g_cameraTintRed);

			if (darkenBlue > 255)
			{
				darkenBlue = 255;
			}
			else if (darkenBlue < 0)
			{
				darkenBlue = 0;
			}

			if (darkenGreen > 255)
			{
				darkenGreen = 255;
			}
			else if (darkenGreen < 0)
			{
				darkenGreen = 0;
			}

			if (darkenRed > 255)
			{
				darkenRed = 255;
			}
			else if (darkenRed < 0)
			{
				darkenRed = 0;
			}

			uvTopLeft.x = 0.0;
			uvTopLeft.y = 0.0;

			uvBottomRight.x = 1.0;
			uvBottomRight.y = 1.0;

			RGBA darkenColor;
			darkenColor.a = -1 - darkenGreen;
			darkenColor.b = darkenBlue;
			darkenColor.g = darkenGreen;
			darkenColor.r = darkenRed;

			int32_t texDataIndex;

			if (darkenBlue == darkenGreen && darkenBlue == darkenRed)
			{
				texDataIndex = NGNLoader::GetTextureDataIndex(14);

				if (! texDataIndex)
				{
					texDataIndex = NGNLoader::GetTextureDataIndex(36);

					if (! texDataIndex)
						texDataIndex = NGNLoader::GetTextureDataIndex(37);
				}
			}
			else
			{
				texDataIndex = 0;
			}

			Sprite::Queue2DSprite(0.0, 0.0, 1.0, 1.0, &uvTopLeft, &uvBottomRight, texDataIndex, darkenColor, RENDER_PRESET_FADE_OVERLAY);

			if (g_renderMode == RENDERMODE_SOFTWARE && SoftwareRenderer::g_bitsPerPixel == 8)
				SoftwareRenderer::UpdatePaletteTint();
		}
	}

	// FUNCTION: TOY2 0x0048F230 [PROVISIONAL]
	void UpdateBackgroundScroll(int16_t cameraPitch, int32_t cameraYaw)
	{
		int32_t textureDataIndex;
		if (Toy2::g_hasStaticBackdrop)
			textureDataIndex = NGNLoader::GetTextureDataIndex(37);
		else
			textureDataIndex = NGNLoader::GetTextureDataIndex(Toy2::g_nextBackdropId);

		if (textureDataIndex && g_drawParallaxTexture)
		{
			uint32_t bitmapWidth;
			uint32_t bitmapHeight;
			uint32_t textureWidth;
			uint32_t textureHeight;
			uint32_t* textureData;
			NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, &textureWidth, &textureHeight, &textureData);

			g_parallaxTexWidthRatio = (float)(int32_t)bitmapWidth / g_virtualScreenWidth;
			g_parallaxTexFirstPixel.value = textureData[0];
			g_parallaxTexLastPixel.value = textureData[textureWidth * textureHeight - 1];
			g_parallaxTexHeightRatio = (float)(int32_t)bitmapHeight / g_virtualScreenHeight;

			float currentYaw = (float)(cameraYaw & 0xFFFF);
			int32_t yawDelta = (int32_t)(currentYaw - g_previousParallaxYaw);
			g_previousParallaxYaw = (int32_t)currentYaw;
			if (yawDelta > 0x8000)
				yawDelta -= 0x10000;
			if (yawDelta < -0x8000)
				yawDelta += 0x10000;

			g_parallaxScrollStep = (float)yawDelta * kParallaxYawScale * kCameraAngleScale;
			g_parallaxCurHorizScroll += g_parallaxScrollStep;
			while (g_parallaxCurHorizScroll <= -g_parallaxTexWidthRatio)
				g_parallaxCurHorizScroll += g_parallaxTexWidthRatio;
			while (g_parallaxCurHorizScroll > 0.0f)
				g_parallaxCurHorizScroll -= g_parallaxTexWidthRatio;

			g_parallaxHorizOffset = (float)cameraPitch * kCameraAngleScale;
			g_parallaxHorizOffset = (float)SoftwareRenderer::g_backdropDimensions.verticalOffset / g_virtualScreenHeight
				+ g_parallaxHorizOffset / (kParallaxVerticalScale * Nu3D::Camera::g_currentCamera->aspectRatio);
		}
	}

	// FUNCTION: TOY2 0x0048F3E0 [MATCHED]
	void ResetParallax()
	{
		g_parallaxCurHorizScroll = 0.0;
		g_parallaxHorizOffset = 0.0;
		g_parallaxTexHeightRatio = 1.0;
		g_parallaxTexWidthRatio = 1.0;
	}

	// FUNCTION: TOY2 0x004B3870 [MATCHED]
	int32_t GetIsSoftwareRendering() { return g_isSoftwareRendering; }

	// FUNCTION: TOY2 0x004B3740 [MATCHED]
	float BuildGammaCorrectionLUT(float gammaCorrection)
	{
		float result = g_gammaCorrection;

		g_gammaCorrection = gammaCorrection;
		g_gammaFixedPoint = (gammaCorrection * 65536.0f);

		int32_t curGammaIdx = 0;
		int32_t count = 0;
		int32_t step = g_gammaFixedPoint;

		do
		{
			int32_t outputValue;

			if ((int32_t)(count & 0xFFFF0000) > 0xFF0000)
				outputValue = 0xFF;
			else
				outputValue = count >> 16;

			g_gammaLUT[curGammaIdx++] = outputValue;
			count += step;

		} while (curGammaIdx < 256);

		return result;
	}

	// FUNCTION: TOY2 0x004B2CC0 [MATCHED]
	void GetBlendShadeCaps(int32_t* capsOut)
	{
		if (capsOut)
			*capsOut = Renderer::g_deviceBlendShadeCaps;
	}

	// FUNCTION: TOY2 0x0048F410 [PROVISIONAL]
	void RenderParallaxBackground(int32_t forceRender)
	{
		if (g_drawParallaxTexture)
		{
			if (Toy2::g_hasStaticBackdrop)
			{
				g_parallaxHorizOffset = 0.0;
				g_parallaxCurHorizScroll = 0.0;
				g_parallaxTexHeightRatio = 1.0;
				g_parallaxTexWidthRatio = 1.0;
			}
			else if (! Toy2::g_hasBackdrop)
			{
				return;
			}

			if (forceRender || GetIsSoftwareRendering() || (Glue::SetBackdrop(Toy2::g_nextBackdropId), ! Glue::BackdropBltFast()))
			{
				int32_t texIndex = NGNLoader::GetTextureDataIndex(Toy2::g_nextBackdropId);

				if (texIndex)
				{
					Vector2F uvMax;
					Vector2F uvMin;

					uvMin.y = 0.0;
					uvMin.x = 0.0;

					uvMax.y = 1.0;
					uvMax.x = 1.0;

					if (g_parallaxHorizOffset > 0.0)
					{
						float topFillHeight = 1.0 / g_virtualScreenHeight + g_parallaxHorizOffset;
						Sprite::Queue2DSprite(0.0, 0.0, 1.0, topFillHeight, &uvMin, &uvMax, 0, g_parallaxTexLastPixel, RENDER_PARALLAX_BG);
					}

					double verticalExtent = g_parallaxTexHeightRatio + g_parallaxHorizOffset;

					if (verticalExtent < 1.0)
					{
						float bottomFillHeight = 1.0 - g_parallaxHorizOffset - g_parallaxTexHeightRatio + 1.0 / g_virtualScreenHeight;
						float verticalEnd = verticalExtent;

						Sprite::Queue2DSprite(0.0, verticalEnd, 1.0, bottomFillHeight, &uvMin, &uvMax, 0, g_parallaxTexFirstPixel, RENDER_PARALLAX_BG);
					}

					double nextHorizontalPos;

					do
					{
						RGBA color;
						color.value = -1;

						Sprite::Queue2DSprite(g_parallaxCurHorizScroll,
							g_parallaxHorizOffset,
							g_parallaxTexWidthRatio,
							g_parallaxTexHeightRatio,
							&uvMin,
							&uvMax,
							texIndex,
							color,
							RENDER_PARALLAX_BG);

						nextHorizontalPos = g_parallaxTexWidthRatio + g_parallaxCurHorizScroll;

						g_parallaxCurHorizScroll = nextHorizontalPos;

					} while (nextHorizontalPos < 1.0);
				}
			}
		}
	}

	// FUNCTION: TOY2 0x004B8490 [MATCHED]
	void RenderPrimitive(Nu3D::Primitive* primitive, const D3DMATRIX* transform, int32_t renderFlags)
	{
		if (0.0f != primitive->originRadius)
		{
			renderFlags |= g_additionalRenderFlags;
			Nu3D::InstanceData* instanceData = Nu3D::InstanceData::AllocFromMatrix(transform, renderFlags);
			if (instanceData)
			{
				for (; primitive; primitive = primitive->listNext)
					ProcessPrimitive(instanceData, primitive);
			}
		}
	}

}

namespace Nu3D
{
	using namespace Renderer;

	// FUNCTION: TOY2 0x004B84E0 [PROVISIONAL]
	InstanceData* InstanceData::AllocFromMatrix(const D3DMATRIX* matrix, int32_t renderFlags)
	{
		if (! g_instanceDataFreeCount)
			return 0;

		Nu3D::InstanceData* instanceData = &g_instanceDataPool[--g_instanceDataFreeCount];
		memcpy(instanceData->matrices, matrix, sizeof(D3DMATRIX));
		instanceData->renderFlags = renderFlags;
		instanceData->lodFactor = g_lodFactor;
		instanceData->horzOffset = g_materialHorzOffset;
		instanceData->vertOffset = g_materialVertOffset;
		instanceData->renderModeFlags = g_vertexLightingEnabled != 0;

		if (g_useVertexColorMod)
		{
			instanceData->renderModeFlags |= Nu3D::INSTANCE_RENDER_VERTEX_COLOR_MODULATION;
			instanceData->vertexModColor.r = g_vertexColorModRed;
			instanceData->vertexModColor.g = g_vertexColorModGreen;
			instanceData->vertexModColor.b = g_vertexColorModBlue;
		}

		Nu3D::Viewport::GetViewClipRect(&instanceData->clipRect);
		instanceData->unkInt6 = g_primitiveRenderFlags;
		instanceData->sprite = g_instanceSpriteTemplate;
		return instanceData;
	}

	// FUNCTION: TOY2 0x004B8840 [PROVISIONAL]
	InstanceData* InstanceData::AllocFromNodeMatrices(const D3DMATRIX* matrices, int32_t* nodeIndices, int32_t count, int32_t* flags, int32_t renderFlags)
	{
		if (! g_instanceDataFreeCount)
			return 0;

		--g_instanceDataFreeCount;
		D3DMATRIX* dest = g_instanceDataPool[g_instanceDataFreeCount].matrices;

		while (count != 0)
		{
			if (flags && (flags[*nodeIndices] & 1))
			{
				++g_instanceDataFreeCount;
				return 0;
			}

			memcpy(dest, &matrices[*nodeIndices], sizeof(D3DMATRIX));
			dest++;
			nodeIndices++;
			count--;
		}

		g_instanceDataPool[g_instanceDataFreeCount].renderFlags = renderFlags;
		g_instanceDataPool[g_instanceDataFreeCount].lodFactor = g_lodFactor;
		g_instanceDataPool[g_instanceDataFreeCount].horzOffset = g_materialHorzOffset;
		g_instanceDataPool[g_instanceDataFreeCount].vertOffset = g_materialVertOffset;
		g_instanceDataPool[g_instanceDataFreeCount].renderModeFlags = g_vertexLightingEnabled != 0;
		Nu3D::Viewport::GetViewClipRect(&g_instanceDataPool[g_instanceDataFreeCount].clipRect);
		g_instanceDataPool[g_instanceDataFreeCount].unkInt6 = g_primitiveRenderFlags;
		return &g_instanceDataPool[g_instanceDataFreeCount];
	}
}

namespace Renderer
{

	// FUNCTION: TOY2 0x004B85E0 [MATCHED]
	void ProcessPrimitive(Nu3D::InstanceData* instanceData, Nu3D::Primitive* primitive)
	{
		Nu3D::Material* material = Nu3D::Material::GetFreeByIndex(primitive->materialIndex);
		for (;;)
		{
			if (! material)
				return;

			RenderEntry::AllocObj(material, primitive, instanceData);
			if ((material->metadata & 4) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[0], primitive, instanceData);
			if ((material->metadata & 0x40) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[1], primitive, instanceData);
			if ((material->metadata & 0x80) != 0)
				RenderEntry::AllocObj(NGNLoader::g_tex14Materials[2], primitive, instanceData);

			for (int32_t textureStage = 0; textureStage < g_maxSimultaneousTextures; ++textureStage)
			{
				if (! material)
					return;
				material = material->nextPass;
			}
		}
	}

	// FUNCTION: TOY2 0x004B8670 [MATCHED]
	RenderEntry* RenderEntry::AllocObj(Nu3D::Material* material, Nu3D::Primitive* primitive, Nu3D::InstanceData* instanceData)
	{
		if (g_renderEntryFreeCount)
		{
			RenderEntry* entry = &g_renderEntryPool[--g_renderEntryFreeCount];
			entry->primitive = primitive;
			entry->instanceData = instanceData;
			entry->material = material;

			if (instanceData->lodFactor == 1.0f && (material->metadata & 0xE33) == 0)
			{
				entry->type = RENDER_TYPE7;
				entry->next = material->renderEntryHead;
				material->renderEntryHead = entry;
			}
			else
			{
				entry->type = RENDER_TYPE9;
				InsertIntoBucket(entry);
			}
			return entry;
		}

		Logger::DebugLog("rndrentryAllocObj - out of rndrentries");
		return 0;
	}

	// FUNCTION: TOY2 0x004B86F0 [PROVISIONAL]
	void RenderEntry::InsertIntoBucket(RenderEntry* entry)
	{
		Vector3F cameraPosition;
		Vector3F instancePosition;
		Nu3D::Math::GetPositionVector(&Nu3D::Camera::g_activeCamera.transform, &cameraPosition);
		Nu3D::Math::GetPositionVector(&entry->instanceData->matrices[0], &instancePosition);
		Nu3D::Math::VertexSubtract(&cameraPosition, &cameraPosition, &instancePosition);

		float distanceSquared = cameraPosition.x * cameraPosition.x + cameraPosition.y * cameraPosition.y + cameraPosition.z * cameraPosition.z;
		if (distanceSquared < 1.0f)
			distanceSquared = 1.0f;
		entry->distanceSquared = distanceSquared;

		union
		{
			float value;
			uint32_t bits;
		} distance;
		distance.value = distanceSquared;
		int32_t bucketIndex = (distance.bits >> 20) - 0x3F8;
		if (bucketIndex > 255)
			return;

		int32_t depth = 0;
		RenderEntry* previous = 0;
		RenderEntry* current = (RenderEntry*)Nu3D::g_spriteBuckets[bucketIndex];
		while (current && distanceSquared < current->distanceSquared)
		{
			++depth;
			previous = current;
			current = current->next;
		}

		entry->next = current;
		if (previous)
			previous->next = entry;
		else
			Nu3D::g_spriteBuckets[bucketIndex] = (Nu3D::Sprite*)entry;

		if (depth >= Nu3D::g_maxBucketDepth)
			Nu3D::g_maxBucketDepth = depth;
	}

	// FUNCTION: TOY2 0x004B87F0 [MATCHED]
	void RenderPatchList(Nu3D::Patch* patch, const D3DMATRIX* matrices, int32_t* flags, int32_t renderFlags)
	{
		renderFlags |= g_additionalRenderFlags;

		while (patch != 0)
		{
			Nu3D::InstanceData* instanceData =
				Nu3D::InstanceData::AllocFromNodeMatrices(matrices, patch->controlPointIndices, patch->controlPointCount, flags, renderFlags);

			if (instanceData != 0)
			{
				ProcessPatch(instanceData, patch);
			}

			patch = patch->listNext;
		}
	}

	// FUNCTION: TOY2 0x004B8940 [MATCHED]
	void ProcessPatch(Nu3D::InstanceData* instanceData, Nu3D::Patch* patch)
	{
		Nu3D::Material* material = Nu3D::Material::GetFreeByIndex(patch->materialId);
		RenderEntry::AllocPatch(material, patch, instanceData);

		if ((material->metadata & 4) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[0], patch, instanceData);
		}

		if ((material->metadata & 0x40) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[1], patch, instanceData);
		}

		if ((material->metadata & 0x80) != 0)
		{
			RenderEntry::AllocPatch(NGNLoader::g_tex14Materials[2], patch, instanceData);
		}
	}

	// FUNCTION: TOY2 0x004B89B0 [PROVISIONAL]
	RenderEntry* RenderEntry::AllocPatch(Nu3D::Material* material, Nu3D::Patch* patch, Nu3D::InstanceData* instanceData)
	{
		if (g_renderEntryFreeCount)
		{
			RenderEntry* entry = &g_renderEntryPool[--g_renderEntryFreeCount];
			entry->patch = patch;
			entry->instanceData = instanceData;
			entry->material = material;

			if (instanceData->lodFactor == 1.0f && (material->metadata & 0xE33) == 0)
			{
				entry->type = RENDER_TYPE6;
				entry->next = material->renderEntryHead;
				material->renderEntryHead = entry;
				return entry;
			}

			entry->type = RENDER_TYPE8;
			InsertIntoBucket(entry);
			return entry;
		}

		Logger::DebugLog("rndrentryAllocPatch - out of rndrentries");
		return 0;
	}

	// FUNCTION: TOY2 0x004B8400 [MATCHED]
	void FlushMaterialBuckets()
	{
		UnbindMaterial();
		Nu3D::Material* material = Nu3D::Material::GetHead();
		while (material != 0)
		{
			Nu3D::Sprite* command = reinterpret_cast<Nu3D::Sprite*>(material->renderEntryHead);
			if (command != 0)
			{
				g_materialHorzOffset = material->horzOffset;
				g_materialVertOffset = material->vertOffset;
				BindMaterial(material, 0);
				Sprite::DispatchCommand(command);
			}

			material->renderEntryHead = 0;
			material = material->next;
		}
	}

	// FUNCTION: TOY2 0x004B6A90 [MATCHED]
	void FlushTransparentBuckets()
	{
		g_drawingTransparentBuckets = g_isSoftwareRendering != 0 ? 0 : g_hardwareTransparencyEnabled;

		Nu3D::Sprite** bucket = &Nu3D::g_spriteBuckets[255];
		do
		{
			Sprite::DispatchCommand(*bucket);
			bucket--;
		} while (reinterpret_cast<int32_t>(bucket) >= reinterpret_cast<int32_t>(Nu3D::g_spriteBuckets));

		g_drawingTransparentBuckets = 0;
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

	// FUNCTION: TOY2 0x004B6A50 [PROVISIONAL]
	void FlushRenderQueues()
	{
		if (g_drawMaterialBuckets)
			FlushMaterialBuckets();

		if (g_drawTransparentBuckets)
			FlushTransparentBuckets();

		FlushPrimitives();
		ResetRenderPools();

		if (g_isSoftwareRendering)
		{
			SoftwareRenderer::FlushSortedRenderCommands();
			SoftwareRenderer::ResetRenderCommands();
		}
	}

	// FUNCTION: TOY2 0x004B8BF0 [PROVISIONAL]
	RGBA ModulateColorByAlpha(RGBA color, int32_t flags)
	{
		uint8_t blue;
		uint8_t green;
		uint8_t red;

		if (((flags & 0x4000) != 0 || flags == 0x20000000) && g_srcBlendMode == 2)
		{
			blue = (color.a * color.b) >> 8;
			red = (color.a * color.r) >> 8;
			green = (color.a * color.g) >> 8;

			color.b = blue;
			color.r = red;
			color.g = green;
		}
		else
		{
			blue = color.b;
			green = color.g;
			red = color.r;
		}

		if (g_alphaBlendDest != 6 || flags != 0x40000000)
			return color;

		color.a = green;

		if (blue == green && blue == red)
		{
			color.b = 0;
			color.g = 0;
			color.r = 0;
			return color;
		}

		color.b = 255 - blue;
		color.g = 255 - green;
		color.r = 255 - red;

		return color;
	}

	// FUNCTION: TOY2 0x004C27D0 [MATCHED]
	void BindMaterial(Nu3D::Material* material, int32_t force)
	{
		if (g_boundMaterial != material || force != 0)
		{
			g_boundMaterial = material;

			if (material != 0)
			{
				if (g_isSoftwareRendering == 0)
					DrawingDevice::SetLightState(D3DLIGHTSTATE_MATERIAL, material->d3dMaterialHandle);

				for (int32_t stage = 0; stage < g_maxSimultaneousTextures; ++stage)
				{
					if (g_boundTextureIndices[stage] != material->texDataIndex)
					{
						g_boundTextureIndices[stage] = material->texDataIndex;
						DrawingDevice::BindTexWithStage(material->texDataIndex, stage);
					}

					material = material->nextPass;

					if (material == 0)
						return;
				}
			}
			else
			{
				for (int32_t stage = 0; stage < g_maxSimultaneousTextures; ++stage)
					g_boundTextureIndices[stage] = -1;
			}
		}
	}

	// FUNCTION: TOY2 0x004B8450 [MATCHED]
	void UnbindMaterial() { BindMaterial(0, 0); }

	// FUNCTION: TOY2 0x004C2870 [MATCHED]
	void BindTexture(int32_t texIndex)
	{
		g_boundMaterial = 0;

		if (g_boundTextureIndices[0] != texIndex)
		{
			g_boundTextureIndices[0] = texIndex;
			DrawingDevice::BindTexWithStage(texIndex, 0);
		}

		for (int32_t idx = 1; idx < g_maxSimultaneousTextures; ++idx)
		{
			if (g_boundTextureIndices[0] != texIndex)
			{
				g_boundTextureIndices[0] = -1;
				DrawingDevice::BindTexWithStage(-1, idx);
			}
		}
	}

	// FUNCTION: TOY2 0x004B9600 [MATCHED]
	void DrawSingleTexturedTriangle(Nu3D::VertexTL* vertices, int32_t texIndex, int32_t renderFlags)
	{
		InitRenderState(renderFlags);
		BindTexture(texIndex);
		DrawingDevice::DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_0x1C4, vertices, 3, 0x10);
	}
}

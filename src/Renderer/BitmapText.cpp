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

// The bitmap text draw: retail holds 0x0049D390 as one object.
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
				case 0xba:
					glyphX = 64;
					glyphY = 64;
					break;
				case 0xb9:
					glyphX = 80;
					glyphY = 64;
					break;
				case 0xb2:
					glyphX = 96;
					glyphY = 64;
					break;
				case 0xb3:
					glyphX = 112;
					glyphY = 64;
					break;
				case '.':
					glyphX = 128;
					glyphY = 128;
					break;
				case ',':
					glyphX = 160;
					glyphY = 128;
					break;
				case ';':
					glyphX = 192;
					glyphY = 128;
					break;
				case ':':
					glyphX = 224;
					glyphY = 128;
					break;
				case '\\':
					glyphX = 0;
					glyphY = 160;
					break;
				case '/':
					glyphX = 32;
					glyphY = 160;
					break;
				case '!':
					glyphX = 96;
					glyphY = 160;
					break;
				case '?':
					glyphX = 128;
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
				case '\'':
					glyphX = 224;
					glyphY = 160;
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
}

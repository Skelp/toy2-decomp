#include "KiteTail.h"
#include "Levels.h"
#include "NGNLoader/NGNLoader.h"
#include "Renderer/RenderType.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"

namespace Toy2
{
	namespace KiteTail
	{
		// GLOBAL: TOY2 0x00559C20
		Data* g_kiteTails[8];

		// FUNCTION: TOY2 0x0044E620
		void Init(const Vector3I* pos, int32_t count, int32_t spacing, int32_t index, int32_t maxAngle)
		{
			g_kiteTails[index] = (Data*)Levels::g_levelLoadArena;
			Levels::g_levelLoadArena += sizeof(Data);

			g_kiteTails[index]->segmentCount = count;
			g_kiteTails[index]->spacing = spacing;
			g_kiteTails[index]->maxAngle = maxAngle;

			if (count > 0)
			{
				Data* tail = g_kiteTails[index];
				int32_t offset = 0;
				int32_t i = 0;
				do
				{
					tail->segments[i].pos.x = pos->x;
					tail->segments[i].pos.y = pos->y + offset;
					tail->segments[i].pos.z = pos->z;
					tail->velocities[i][0] = 0;
					tail->velocities[i][1] = 0;
					tail->velocities[i][2] = 0;
					tail->velocities[i][3] = 0;
					offset += spacing;
					i++;
				} while (--count);
			}
		}

		// FUNCTION: TOY2 0x0044E6D0
		void Activate(int32_t index) { g_kiteTails[index]->flags |= 1; }

		// FUNCTION: TOY2 0x0044E6F0
		void Deactivate(int32_t index) { g_kiteTails[index]->flags &= ~1; }

		// FUNCTION: TOY2 0x0044EB90
		void Draw()
		{
			uint32_t bitmapWidth = 255;
			uint32_t bitmapHeight = 255;
			RGBA white = { 255, 255, 255, 255 };

			for (int32_t tailIndex = 0; tailIndex < 8; tailIndex++)
			{
				Data* tail = g_kiteTails[tailIndex];
				if (tail && (tail->flags & 1))
				{
					int32_t red = 128;
					for (int32_t segmentIndex = 0; segmentIndex < tail->segmentCount - 1; segmentIndex++)
					{
						Data::Segment* segment = &tail->segments[segmentIndex];
						Vector3I delta;
						delta.x = segment[1].pos.x - segment[0].pos.x;
						delta.y = segment[1].pos.y - segment[0].pos.y;
						delta.z = segment[1].pos.z - segment[0].pos.z;
						Renderer::Sprite::QueueSegment(&segment->pos, &delta, red, 0, 0);
						red -= 4;
					}

					Renderer::SpriteSheet* sheet = Renderer::g_spriteSheets[51];
					int32_t textureDataIndex = NGNLoader::GetTextureDataIndex(sheet->texIndex);
					if (textureDataIndex)
						NGNLoader::RetrieveTextureData(textureDataIndex, &bitmapWidth, &bitmapHeight, 0, 0, 0);

					Vector2F uvTopLeft;
					uvTopLeft.x = (float)sheet->tiles[0].x / (int32_t)bitmapWidth;
					uvTopLeft.y = (float)sheet->tiles[0].y / (int32_t)bitmapHeight;

					Vector2F uvBottomRight;
					uvBottomRight.x = ((float)sheet->tileWidth + sheet->tiles[0].x) / (int32_t)bitmapWidth;
					uvBottomRight.y = ((float)sheet->tileHeight + sheet->tiles[0].y) / (int32_t)bitmapHeight;

					for (int32_t spriteIndex = 0; spriteIndex < tail->segmentCount; spriteIndex++)
					{
						Vector3F position;
						position.x = (float)(tail->segments[spriteIndex].pos.x >> 5);
						position.y = (float)(tail->segments[spriteIndex].pos.y >> 5);
						position.z = (float)(tail->segments[spriteIndex].pos.z >> 5);
						Renderer::Sprite::QueueQuadSprite(&position,
							0,
							140.0f,
							140.0f,
							&uvTopLeft,
							&uvBottomRight,
							textureDataIndex,
							white,
							Renderer::RENDER_ZWRITE | Renderer::RENDER_CULL_NONE | Renderer::RENDER_ALPHA_DEFAULT);
					}
				}
			}
		}
	}
}

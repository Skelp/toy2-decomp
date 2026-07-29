#include "KiteTail.h"
#include "Collision.h"
#include "Levels.h"
#include "NGNLoader/NGNLoader.h"
#include "Nu3D/Math.h"
#include "Renderer/RenderType.h"
#include "Renderer/Renderer.h"
#include "Renderer/Sprite.h"
#include "Renderer/SpriteSheets.h"

namespace Toy2
{
	namespace KiteTail
	{
		static __forceinline int32_t ShiftTowardZero(int32_t value, int32_t bits) { return (value + ((value >> 31) & ((1 << bits) - 1))) >> bits; }

		static __forceinline int32_t SignedDistanceToNearestEdge(int32_t position, int32_t minimum, int32_t maximum)
		{
			int32_t minimumDistance = abs(position - minimum);
			int32_t maximumDistance = abs(position - maximum);
			return minimumDistance <= maximumDistance ? minimumDistance : -maximumDistance;
		}

		// GLOBAL: TOY2 0x00559C20
		Data* g_kiteTails[8];

		// FUNCTION: TOY2 0x0044E620 [PROVISIONAL]
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

		// FUNCTION: TOY2 0x0044E6D0 [MATCHED]
		void Activate(int32_t index) { g_kiteTails[index]->flags |= 1; }

		// FUNCTION: TOY2 0x0044E6F0 [MATCHED]
		void Deactivate(int32_t index) { g_kiteTails[index]->flags &= ~1; }

		// FUNCTION: TOY2 0x0044E710 [PROVISIONAL]
		void Simulate(const Vector3I* anchor, int32_t index)
		{
			const int32_t collisionMinX = 0x1CDC4;
			const int32_t collisionMaxX = 0x32350;
			const int32_t collisionMinZ = 0x2CE98;
			const int32_t collisionMaxZ = 0x4CAD0;
			const int32_t floorMinX = 0x1C9C4;
			const int32_t floorMaxX = 0x31750;
			const int32_t floorMinZ = 0x2CA98;
			const int32_t floorMaxZ = 0x4BED0;
			const int32_t collisionFloorY = -0x722C6;
			const int32_t collisionTopY = -0x721C6;
			const int32_t collisionClearance = 0x100;

			g_kiteTails[index]->segments[0].pos = *anchor;
			Data* tail = g_kiteTails[index];
			int32_t spacing = tail->spacing;
			int32_t gravity = tail->maxAngle;

			int32_t segmentIndex = 1;
			Data::Segment* previous = &tail->segments[0];
			Data::Segment* segment = &tail->segments[1];
			int16_t* velocity = tail->velocities[1];
			Collision::MathScratchVector* constrained = &Collision::g_mathScratch[0];
			for (; segmentIndex < g_kiteTails[index]->segmentCount; segmentIndex++)
			{
				segment->pos.x += velocity[0];
				segment->pos.y += velocity[1];
				segment->pos.z += velocity[2];
				constrained->value.x = (segment->pos.x - previous->pos.x) >> 3;
				constrained->value.y = (segment->pos.y - previous->pos.y) >> 3;
				constrained->value.z = (segment->pos.z - previous->pos.z) >> 3;
				Nu3D::Math::NormalizeToFixedPoint(&constrained->value, &constrained->value);
				constrained->value.x = ((constrained->value.x * spacing) >> 12) + previous->pos.x;
				constrained->value.y = ((constrained->value.y * spacing) >> 12) + previous->pos.y;
				constrained->value.z = ((constrained->value.z * spacing) >> 12) + previous->pos.z;
				previous++;
				segment++;
				velocity += 4;
				constrained++;
			}

			segmentIndex = 1;
			tail = g_kiteTails[index];
			segment = &tail->segments[1];
			velocity = tail->velocities[1];
			constrained = &Collision::g_mathScratch[0];
			for (; segmentIndex < g_kiteTails[index]->segmentCount - 1; segmentIndex++)
			{
				velocity[0] = (int16_t)ShiftTowardZero(velocity[0] * 30, 5) + (int16_t)((constrained->value.x - segment->pos.x) >> 4)
					+ (int16_t)((segment[1].pos.x - constrained[1].value.x) >> 5);
				velocity[1] = (int16_t)((segment[1].pos.y - constrained[1].value.y) >> 5) + (int16_t)((constrained->value.y - segment->pos.y) >> 4)
					+ (int16_t)ShiftTowardZero(velocity[1] * 30, 5) + (int16_t)Renderer::g_frameDelta * (int16_t)gravity;
				velocity[2] = (int16_t)((segment[1].pos.z - constrained[1].value.z) >> 5) + (int16_t)((constrained->value.z - segment->pos.z) >> 4)
					+ (int16_t)ShiftTowardZero(velocity[2] * 30, 5);
				segment++;
				velocity += 4;
				constrained++;
			}

			velocity[0] = (int16_t)ShiftTowardZero(velocity[0] * 30, 5) + (int16_t)((constrained->value.x - segment->pos.x) >> 4);
			velocity[1] = (int16_t)((constrained->value.y - segment->pos.y) >> 4) + (int16_t)ShiftTowardZero(velocity[1] * 30, 5)
				+ (int16_t)Renderer::g_frameDelta * (int16_t)gravity;
			velocity[2] = (int16_t)((constrained->value.z - segment->pos.z) >> 4) + (int16_t)ShiftTowardZero(velocity[2] * 30, 5);

			segmentIndex = 1;
			tail = g_kiteTails[index];
			segment = &tail->segments[1];
			velocity = tail->velocities[1];
			constrained = &Collision::g_mathScratch[0];
			for (; segmentIndex < g_kiteTails[index]->segmentCount; segmentIndex++)
			{
				Vector3I& position = constrained->value;
				if ((uint32_t)(position.x - collisionMinX) < (uint32_t)(collisionMaxX - collisionMinX)
					&& (uint32_t)(position.z - collisionMinZ) < (uint32_t)(collisionMaxZ - collisionMinZ) && position.y >= collisionTopY)
				{
					if ((uint32_t)(position.x - floorMinX) < (uint32_t)(floorMaxX - floorMinX)
						&& (uint32_t)(position.z - floorMinZ) < (uint32_t)(floorMaxZ - floorMinZ))
					{
						position.y = collisionFloorY;
						velocity[3] = 1;
					}
					else
					{
						int32_t distanceX = SignedDistanceToNearestEdge(position.x, collisionMinX, collisionMaxX);
						int32_t distanceZ = SignedDistanceToNearestEdge(position.z, collisionMinZ, collisionMaxZ);
						int32_t floorDistance = position.y - collisionTopY;
						velocity[3] = 0;

						if (abs(distanceX) < floorDistance)
						{
							if (abs(distanceX) < abs(distanceZ))
								position.x = distanceX > 0 ? collisionMinX - collisionClearance : collisionMaxX + collisionClearance;
							else
								position.z = distanceZ > 0 ? collisionMinZ - collisionClearance : collisionMaxZ + collisionClearance;

							velocity[1] += (int16_t)ShiftTowardZero(Renderer::g_frameDelta * gravity * -6, 2);
						}
						else if (abs(distanceZ) < floorDistance)
						{
							position.z = distanceZ > 0 ? collisionMinZ - collisionClearance : collisionMaxZ + collisionClearance;
							velocity[1] += (int16_t)ShiftTowardZero(Renderer::g_frameDelta * gravity * -6, 2);
						}
						else
						{
							position.y = collisionFloorY;
							velocity[3] = 1;
						}
					}
				}

				segment->pos = position;
				segment++;
				velocity += 4;
				constrained++;
			}
		}

		// FUNCTION: TOY2 0x0044EB90 [PROVISIONAL]
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

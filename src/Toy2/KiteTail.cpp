#include "KiteTail.h"
#include "Levels.h"

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
	}
}

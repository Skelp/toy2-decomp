#pragma once

#include "Numerics.h"

namespace Toy2
{
	namespace KiteTail
	{
		struct Data
		{
			struct Segment
			{
				Vector3I pos;
				int32_t reserved;
			} segments[64];

			int16_t velocities[64][4];

			int32_t segmentCount;
			int32_t spacing;
			int32_t maxAngle;
			uint8_t flags;
		};

		extern Data* g_kiteTails[8];

		void Init(const Vector3I* pos, int32_t count, int32_t spacing, int32_t index, int32_t maxAngle);
		void Activate(int32_t index);
		void Deactivate(int32_t index);
		void Draw();
	}
}

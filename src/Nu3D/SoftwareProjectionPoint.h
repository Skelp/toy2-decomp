#pragma once

#include "Common.h"

namespace Nu3D
{
	namespace Camera
	{
		struct SoftwareProjectionPoint
		{
			int16_t x;
			int16_t y;
			int16_t z;
			int16_t reserved;
		};

		STATIC_ASSERT(sizeof(SoftwareProjectionPoint) == 0x8);
	}
}

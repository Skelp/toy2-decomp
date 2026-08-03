#pragma once

#include "Numerics.h"

namespace Toy2
{
	struct PoleRecord
	{
		Vector3I position;
		int32_t type;
		int32_t height;
		int32_t reserved;
	};

	STATIC_ASSERT(sizeof(PoleRecord) == 0x18);
}

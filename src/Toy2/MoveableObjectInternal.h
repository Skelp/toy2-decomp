#pragma once

#include "Toy2/Toy2.h"

namespace Toy2
{
	struct MoveableObjectInitTable
	{
		MoveableObject::InitEntry entries[2];
		int16_t terminator;
	};

	STATIC_ASSERT(sizeof(MoveableObjectInitTable) == 0xE);
	STATIC_ASSERT(offsetof(MoveableObjectInitTable, terminator) == 0xC);
}

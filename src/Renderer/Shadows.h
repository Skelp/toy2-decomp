#pragma once

#include "Common.h"
#include "Numerics.h"

namespace Renderer
{
	namespace Shadows
	{
		struct ShadowInstance
		{
			Vector3I pos;
			int16_t size;
			int16_t opacity;
		};

		extern int16_t g_shadowCount;
		extern int32_t g_unusedShadowVar;
		extern ShadowInstance g_shadowInstances[48];

		STATIC_ASSERT(sizeof(ShadowInstance) == 0x10);
	}
}
